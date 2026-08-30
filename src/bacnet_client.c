#include "bacnet_client.h"

#include "device_table.h"
#include "gateway.h"
#include "mqtt_client.h"

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

static uint64_t g_next_discovery_ms = 0;

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
            point->value_kind = VALUE_NUMBER;
            point->numeric_value = (double)value->type.Enumerated;
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
    memset(&g_request, 0, sizeof(g_request));
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
        get_or_create_device(device_id);
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

    if (!g_request.active || service_data->invoke_id != g_request.invoke_id)
        return;

    memset(&rp, 0, sizeof(rp));
    memset(&value, 0, sizeof(value));

    rc = rp_ack_decode_service_request(request, length, &rp);
    if (rc <= 0) {
        fprintf(stderr, "Malformed RP ACK invoke=%u\n", service_data->invoke_id);
        request_clear();
        return;
    }

    rc = bacapp_decode_application_data(rp.application_data,
                                        rp.application_data_len,
                                        &value);
    if (rc <= 0) {
        fprintf(stderr, "Cannot decode RP value invoke=%u\n", service_data->invoke_id);
        request_clear();
        return;
    }

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

        case REQ_POINT_PRESENT_VALUE:
            if (g_request.point &&
                application_value_to_point_value(&value, g_request.point)) {
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

static void gateway_abort_handler(BACNET_ADDRESS *src,
                                  uint8_t invoke_id,
                                  uint8_t reason,
                                  bool server)
{
    (void)src;
    (void)reason;
    (void)server;

    if (g_request.active && invoke_id == g_request.invoke_id) {
        fprintf(stderr, "BACnet Abort invoke=%u\n", invoke_id);
        request_clear();
    }
}

static void gateway_reject_handler(BACNET_ADDRESS *src,
                                   uint8_t invoke_id,
                                   uint8_t reason)
{
    (void)src;
    (void)reason;

    if (g_request.active && invoke_id == g_request.invoke_id) {
        fprintf(stderr, "BACnet Reject invoke=%u\n", invoke_id);
        request_clear();
    }
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
        point->metadata_complete = true;
        mqtt_publish_config(device, point);
        return false;
    }

    point->metadata_complete = true;
    return false;
}

static bool schedule_poll(DEVICE_STATE *device,
                          POINT_STATE *point,
                          uint64_t now)
{
    if (!point->metadata_complete || point->next_poll_ms > now)
        return false;

    return send_read_property(REQ_POINT_PRESENT_VALUE,
                              device,
                              point,
                              point->object_type,
                              point->object_instance,
                              PROP_PRESENT_VALUE,
                              BACNET_ARRAY_ALL,
                              0);
}

static void scheduler_run(void)
{
    size_t i;
    size_t j;
    uint64_t now = monotonic_ms();

    if (g_request.active) {
        if (now - g_request.sent_ms > g_rp_timeout_ms) {
            fprintf(stderr,
                    "BACnet timeout invoke=%u kind=%d\n",
                    g_request.invoke_id,
                    (int)g_request.kind);

            if (g_request.kind == REQ_POINT_UNITS && g_request.point) {
                g_request.point->unit[0] = '\0';
                g_request.point->metadata_step = 3;
                g_request.point->metadata_complete = true;
                mqtt_publish_config(g_request.device, g_request.point);
            } else if (g_request.kind == REQ_POINT_DESCRIPTION && g_request.point) {
                g_request.point->description[0] = '\0';
                g_request.point->metadata_step = 2;
            }

            request_clear();
        }
        return;
    }

    for (i = 0; i < MAX_DEVICES; i++) {
        if (schedule_device_work(&g_devices[i]))
            return;
    }

    for (i = 0; i < MAX_DEVICES; i++) {
        DEVICE_STATE *device = &g_devices[i];
        if (!device->used || !device->object_list_complete)
            continue;

        for (j = 0; j < device->point_count; j++) {
            if (!device->points[j].metadata_complete &&
                schedule_metadata(device, &device->points[j]))
                return;
        }
    }

    for (i = 0; i < MAX_DEVICES; i++) {
        DEVICE_STATE *device = &g_devices[i];
        if (!device->used || !device->object_list_complete)
            continue;

        for (j = 0; j < device->point_count; j++) {
            if (schedule_poll(device, &device->points[j], now))
                return;
        }
    }
}

int bacnet_client_init(const char *interface_name)
{
    /* bacnet-stack's BIP_Port (ports/linux/bip-init.c) has no static
     * initializer, so it defaults to 0. Apps normally get 0xBAC0 via
     * dlenv_init()'s BACNET_IP_PORT fallback; we call datalink_init()
     * directly, so without this bip_init() binds both its unicast and
     * broadcast sockets to OS-assigned ephemeral ports instead of 47808 -
     * confirmed on-device (two random ports bound, nothing on 0xBAC0). */
    bip_set_port(0xBAC0);

    /* debug_print_bip()/BVLC logging is gated behind these at runtime even
     * when compiled with PRINT_ENABLED; without them nothing shows up in
     * the log regardless of build flags. */
    bip_debug_enable();
    bvlc_debug_enable();

    address_init();

    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_I_AM,
                                 gateway_i_am_handler);
    apdu_set_confirmed_ack_handler(SERVICE_CONFIRMED_READ_PROPERTY,
                                   gateway_read_property_ack_handler);
    apdu_set_abort_handler(gateway_abort_handler);
    apdu_set_reject_handler(gateway_reject_handler);

    if (!datalink_init((char *)interface_name))
        return -1;

    g_next_discovery_ms = 0;
    return 0;
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

    scheduler_run();
    tsm_timer_milliseconds(20);
}

void bacnet_client_cleanup(void)
{
    datalink_cleanup();
}
