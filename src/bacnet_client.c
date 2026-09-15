#include "bacnet_client.h"

#include "device_table.h"
#include "gateway.h"
#include "mqtt_client.h"
#include "logger.h"
#include "tag_registry.h"

#include <stdio.h>
#include <string.h>

#include "bacnet/bacapp.h"
#include "bacnet/iam.h"
#include "bacnet/rp.h"
#include "bacnet/datalink/datalink.h"
#include "bacnet/basic/binding/address.h"
#include "bacnet/basic/service/h_apdu.h"
#include "bacnet/basic/service/h_iam.h"
#include "bacnet/basic/service/s_rp.h"
#include "bacnet/basic/service/s_whois.h"
#include "bacnet/basic/npdu/h_npdu.h"
#include "bacnet/basic/tsm/tsm.h"
#include "bacnet/basic/service/s_rpm.h"

static uint64_t g_next_discovery_ms = 0;
static uint64_t next_timeout_log_ms;
static unsigned timeout_count;
static size_t device_cursor;
static uint64_t timer_last_ms;
static uint64_t next_recovery_ms;

static bool application_value_to_uint32(const BACNET_APPLICATION_DATA_VALUE *value,
                                        uint32_t *out)
{
    if (!value || !out)
        return false;

    if (value->tag == BACNET_APPLICATION_TAG_UNSIGNED_INT) {
        *out = value->type.Unsigned_Int;
        return true;
    }

    if (value->tag == BACNET_APPLICATION_TAG_ENUMERATED) {
        *out = value->type.Enumerated;
        return true;
    }

    return false;
}

static bool application_value_to_object_id(const BACNET_APPLICATION_DATA_VALUE *value,
                                           BACNET_OBJECT_TYPE *type,
                                           uint32_t *instance)
{
    if (!value || value->tag != BACNET_APPLICATION_TAG_OBJECT_ID)
        return false;

    *type = value->type.Object_Id.type;
    *instance = value->type.Object_Id.instance;
    return true;
}

static bool application_value_to_string(BACNET_APPLICATION_DATA_VALUE *value,
                                        char *dst,
                                        size_t n)
{
    size_t len;

    if (!value || !dst || !n ||
        value->tag != BACNET_APPLICATION_TAG_CHARACTER_STRING)
        return false;

    len = characterstring_length(&value->type.Character_String);
    if (len >= n)
        len = n - 1;

    memcpy(dst, characterstring_value(&value->type.Character_String), len);
    dst[len] = '\0';
    return true;
}

static bool application_value_to_point_value(BACNET_APPLICATION_DATA_VALUE *value,
                                             POINT_STATE *point)
{
    if (!value || !point)
        return false;

    switch (value->tag) {
        case BACNET_APPLICATION_TAG_REAL:
            point->value_kind = VALUE_NUMBER;
            point->numeric_value = value->type.Real;
            return true;
        case BACNET_APPLICATION_TAG_DOUBLE:
            point->value_kind = VALUE_NUMBER;
            point->numeric_value = value->type.Double;
            return true;
        case BACNET_APPLICATION_TAG_UNSIGNED_INT:
            point->value_kind = VALUE_NUMBER;
            point->numeric_value = (double)value->type.Unsigned_Int;
            return true;
        case BACNET_APPLICATION_TAG_SIGNED_INT:
            point->value_kind = VALUE_NUMBER;
            point->numeric_value = (double)value->type.Signed_Int;
            return true;
        case BACNET_APPLICATION_TAG_ENUMERATED:
            point->value_kind = VALUE_ENUM;
            point->numeric_value = (double)value->type.Enumerated;
            if (point->object_type == OBJECT_BINARY_INPUT ||
                point->object_type == OBJECT_BINARY_OUTPUT ||
                point->object_type == OBJECT_BINARY_VALUE) {
                point->value_kind = VALUE_BOOL;
                point->bool_value = value->type.Enumerated != 0;
            }
            return true;
        case BACNET_APPLICATION_TAG_BOOLEAN:
            point->value_kind = VALUE_BOOL;
            point->bool_value = value->type.Boolean;
            return true;
        case BACNET_APPLICATION_TAG_CHARACTER_STRING:
            point->value_kind = VALUE_STRING;
            return application_value_to_string(value,
                                               point->string_value,
                                               sizeof(point->string_value));
        default:
            return false;
    }
}

