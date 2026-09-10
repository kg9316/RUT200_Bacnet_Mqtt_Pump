#include "gk_cloud.h"
#include "mqtt_client.h"
#include "logger.h"
#include "tag_registry.h"

#include <curl/curl.h>
#include <json-c/json.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define TOKEN_URL "https://login.gkcloud.no/v2/token"
#define RESPONSE_SIZE 32768

static CURLM *multi;
static CURL *request;
static char response[RESPONSE_SIZE];
static size_t response_size;
static char token[16384];
static uint64_t expires_ms, refresh_ms, retry_ms, request_started_ms;
static unsigned generation;

static void finish_request(void)
{
    if (request) {
        curl_multi_remove_handle(multi, request);
        curl_easy_cleanup(request);
        request = NULL;
    }
    memset(response, 0, sizeof(response));
    response_size = 0;
}

int gk_cloud_init(void)
{
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        return -1;
    multi = curl_multi_init();
    return multi ? 0 : -1;
}

void gk_cloud_reset(void)
{
    finish_request();
    memset(token, 0, sizeof(token));
    expires_ms = refresh_ms = retry_ms = 0;
}

void gk_cloud_cleanup(void)
{
    finish_request();
    if (multi) curl_multi_cleanup(multi);
    multi = NULL;
    memset(token, 0, sizeof(token));
    curl_global_cleanup();
}

const char *gk_cloud_token(void)
{
    return token[0] && monotonic_ms() < expires_ms ? token : NULL;
}

unsigned gk_cloud_token_generation(void) { return generation; }

void gk_cloud_invalidate(void)
{
    memset(token, 0, sizeof(token));
    expires_ms = refresh_ms = 0;
    /* Keep retry_ms: rejected credentials must not hammer the token service. */
}

static size_t receive_token(char *data, size_t size, size_t count, void *unused)
{
    size_t n;
    (void)unused;
    if (size && count > (sizeof(response) - 1 - response_size) / size)
        return 0;
    n = size * count;
    memcpy(response + response_size, data, n);
    response_size += n;
    response[response_size] = 0;
    return n;
}

