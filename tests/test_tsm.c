#define BACNET_SCHEDULER_TEST
#include <assert.h>
#include <stdio.h>
#include "../src/bacnet_client.c"
#include "../src/device_table.c"
static uint64_t clock_ms=100000;
static bool failed=false,active=false;
static unsigned freed=0,published=0;
unsigned g_poll_ms=5000,g_rp_timeout_ms=3000,g_discovery_ms=10000;
uint64_t monotonic_ms(void){return clock_ms;}
time_t unix_time_now(void){return 1789000000+clock_ms/1000;}
void safe_copy(char*d,size_t n,const char*s){snprintf(d,n,"%s",s?s:"");}
void tag_registry_set_state_text(DEVICE_STATE*d,POINT_STATE*p,uint32_t v,const char*t){(void)d;(void)p;(void)v;(void)t;}
void mqtt_publish_config(DEVICE_STATE*d,POINT_STATE*p){(void)d;(void)p;}
void mqtt_publish_live_if_needed(DEVICE_STATE*d,POINT_STATE*p){(void)d;(void)p;published++;}
bool address_get_by_device(uint32_t id,unsigned*m,BACNET_ADDRESS*a){(void)id;*m=1460;memset(a,0,sizeof(*a));return true;}
bool bacnet_address_same(BACNET_ADDRESS*a,BACNET_ADDRESS*b){return memcmp(a,b,sizeof(*a))==0;}
int bacapp_decode_application_data(uint8_t*b,uint32_t n,BACNET_APPLICATION_DATA_VALUE*v){if(n!=5||b[0]!=0x44)return -1;v->tag=BACNET_APPLICATION_TAG_REAL;v->type.Real=21.5;return 5;}
size_t characterstring_length(BACNET_CHARACTER_STRING*s){return s->length;}
char *characterstring_value(BACNET_CHARACTER_STRING*s){return s->value;}
static DEVICE_STATE *device(unsigned id,unsigned points){DEVICE_STATE*d=get_or_create_device(id);d->have_name=d->have_object_count=d->object_list_complete=true;d->state=1;d->rpm_limit=1;for(unsigned i=0;i<points;i++){POINT_STATE*p=add_point(d,OBJECT_ANALOG_INPUT,i);p->metadata_complete=true;}return d;}
#include "bacnet/basic/tsm/tsm.c"
static unsigned retried;
uint16_t apdu_timeout(void){return 3000;}
uint8_t apdu_retries(void){return 1;}
void npdu_copy_data(BACNET_NPDU_DATA*d,BACNET_NPDU_DATA*s){*d=*s;}
void bacnet_address_copy(BACNET_ADDRESS*d,BACNET_ADDRESS*s){*d=*s;}
int bip_send_pdu(BACNET_ADDRESS*d,BACNET_NPDU_DATA*n,uint8_t*p,unsigned len){(void)d;(void)n;(void)p;retried++;return len;}
static uint8_t transaction(void){uint8_t id=tsm_next_free_invokeID(),data[3]={0};BACNET_ADDRESS a={0};BACNET_NPDU_DATA n={0};tsm_set_confirmed_unsegmented_transaction(id,&a,&n,data,3);return id;}
uint8_t Send_Read_Property_Request(uint32_t d,BACNET_OBJECT_TYPE t,uint32_t i,BACNET_PROPERTY_ID p,uint32_t a){(void)d;(void)t;(void)i;(void)p;(void)a;return transaction();}
uint8_t Send_Read_Property_Multiple_Request(uint8_t*p,size_t n,uint32_t d,BACNET_READ_ACCESS_DATA*a){(void)p;(void)n;(void)d;(void)a;return transaction();}
int main(void){DEVICE_STATE*a=device(100,2),*b=device(200,2);unsigned capacity=tsm_transaction_idle_count();scheduler_run();assert(g_request.device==a);uint8_t id=g_request.invoke_id;
 clock_ms+=3000;tsm_timer_milliseconds(3000);scheduler_run();assert(g_request.active&&retried==1&&!tsm_invoke_id_failed(id));
 clock_ms+=3000;tsm_timer_milliseconds(3000);scheduler_run();assert(!g_request.active&&tsm_invoke_id_free(id)&&tsm_transaction_idle_count()==capacity);
 scheduler_run();assert(g_request.device==b);request_clear();assert(tsm_transaction_idle_count()==capacity);
 device_table_cleanup();puts("PASS: actual BACnet-stack TSM retries after 3s, fails after 6s, releases transaction, advances to healthy device");}