static void unit_to_text(uint32_t unit, char *dst, size_t n)
{
    switch (unit) {
        case 62: safe_copy(dst, n, "°C"); break;
        case 64: safe_copy(dst, n, "°F"); break;
        case 98: safe_copy(dst, n, "%"); break;
        case 95: safe_copy(dst, n, "Pa"); break;
        case 3:  safe_copy(dst, n, "A"); break;
        case 5:  safe_copy(dst, n, "V"); break;
        case 47: safe_copy(dst, n, "W"); break;
        case 48: safe_copy(dst, n, "kW"); break;
        default:
            snprintf(dst, n, "unit:%lu", (unsigned long)unit);
            break;
    }
}

static void request_clear(void)
{
    if (g_request.active) tsm_free_invoke_id(g_request.invoke_id);
    memset(&g_request, 0, sizeof(g_request));
}

static bool request_matches(BACNET_ADDRESS *src, uint8_t id)
{
    BACNET_ADDRESS dest; unsigned max_apdu;
    return g_request.active && id == g_request.invoke_id &&
        address_get_by_device(g_request.device->device_id, &max_apdu, &dest) &&
        address_match(src, &dest);
}

static void device_answered(DEVICE_STATE *d)
{
    if (d->state == 2) LOG_INFOF("BACnet device online: device=%lu", (unsigned long)d->device_id);
    d->state = 1; d->failures = 0; d->backoff = 0; d->retry_after_ms = 0;
    d->last_seen_ms = monotonic_ms(); d->last_response = unix_time_now();
}

static void point_failed(POINT_STATE *p)
{
    if (p->failures < 6) p->failures++;
    p->retry_after_ms = monotonic_ms() + (p->failures == 1 ? 10000 : p->failures == 2 ? 30000 : 60000);
}

static void device_timed_out(DEVICE_STATE *d)
{
    if (d->failures < 3) d->failures++;
    if (d->failures >= 3) {
        if (d->state != 2) LOG_WARNF("BACnet device offline: device=%lu", (unsigned long)d->device_id);
        d->state = 2;
        d->retry_after_ms = monotonic_ms() + (d->backoff == 0 ? 10000 : d->backoff == 1 ? 30000 : 60000);
        if (d->backoff < 2) d->backoff++;
    } else d->retry_after_ms = monotonic_ms() + 1000;
}

static bool binary_point(POINT_STATE *p)
{
    return p->object_type==OBJECT_BINARY_INPUT || p->object_type==OBJECT_BINARY_OUTPUT || p->object_type==OBJECT_BINARY_VALUE;
}
static bool multistate_point(POINT_STATE *p)
{
    return p->object_type==OBJECT_MULTI_STATE_INPUT || p->object_type==OBJECT_MULTI_STATE_OUTPUT || p->object_type==OBJECT_MULTI_STATE_VALUE;
}
static void state_text_advance(POINT_STATE *p, REQUEST_KIND kind)
{
    if (kind==REQ_POINT_INACTIVE_TEXT) p->metadata_step=4;
    else if (kind==REQ_POINT_STATE_TEXT && p->state_text_index < p->state_text_count) p->state_text_index++;
    else p->metadata_complete=true;
}

static void request_failed(bool timeout)
{
    DEVICE_STATE *d = g_request.device;
    if (!d) return;
    if (timeout) device_timed_out(d);
    if (g_request.point) point_failed(g_request.point);
    for (unsigned i=0; i<g_request.batch_count; i++) point_failed(&d->points[g_request.batch[i]]);
    /* A failed group is split on its next turn; no immediate retry monopolizes the channel. */
    if (g_request.kind == REQ_POINT_MULTIPLE && d->rpm_limit > 1)
        d->rpm_limit = (g_request.batch_count + 1) / 2;
    if (!g_request.point && g_request.kind != REQ_POINT_MULTIPLE)
        d->discovery_after_ms = monotonic_ms() + 10000;
    /* Optional metadata must not prevent polling. Preserve cached text on failures. */
    if (g_request.point && g_request.kind >= REQ_POINT_NAME && g_request.kind <= REQ_POINT_UNITS) {
        g_request.point->metadata_step++;
        if (g_request.point->metadata_step >= 3 && !binary_point(g_request.point) && !multistate_point(g_request.point)) g_request.point->metadata_complete = true;
    }
    if (g_request.point && g_request.kind >= REQ_POINT_STATE_COUNT) state_text_advance(g_request.point,g_request.kind);
    if (!timeout && g_request.kind == REQ_DEVICE_NAME) d->have_name = true;
    if (!timeout && g_request.kind == REQ_OBJECT_LIST_ITEM) d->next_object_index++;
    request_clear();
}