static int start_request(void)
{
    char client_id[160];
    char *id = NULL, *secret = NULL, *body = NULL;
    int result = -1;
    request = curl_easy_init();
    if (!request) return -1;
    snprintf(client_id, sizeof(client_id), "controller:%s", g_mqtt_settings.controller_id);
    id = curl_easy_escape(request, client_id, 0);
    secret = curl_easy_escape(request, g_mqtt_settings.license, 0);
    if (!id || !secret) goto done;
    body = malloc(strlen(id) + strlen(secret) + 80);
    if (!body) goto done;
    sprintf(body, "grant_type=client_credentials&client_id=%s&client_secret=%s", id, secret);
#define SETOPT(option, value) do { if (curl_easy_setopt(request, option, value) != CURLE_OK) goto done; } while (0)
    SETOPT(CURLOPT_URL, TOKEN_URL);
    SETOPT(CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
    SETOPT(CURLOPT_FOLLOWLOCATION, 0L);
    SETOPT(CURLOPT_SSL_VERIFYPEER, 1L);
    SETOPT(CURLOPT_SSL_VERIFYHOST, 2L);
    SETOPT(CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);
    SETOPT(CURLOPT_CAINFO, g_mqtt_settings.ca_file[0] ? g_mqtt_settings.ca_file : MQTT_CA_BUNDLE);
    SETOPT(CURLOPT_CONNECTTIMEOUT, 10L);
    SETOPT(CURLOPT_TIMEOUT, 20L);
    SETOPT(CURLOPT_NOSIGNAL, 1L);
    SETOPT(CURLOPT_WRITEFUNCTION, receive_token);
    /* libcurl copies the form body; no secrets in shell arguments or logs. */
    SETOPT(CURLOPT_COPYPOSTFIELDS, body);
#undef SETOPT
    if (curl_multi_add_handle(multi, request) == CURLM_OK) result = 0;
done:
    if (body) { memset(body, 0, strlen(body)); free(body); }
    curl_free(id);
    if (secret) { memset(secret, 0, strlen(secret)); curl_free(secret); }
    if (result) { curl_easy_cleanup(request); request = NULL; }
    return result;
}

static bool accept_token(void)
{
    struct json_object *root = json_tokener_parse(response), *access, *expires, *type;
    bool ok = false;
    if (root && json_object_object_get_ex(root, "access_token", &access) &&
        json_object_is_type(access, json_type_string) &&
        json_object_object_get_ex(root, "expires_in", &expires) &&
        json_object_is_type(expires, json_type_int) &&
        json_object_object_get_ex(root, "token_type", &type) &&
        json_object_is_type(type, json_type_string) &&
        strcasecmp(json_object_get_string(type), "Bearer") == 0) {
        const char *value = json_object_get_string(access);
        int64_t seconds = json_object_get_int64(expires);
        size_t length = (size_t)json_object_get_string_len(access);
        if (length > 0 && length < sizeof(token) && strlen(value) == length &&
            seconds > 0 && seconds <= 86400 * 365) {
            uint64_t lifetime = (uint64_t)seconds * 1000;
            uint64_t margin = lifetime / 10 < 60000 ? lifetime / 10 : 60000;
            expires_ms = request_started_ms + lifetime;
            refresh_ms = expires_ms - margin;
            if (monotonic_ms() < refresh_ms) {
                safe_copy(token, sizeof(token), value);
                ++generation;
                ok = true;
            }
        }
    }
    if (root) json_object_put(root);
    return ok;
}

void gk_cloud_loop(void)
{
    int active, remaining;
    CURLMsg *message;
    uint64_t now = monotonic_ms();
    if (!multi || !g_mqtt_settings.gk_cloud) return;
    if (!request && now >= refresh_ms && now >= retry_ms) {
        retry_ms = now + 30000;
        if (!g_mqtt_settings.controller_id[0] || !g_mqtt_settings.license[0]) return;
        request_started_ms = now;
        if (start_request()) LOG_ERRORF("GK token request could not be started");
    }
    if (!request) return;
    if (curl_multi_perform(multi, &active) != CURLM_OK) {
        finish_request();
        retry_ms = now + 30000;
        return;
    }
    while ((message = curl_multi_info_read(multi, &remaining))) {
        if (message->msg == CURLMSG_DONE && message->easy_handle == request) {
            long status = 0;
            curl_easy_getinfo(request, CURLINFO_RESPONSE_CODE, &status);
            if (message->data.result == CURLE_OK && status == 200 && accept_token())
                LOG_INFOF("GK access token renewed");
            else
                LOG_WARNF("GK token request failed (HTTP %ld, transport %d)", status, message->data.result);
            finish_request();
            retry_ms = now + 30000;
            break;
        }
    }
}

char *gk_cloud_message(const DEVICE_STATE *device, const POINT_STATE *point,
                       char *topic, size_t topic_size)
{
    struct json_object *payload;
    const char *tag_id, *data_type;
    char *result;
    int n;
    if (!point->have_value) return NULL;
    tag_id = tag_registry_get(device->device_id, (unsigned)point->object_type, point->object_instance);
    if (!tag_id) return NULL;
    n = snprintf(topic, topic_size, "c/%s/t/%s", g_mqtt_settings.controller_id, tag_id);
    if (n < 0 || (size_t)n >= topic_size) return NULL;
    switch (point->value_kind) {
        case VALUE_NUMBER: data_type = "number"; break;
        case VALUE_ENUM: data_type = "enum"; break;
        case VALUE_BOOL: data_type = "digital"; break;
        case VALUE_STRING: data_type = "string"; break;
        default: return NULL;
    }
    if ((point->value_kind == VALUE_NUMBER || point->value_kind == VALUE_ENUM) && !isfinite(point->numeric_value)) return NULL;
    payload = json_object_new_object();
    if (!payload) return NULL;
    json_object_object_add(payload, "timestamp", json_object_new_int64((int64_t)unix_time_ms()));
    json_object_object_add(payload, "dataType", json_object_new_string(data_type));
    if (point->value_kind == VALUE_STRING)
        json_object_object_add(payload, "valueText", json_object_new_string(point->string_value));
    else if (point->value_kind == VALUE_BOOL)
        json_object_object_add(payload, "value", json_object_new_boolean(point->bool_value));
    else if (point->value_kind == VALUE_ENUM)
        json_object_object_add(payload, "value", json_object_new_int64((int64_t)point->numeric_value));
    else
        json_object_object_add(payload, "value", json_object_new_double(point->numeric_value));
    /* Metadata stays on the gateway. A future REST service can associate it
     * with this GUID separately; it must not leak through MQTT. */
    result = strdup(json_object_to_json_string_ext(payload, JSON_C_TO_STRING_PLAIN));
    json_object_put(payload);
    return result;
}
