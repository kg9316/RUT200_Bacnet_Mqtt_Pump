#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define LIBMOSQUITTO_STATIC
#include "../src/mqtt_client.c"

DEVICE_STATE g_devices[MAX_DEVICES];
struct mosquitto { int unused; };
static uint64_t clock_ms;
static const char *test_token;
static unsigned test_generation;
static int tls_failure, tls_calls, connect_calls, destroy_calls;
static bool check_hostname;
static char used_id[160], used_user[128], used_password[128];
static void (*connected_cb)(struct mosquitto *, void *, int);
uint64_t monotonic_ms(void) { return clock_ms; }
time_t unix_time_now(void) { return 1788180633; }
void safe_copy(char *d, size_t n, const char *s) { snprintf(d, n, "%s", s ? s : ""); }
void topic_sanitize(char *s) { (void)s; }
bool double_changed(double a, double b) { return a != b; }
int gk_cloud_init(void) { return 0; }
void gk_cloud_reset(void) { test_token = NULL; }
void gk_cloud_cleanup(void) {}
void gk_cloud_loop(void) {}
void gk_cloud_invalidate(void) { test_token = NULL; }
const char *gk_cloud_token(void) { return test_token; }
unsigned gk_cloud_token_generation(void) { return test_generation; }
char *gk_cloud_message(const DEVICE_STATE *d, const POINT_STATE *p, char *t, size_t n) { (void)d; (void)p; (void)t; (void)n; return NULL; }
int tag_registry_init(void) { return 0; }
void tag_registry_cleanup(void) {}
int mosquitto_lib_init(void) { return 0; }
int mosquitto_lib_cleanup(void) { return 0; }
struct mosquitto *mosquitto_new(const char *id, bool clean, void *data)
{ (void)clean; (void)data; safe_copy(used_id, sizeof(used_id), id); return calloc(1,sizeof(struct mosquitto)); }
void mosquitto_destroy(struct mosquitto *m) { destroy_calls++; free(m); }
int mosquitto_disconnect(struct mosquitto *m) { (void)m; return 0; }
void mosquitto_connect_callback_set(struct mosquitto *m, void (*cb)(struct mosquitto *,void *,int)) { (void)m; connected_cb=cb; }
void mosquitto_disconnect_callback_set(struct mosquitto *m, void (*cb)(struct mosquitto *,void *,int)) { (void)m; (void)cb; }
int mosquitto_tls_set(struct mosquitto *m,const char *ca,const char *path,const char *cert,const char *key,int (*cb)(char *,int,int,void *))
{ (void)m; (void)path; (void)cert; (void)key; (void)cb; assert(ca && ca[0]); tls_calls++; return tls_failure; }
int mosquitto_tls_opts_set(struct mosquitto *m,int verify,const char *version,const char *ciphers)
{ (void)m; (void)ciphers; assert(verify == 1 && strcmp(version,"tlsv1.2") == 0); return 0; }
int mosquitto_tls_insecure_set(struct mosquitto *m,bool insecure) { (void)m; check_hostname = !insecure; return 0; }
int mosquitto_username_pw_set(struct mosquitto *m,const char *user,const char *password)
{ (void)m; safe_copy(used_user,sizeof(used_user),user); safe_copy(used_password,sizeof(used_password),password); return 0; }
int mosquitto_connect_async(struct mosquitto *m,const char *host,int port,int keepalive)
{ (void)m; (void)host; (void)keepalive; assert(port>0); connect_calls++; return 0; }
int mosquitto_loop(struct mosquitto *m,int timeout,int packets) { (void)timeout; (void)packets; connected_cb(m,NULL,0); return 0; }
const char *mosquitto_strerror(int rc) { (void)rc; return "test error"; }
int mosquitto_publish(struct mosquitto *m,int *mid,const char *topic,int length,const void *payload,int qos,bool retain)
{ (void)m; (void)mid; (void)topic; (void)length; (void)payload; (void)qos; (void)retain; return 0; }

int main(void)
{
    g_mqtt_settings.gk_cloud = true;
    g_mqtt_settings.tls = true;
    safe_copy(g_mqtt_settings.controller_id,sizeof(g_mqtt_settings.controller_id),"controller-test");
    assert(mqtt_client_init() == 0);
    mqtt_client_loop();
    assert(connect_calls == 0); /* never connect without a token */
    test_token = "first-token";
    test_generation = 1;
    mqtt_client_loop();
    assert(connect_calls == 1 && tls_calls == 1 && check_hostname);
    assert(strcmp(used_id,"c/controller-test/stream/Connector") == 0);
    assert(strcmp(used_user,"controller-test") == 0);
    assert(strcmp(used_password,"first-token") == 0);
    assert(mqtt_client_is_connected());
    test_token = "renewed-token";
    test_generation++;
    mqtt_client_loop();
    assert(connect_calls == 2 && destroy_calls == 1);
    assert(strcmp(used_password,"renewed-token") == 0);
    test_token = NULL;
    mqtt_client_loop();
    assert(!mqtt_client_is_connected() && connect_calls == 2);
    test_token = "valid-token";
    tls_failure = MOSQ_ERR_INVAL;
    clock_ms = 40000;
    mqtt_client_loop();
    assert(!mqtt_client_is_connected() && connect_calls == 2); /* no plaintext fallback */
    tls_failure = 0;
    g_mqtt_settings.gk_cloud = false;
    g_mqtt_settings.tls = false;
    mqtt_client_reconnect();
    mqtt_client_loop();
    assert(mqtt_client_is_connected() && connect_calls == 3);
    mqtt_client_cleanup();
    puts("PASS: verified TLS, GK identity/password, token renewal reconnect, expiry disconnect, no plaintext fallback, generic mode");
    return 0;
}