/* Validate primitive length before passing remote bytes to the legacy stack decoder. */
static int primitive_length(const uint8_t *p, unsigned n)
{
    if (!n || (p[0] & 8) || (p[0] >> 4) > 12) return -1;
    unsigned tag=p[0] >> 4, len=p[0]&7, head=1;
    if (tag <= 1) return (tag==0 ? len==0 : len<=1) ? 1 : -1;
    if (len > 5) return -1;
    if (len == 5) {
        if (n < 2) return -1;
        len=p[1]; head=2;
        if (len==254) { if(n<4)return -1; len=((unsigned)p[2]<<8)|p[3];head=4; }
        else if(len==255) { if(n<6)return -1; len=((unsigned)p[2]<<24)|((unsigned)p[3]<<16)|((unsigned)p[4]<<8)|p[5];head=6; }
    }
    if (head>n || len>n-head) return -1;
    if ((tag==4 && len!=4) || (tag==5 && len!=8) || (tag==12 && len!=4)) return -1;
    return (int)(head+len);
}

static bool send_read_property(REQUEST_KIND kind,
                               DEVICE_STATE *device,
                               POINT_STATE *point,
                               BACNET_OBJECT_TYPE type,
                               uint32_t instance,
                               BACNET_PROPERTY_ID property,
                               uint32_t array_index,
                               uint32_t list_index)
{
    uint8_t invoke_id;

    if (g_request.active || !device)
        return false;

    invoke_id = Send_Read_Property_Request(device->device_id,
                                           type,
                                           instance,
                                           property,
                                           array_index);
    if (!invoke_id)
        return false;

    memset(&g_request, 0, sizeof(g_request));
    g_request.active = true;
    g_request.kind = kind;
    g_request.invoke_id = invoke_id;
    g_request.sent_ms = monotonic_ms();
    g_request.device = device;
    g_request.point = point;
    g_request.object_list_index = list_index;
    return true;
}

static void gateway_i_am_handler(uint8_t *request,
                                 uint16_t length,
                                 BACNET_ADDRESS *src)
{
    uint32_t device_id = 0;
    unsigned max_apdu = 0;
    int segmentation = 0;
    uint16_t vendor_id = 0;
    int rc;

    (void)length;
    rc = iam_decode_service_request(request,
                                    &device_id,
                                    &max_apdu,
                                    &segmentation,
                                    &vendor_id);
    if (rc >= 0) {
        /* address_add_binding() (via handler_i_am_bind()) only updates an
         * address cache entry that already exists - it never creates one,
         * so ReadProperty's address_get_by_device() lookup always failed
         * silently and no request was ever sent (confirmed on-device:
         * devices discovered, zero points, no timeout/abort/reject logs
         * because nothing was ever transmitted). address_add() has the
         * missing "allocate a free slot" fallback. */
        address_add(device_id, max_apdu, src);
        DEVICE_STATE *d = get_or_create_device(device_id);
        if (d) {
            d->max_apdu = max_apdu;
            d->last_iam_ms = monotonic_ms();
        }
    }
}

static void gateway_read_property_ack_handler(
    uint8_t *request,
    uint16_t length,
    BACNET_ADDRESS *src,
    BACNET_CONFIRMED_SERVICE_ACK_DATA *service_data)
{
    BACNET_READ_PROPERTY_DATA rp;
    BACNET_APPLICATION_DATA_VALUE value;
    int rc;

    (void)src;

    if (!request_matches(src, service_data->invoke_id) || g_request.kind == REQ_POINT_MULTIPLE)
        return;

    memset(&rp, 0, sizeof(rp));
    memset(&value, 0, sizeof(value));

    rc = rp_ack_decode_service_request(request, length, &rp);
    if (rc <= 0) {
        fprintf(stderr, "Malformed RP ACK invoke=%u\n", service_data->invoke_id);
        request_failed(false);
        return;
    }

    if (primitive_length(rp.application_data, rp.application_data_len) != rp.application_data_len) { request_failed(false); return; }
    rc = bacapp_decode_application_data(rp.application_data,
                                        rp.application_data_len,
                                        &value);
    if (rc <= 0) {
        request_failed(false);
        return;
    }

    if (g_request.point) {
        if (rp.object_type != g_request.point->object_type || rp.object_instance != g_request.point->object_instance) { request_failed(false); return; }
    } else if (rp.object_type != OBJECT_DEVICE || rp.object_instance != g_request.device->device_id) { request_failed(false); return; }
    BACNET_PROPERTY_ID expected = g_request.kind==REQ_POINT_PRESENT_VALUE ? PROP_PRESENT_VALUE :
        (g_request.kind==REQ_POINT_STATE_COUNT || g_request.kind==REQ_POINT_STATE_TEXT) ? PROP_STATE_TEXT :
        g_request.kind==REQ_POINT_INACTIVE_TEXT ? PROP_INACTIVE_TEXT : g_request.kind==REQ_POINT_ACTIVE_TEXT ? PROP_ACTIVE_TEXT :
        g_request.kind==REQ_POINT_DESCRIPTION ? PROP_DESCRIPTION : g_request.kind==REQ_POINT_UNITS ? PROP_UNITS :
        (g_request.kind==REQ_OBJECT_LIST_COUNT || g_request.kind==REQ_OBJECT_LIST_ITEM) ? PROP_OBJECT_LIST : PROP_OBJECT_NAME;
    if (rp.object_property != expected) { request_failed(false); return; }
    if ((g_request.kind==REQ_POINT_STATE_COUNT && rp.array_index!=0) ||
        (g_request.kind==REQ_POINT_STATE_TEXT && rp.array_index!=g_request.point->state_text_index)) { request_failed(false); return; }
    device_answered(g_request.device);
    switch (g_request.kind) {
        case REQ_DEVICE_NAME:
            if (application_value_to_string(&value,
                                            g_request.device->name,
                                            sizeof(g_request.device->name))) {
                g_request.device->have_name = true;
                printf("DEVICE %lu NAME=%s\n",
                       (unsigned long)g_request.device->device_id,
                       g_request.device->name);
            }
            break;

        case REQ_OBJECT_LIST_COUNT: {
            uint32_t count;
            if (application_value_to_uint32(&value, &count)) {
                g_request.device->object_count = count;
                g_request.device->have_object_count = true;
                g_request.device->next_object_index = 1;
                printf("DEVICE %lu OBJECTS=%lu\n",
                       (unsigned long)g_request.device->device_id,
                       (unsigned long)count);
            }
            break;
        }

        case REQ_OBJECT_LIST_ITEM: {
            BACNET_OBJECT_TYPE type;
            uint32_t instance;

            if (application_value_to_object_id(&value, &type, &instance)) {
                if (type != OBJECT_DEVICE)
                    add_point(g_request.device, type, instance);

                if (g_request.object_list_index >= g_request.device->object_count)
                    g_request.device->object_list_complete = true;
                else
                    g_request.device->next_object_index = g_request.object_list_index + 1;
            }
            break;
        }

        case REQ_POINT_NAME:
            if (g_request.point) {
                application_value_to_string(&value,
                                            g_request.point->name,
                                            sizeof(g_request.point->name));
                g_request.point->metadata_step = 1;
            }
            break;

        case REQ_POINT_DESCRIPTION:
            if (g_request.point) {
                application_value_to_string(&value,
                                            g_request.point->description,
                                            sizeof(g_request.point->description));
                g_request.point->metadata_step = 2;
            }
            break;

        case REQ_POINT_UNITS:
            if (g_request.point) {
                uint32_t unit;
                if (application_value_to_uint32(&value, &unit))
                    unit_to_text(unit,
                                 g_request.point->unit,
                                 sizeof(g_request.point->unit));
                g_request.point->metadata_step = 3;
                g_request.point->metadata_complete = true;
                mqtt_publish_config(g_request.device, g_request.point);
            }
            break;

        case REQ_POINT_STATE_COUNT: {
            uint32_t count;
            if (application_value_to_uint32(&value,&count) && count>0 && count<=65535) {
                g_request.point->state_text_count=count;
                g_request.point->state_text_index=1;
                g_request.point->metadata_step=4;
            } else g_request.point->metadata_complete=true;
            break;
        }
        case REQ_POINT_STATE_TEXT:
        case REQ_POINT_INACTIVE_TEXT:
        case REQ_POINT_ACTIVE_TEXT: {
            if (value.tag==BACNET_APPLICATION_TAG_CHARACTER_STRING) {
                char text[MAX_CHARACTER_STRING_BYTES+1];
                if (application_value_to_string(&value,text,sizeof(text))) {
                    uint32_t state=g_request.kind==REQ_POINT_STATE_TEXT ? g_request.point->state_text_index : g_request.kind==REQ_POINT_ACTIVE_TEXT ? 1 : 0;
                    tag_registry_set_state_text(g_request.device,g_request.point,state,text);
                }
            }
            state_text_advance(g_request.point,g_request.kind);
            break;
        }
        case REQ_POINT_PRESENT_VALUE:
            if (g_request.point &&
                application_value_to_point_value(&value, g_request.point)) {
                g_request.point->failures = 0;
                g_request.point->retry_after_ms = 0;
                g_request.point->have_value = true;
                mqtt_publish_live_if_needed(g_request.device, g_request.point);
            }
            if (g_request.point)
                g_request.point->next_poll_ms = monotonic_ms() + g_poll_ms;
            break;

        default:
            break;
    }

    request_clear();
}

static void gateway_error_handler(BACNET_ADDRESS *src, uint8_t id,
                                  BACNET_ERROR_CLASS cls, BACNET_ERROR_CODE code)
{
    (void)cls; (void)code;
    if (!request_matches(src,id)) return;
    device_answered(g_request.device);
    request_failed(false);
}
static void gateway_abort_handler(BACNET_ADDRESS *src, uint8_t id, uint8_t reason, bool server)
{
    (void)reason;
    if (!server || !request_matches(src,id)) return;
    device_answered(g_request.device);
    request_failed(false);
}
static void gateway_reject_handler(BACNET_ADDRESS *src, uint8_t id, uint8_t reason)
{
    if (!request_matches(src,id)) return;
    device_answered(g_request.device);
    if (g_request.kind == REQ_POINT_MULTIPLE && reason == REJECT_REASON_UNRECOGNIZED_SERVICE) {
        g_request.device->rpm_limit = 1;
        g_request.device->rpm_unsupported = true;
    }
    request_failed(false);
}

static void gateway_rpm_ack(uint8_t *buf, uint16_t n, BACNET_ADDRESS *src,
                            BACNET_CONFIRMED_SERVICE_ACK_DATA *svc)
{
    if (!request_matches(src,svc->invoke_id) || g_request.kind != REQ_POINT_MULTIPLE) return;
    DEVICE_STATE *d=g_request.device;
    bool seen[RPM_BATCH_MAX]={false};
    unsigned pos=0;
    /* Only the requested scalar Present_Value results are accepted. No heap allocation. */
    while (pos<n) {
        if (n-pos<10 || buf[pos++]!=0x0c) goto malformed;
        uint32_t obj=((uint32_t)buf[pos]<<24)|((uint32_t)buf[pos+1]<<16)|((uint32_t)buf[pos+2]<<8)|buf[pos+3];pos+=4;
        if (buf[pos++]!=0x1e || buf[pos++]!=0x29 || buf[pos++]!=PROP_PRESENT_VALUE) goto malformed;
        unsigned i;
        for(i=0;i<g_request.batch_count;i++) {
            POINT_STATE *p=&d->points[g_request.batch[i]];
            if ((unsigned)p->object_type==(obj>>22) && p->object_instance==(obj&0x3fffff)) break;
        }
        if (i==g_request.batch_count || seen[i]) goto malformed;
        POINT_STATE *p=&d->points[g_request.batch[i]];
        if (buf[pos]==0x4e) {
            pos++; int len=primitive_length(buf+pos,n-pos);
            if(len<0 || (unsigned)len+2>n-pos || buf[pos+len]!=0x4f || buf[pos+len+1]!=0x1f) goto malformed;
            BACNET_APPLICATION_DATA_VALUE value;memset(&value,0,sizeof(value));
            if (bacapp_decode_application_data(buf+pos,len,&value)==len && application_value_to_point_value(&value,p)) {
                p->have_value=true;p->failures=0;p->retry_after_ms=0;
                p->next_poll_ms=monotonic_ms()+g_poll_ms;
                mqtt_publish_live_if_needed(d,p);
            } else point_failed(p);
            pos+=len+2;
        } else if(buf[pos]==0x5e) {
            pos++;
            for(unsigned j=0;j<2;j++) {
                if(pos>=n || (buf[pos]>>4)!=9) goto malformed;
                int len=primitive_length(buf+pos,n-pos);if(len<0)goto malformed;pos+=len;
            }
            if(n-pos<2 || buf[pos++]!=0x5f || buf[pos++]!=0x1f)goto malformed;
            point_failed(p);
        } else goto malformed;
        seen[i]=true;
    }
    device_answered(d);
    if (n) d->rpm_confirmed=true;
    for(unsigned i=0;i<g_request.batch_count;i++) if(!seen[i]) point_failed(&d->points[g_request.batch[i]]);
    request_clear();return;
malformed:
    request_failed(false);
}

static bool schedule_device_work(DEVICE_STATE *device)
{
    if (!device || !device->used)
        return false;

    if (!device->have_name) {
        return send_read_property(REQ_DEVICE_NAME,
                                  device,
                                  NULL,
                                  OBJECT_DEVICE,
                                  device->device_id,
                                  PROP_OBJECT_NAME,
                                  BACNET_ARRAY_ALL,
                                  0);
    }

    if (!device->have_object_count) {
        return send_read_property(REQ_OBJECT_LIST_COUNT,
                                  device,
                                  NULL,
                                  OBJECT_DEVICE,
                                  device->device_id,
                                  PROP_OBJECT_LIST,
                                  0,
                                  0);
    }

    if (!device->object_list_complete) {
        if (!device->next_object_index)
            device->next_object_index = 1;

        if (device->next_object_index > device->object_count) {
            device->object_list_complete = true;
            return false;
        }

        return send_read_property(REQ_OBJECT_LIST_ITEM,
                                  device,
                                  NULL,
                                  OBJECT_DEVICE,
                                  device->device_id,
                                  PROP_OBJECT_LIST,
                                  device->next_object_index,
                                  device->next_object_index);
    }

    return false;
}

static bool schedule_metadata(DEVICE_STATE *device, POINT_STATE *point)
{
    if (!device || !point || point->metadata_complete)
        return false;

    if (point->metadata_step == 0) {
        return send_read_property(REQ_POINT_NAME,
                                  device,
                                  point,
                                  point->object_type,
                                  point->object_instance,
                                  PROP_OBJECT_NAME,
                                  BACNET_ARRAY_ALL,
                                  0);
    }

    if (point->metadata_step == 1) {
        return send_read_property(REQ_POINT_DESCRIPTION,
                                  device,
                                  point,
                                  point->object_type,
                                  point->object_instance,
                                  PROP_DESCRIPTION,
                                  BACNET_ARRAY_ALL,
                                  0);
    }

    if (point->metadata_step == 2) {
        if (point->object_type == OBJECT_ANALOG_INPUT ||
            point->object_type == OBJECT_ANALOG_OUTPUT ||
            point->object_type == OBJECT_ANALOG_VALUE) {
            return send_read_property(REQ_POINT_UNITS,
                                      device,
                                      point,
                                      point->object_type,
                                      point->object_instance,
                                      PROP_UNITS,
                                      BACNET_ARRAY_ALL,
                                      0);
        }

        point->unit[0] = '\0';
        point->metadata_step = 3;
        point->metadata_complete = !binary_point(point) && !multistate_point(point);
        mqtt_publish_config(device, point);
        return false;
    }

    if (binary_point(point)) {
        bool active=point->metadata_step>=4;
        return send_read_property(active?REQ_POINT_ACTIVE_TEXT:REQ_POINT_INACTIVE_TEXT, device, point,
            point->object_type,point->object_instance,active?PROP_ACTIVE_TEXT:PROP_INACTIVE_TEXT,BACNET_ARRAY_ALL,0);
    }
    if (multistate_point(point)) {
        bool count=point->metadata_step==3;
        return send_read_property(count?REQ_POINT_STATE_COUNT:REQ_POINT_STATE_TEXT,device,point,
            point->object_type,point->object_instance,PROP_STATE_TEXT,count?0:point->state_text_index,0);
    }
    point->metadata_complete = true;
    return false;
}

static bool schedule_poll(DEVICE_STATE *device,
                          POINT_STATE *point,
                          uint64_t now)
{
    if ((!point->metadata_complete && point->metadata_step<3) || point->next_poll_ms > now)
        return false;

    bool sent = send_read_property(REQ_POINT_PRESENT_VALUE,
                              device,
                              point,
                              point->object_type,
                              point->object_instance,
                              PROP_PRESENT_VALUE,
                              BACNET_ARRAY_ALL,
                              0);
    if (sent) device->last_poll_count = 1;
    return sent;
}

unsigned g_rpm_batch_max = RPM_BATCH_DEFAULT;

void bacnet_client_set_rpm_max(unsigned limit)
{
    if (limit < 1 || limit > RPM_BATCH_MAX || limit == g_rpm_batch_max) return;
    g_rpm_batch_max = limit;
    for (size_t i=0; i<MAX_DEVICES; i++) {
        DEVICE_STATE *d=&g_devices[i];
        if (d->used) d->rpm_limit=d->rpm_unsupported ? 1 : 0;
    }
}

static bool schedule_values(DEVICE_STATE *d, uint64_t now)
{
    if (!d->point_count || now < d->values_check_ms) return false;
    size_t selected[RPM_BATCH_MAX]; unsigned count=0;
    unsigned budget=d->max_apdu ? d->max_apdu : 480;
    if (budget>MAX_APDU) budget=MAX_APDU;
    unsigned used=3, limit=d->rpm_limit ? d->rpm_limit : g_rpm_batch_max;
    if (limit>g_rpm_batch_max) limit=g_rpm_batch_max;
    for(size_t step=0;step<d->point_count;step++) {
        size_t i=d->point_cursor;d->point_cursor=(i+1)%d->point_count;
        POINT_STATE *p=&d->points[i];
        if ((!p->metadata_complete && p->metadata_step<3) || p->next_poll_ms>now || p->retry_after_ms>now) continue;
        /* Schedule and strings are variable length: read them individually. */
        bool variable=p->object_type==OBJECT_CHARACTERSTRING_VALUE || p->object_type==OBJECT_SCHEDULE;
        if (variable && count) {d->point_cursor=i;break;}
        if (variable || limit==1) return schedule_poll(d,p,now);
        if (used+20>budget || count>=limit) {d->point_cursor=i;break;}
        selected[count++]=i;used+=20; /* includes common property-error responses */
    }
    if (!count) { d->values_check_ms = now + 100; return false; }
    if(count==1) return schedule_poll(d,&d->points[selected[0]],now);
    BACNET_READ_ACCESS_DATA objects[RPM_BATCH_MAX];
    BACNET_PROPERTY_REFERENCE props[RPM_BATCH_MAX];uint8_t pdu[MAX_MPDU];
    memset(objects,0,sizeof(objects));memset(props,0,sizeof(props));
    for(unsigned i=0;i<count;i++) {
        POINT_STATE *p=&d->points[selected[i]];
        objects[i].object_type=p->object_type;objects[i].object_instance=p->object_instance;
        objects[i].listOfProperties=&props[i];objects[i].next=i+1<count?&objects[i+1]:NULL;
        props[i].propertyIdentifier=PROP_PRESENT_VALUE;props[i].propertyArrayIndex=BACNET_ARRAY_ALL;
    }
    uint8_t id=Send_Read_Property_Multiple_Request(pdu,sizeof(pdu),d->device_id,objects);
    if(!id) {d->retry_after_ms=now+1000;return false;}
    memset(&g_request,0,sizeof(g_request));g_request.active=true;g_request.invoke_id=id;
    g_request.kind=REQ_POINT_MULTIPLE;g_request.device=d;g_request.sent_ms=now;g_request.batch_count=count;
    memcpy(g_request.batch,selected,count*sizeof(selected[0]));
    if(!d->rpm_limit)d->rpm_limit=limit;
    d->last_poll_count=count;
    return true;
}

static void scheduler_run(void)
{
    uint64_t now=monotonic_ms();
    if(g_request.active) {
        /* Recovery probes get one timeout window, without waiting for a retry. */
        if (g_request.device->failures && now-g_request.sent_ms >= g_rp_timeout_ms) {
            request_failed(true);
            return;
        }
        /* TSM owns retransmission and timeout. A bounded watchdog also releases its invoke ID. */
        if(tsm_invoke_id_failed(g_request.invoke_id) || tsm_invoke_id_free(g_request.invoke_id) ||
           now-g_request.sent_ms>(uint64_t)g_rp_timeout_ms*3+1000) {
            ++timeout_count;
            if(now>=next_timeout_log_ms) {
                LOG_WARNF("BACnet read timeout: device=%lu (%u since last report)",
                          (unsigned long)g_request.device->device_id,timeout_count);
                timeout_count=0;next_timeout_log_ms=now+60000;
            }
            request_failed(true);
        }
        return;
    }
    for(size_t step=0;step<MAX_DEVICES;step++) {
        size_t i=device_cursor; device_cursor=(i+1)%MAX_DEVICES;
        DEVICE_STATE *d=&g_devices[i];
        if(!d->used || d->retry_after_ms>now) continue;
        BACNET_ADDRESS dest;unsigned apdu;
        if(!address_get_by_device(d->device_id,&apdu,&dest)) {
            device_timed_out(d);continue;
        }
        d->max_apdu=apdu;
        if(d->state==2 || d->failures) {
            /* WhoIs already checks presence. Do not repeatedly unicast-probe a
             * silent offline device; a fresh I-Am makes it eligible again.
             * I-Am alone never clears read failures or bypasses the budget. */
            if (d->state==2 && (!d->last_iam_ms || now-d->last_iam_ms > (uint64_t)g_discovery_ms*3)) continue;
            if (now < next_recovery_ms) continue;
            if(send_read_property(REQ_DEVICE_NAME,d,NULL,OBJECT_DEVICE,d->device_id,PROP_OBJECT_NAME,BACNET_ARRAY_ALL,0)) {
                next_recovery_ms = now + (uint64_t)g_rp_timeout_ms * 10 + 1000;
                return;
            }
            d->retry_after_ms=now+1000;continue;
        }
        /* Rotate discovery, metadata, values as well as devices. Cached points need not wait for discovery. */
        for(unsigned phase=0;phase<3;phase++) {
            unsigned task=d->phase;d->phase=(task+1)%3;
            if(task==0 && d->discovery_after_ms<=now && schedule_device_work(d))return;
            if(task==1 && d->point_count && !d->metadata_done) {
                bool complete = true;
                for(size_t j=0;j<d->point_count;j++) {
                    size_t k=d->metadata_cursor;d->metadata_cursor=(k+1)%d->point_count;
                    if (!d->points[k].metadata_complete) complete = false;
                    else if (!d->points[k].metadata_synced) {
                        POINT_STATE *p = &d->points[k];
                        if (!tag_registry_get(d->device_id,p->object_type,p->object_instance)) { complete = false; continue; }
                        tag_registry_set_metadata(d->device_id,p->object_type,p->object_instance,p->name,p->unit,p->description);
                        p->metadata_synced = true;
                    }
                    if(d->points[k].retry_after_ms<=now && schedule_metadata(d,&d->points[k]))return;
                }
                d->metadata_done = complete;
            }
            if(task==2 && schedule_values(d,now))return;
        }
    }
}

#ifndef BACNET_SCHEDULER_TEST
int bacnet_client_init(const char *interface_name)
{
    /* bacnet-stack's BIP_Port (ports/linux/bip-init.c) has no static
     * initializer, so it defaults to 0. Apps normally get 0xBAC0 via
     * dlenv_init()'s BACNET_IP_PORT fallback; we call datalink_init()
     * directly, so without this bip_init() binds both its unicast and
     * broadcast sockets to OS-assigned ephemeral ports instead of 47808 -
     * confirmed on-device (two random ports bound, nothing on 0xBAC0). */
    bip_set_port(0xBAC0);

    /* Per-packet BIP/BVLC traces are disabled in normal operation. */

    request_clear();
    address_init();
    apdu_timeout_set(g_rp_timeout_ms);
    apdu_retries_set(1);
    timer_last_ms = monotonic_ms();

    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_I_AM,
                                 gateway_i_am_handler);
    apdu_set_confirmed_ack_handler(SERVICE_CONFIRMED_READ_PROPERTY,
                                   gateway_read_property_ack_handler);
    apdu_set_confirmed_ack_handler(SERVICE_CONFIRMED_READ_PROP_MULTIPLE, gateway_rpm_ack);
    apdu_set_error_handler(SERVICE_CONFIRMED_READ_PROPERTY, gateway_error_handler);
    apdu_set_error_handler(SERVICE_CONFIRMED_READ_PROP_MULTIPLE, gateway_error_handler);
    apdu_set_abort_handler(gateway_abort_handler);
    apdu_set_reject_handler(gateway_reject_handler);

    if (!datalink_init((char *)interface_name))
        return -1;

    g_next_discovery_ms = 0;
    return 0;
}

int bacnet_client_reinit(const char *interface_name)
{
    /* Live interface switch, triggered by config_reload.c when
     * bacnet_interface changes in UCI: tear down the current datalink
     * socket and bring it back up on the new interface, without exiting
     * the process. The device/point table is intentionally left as-is -
     * address_add() already updates-or-creates by device_id, so a fresh
     * WhoIs/I-Am cycle on the new interface reconciles it naturally. */
    datalink_cleanup();
    return bacnet_client_init(interface_name);
}

void bacnet_client_loop(void)
{
    BACNET_ADDRESS src;
    uint8_t rx[MAX_MPDU];
    uint16_t pdu_len;
    uint64_t now = monotonic_ms();
    unsigned interval_ms;

    if (now >= g_next_discovery_ms) {
        interval_ms = (device_count() == 0 && DISCOVERY_EMPTY_MS < g_discovery_ms)
                          ? DISCOVERY_EMPTY_MS
                          : g_discovery_ms;
        Send_WhoIs(-1, -1);
        g_next_discovery_ms = now + interval_ms;
    }

    memset(&src, 0, sizeof(src));
    pdu_len = datalink_receive(&src, rx, sizeof(rx), 20);
    if (pdu_len)
        npdu_handler(&src, rx, pdu_len);

    now = monotonic_ms();
    uint64_t elapsed=now-timer_last_ms;timer_last_ms=now;
    while(elapsed) {uint16_t dt=elapsed>65535?65535:(uint16_t)elapsed;tsm_timer_milliseconds(dt);elapsed-=dt;}
    scheduler_run();
}

void bacnet_client_cleanup(void)
{
    request_clear();
    datalink_cleanup();
}

#endif
