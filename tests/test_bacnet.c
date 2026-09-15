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
void mqtt_publish_config(DEVICE_STATE*d,POINT_STATE*p){(void)d;(void)p;}
void mqtt_publish_live_if_needed(DEVICE_STATE*d,POINT_STATE*p){(void)d;(void)p;published++;}
void tsm_free_invoke_id(uint8_t id){(void)id;active=false;failed=false;freed++;}
bool tsm_invoke_id_free(uint8_t id){(void)id;return !active;}
bool tsm_invoke_id_failed(uint8_t id){(void)id;return failed;}
bool address_get_by_device(uint32_t id,unsigned*m,BACNET_ADDRESS*a){(void)id;*m=1460;memset(a,0,sizeof(*a));return true;}
bool bacnet_address_same(BACNET_ADDRESS*a,BACNET_ADDRESS*b){return memcmp(a,b,sizeof(*a))==0;}
uint8_t Send_Read_Property_Request(uint32_t d,BACNET_OBJECT_TYPE t,uint32_t i,BACNET_PROPERTY_ID p,uint32_t a){(void)d;(void)t;(void)i;(void)p;(void)a;active=true;return 7;}
uint8_t Send_Read_Property_Multiple_Request(uint8_t*p,size_t n,uint32_t d,BACNET_READ_ACCESS_DATA*a){(void)p;(void)n;(void)d;(void)a;active=true;return 8;}
int bacapp_decode_application_data(uint8_t*b,uint32_t n,BACNET_APPLICATION_DATA_VALUE*v){if(n!=5||b[0]!=0x44)return -1;v->tag=BACNET_APPLICATION_TAG_REAL;v->type.Real=21.5;return 5;}
size_t characterstring_length(BACNET_CHARACTER_STRING*s){return s->length;}
char *characterstring_value(BACNET_CHARACTER_STRING*s){return s->value;}
static DEVICE_STATE *device(unsigned id,unsigned points){DEVICE_STATE*d=get_or_create_device(id);d->have_name=d->have_object_count=d->object_list_complete=true;d->state=1;d->rpm_limit=1;for(unsigned i=0;i<points;i++){POINT_STATE*p=add_point(d,OBJECT_ANALOG_INPUT,i);p->metadata_complete=true;}return d;}
int main(void){
 DEVICE_STATE*a=device(100,3),*b=device(200,3);
 scheduler_run();assert(g_request.device==a);failed=true;scheduler_run();assert(!g_request.active&&freed==1);assert(a->points[0].retry_after_ms>clock_ms);
 scheduler_run();assert(g_request.device==b);request_clear();
 device_timed_out(a);device_timed_out(a);assert(a->state==2&&a->retry_after_ms==clock_ms+10000);
 clock_ms+=10000;device_timed_out(a);assert(a->retry_after_ms==clock_ms+30000);
 clock_ms+=30000;device_timed_out(a);assert(a->retry_after_ms==clock_ms+60000);
 device_answered(a);assert(a->state==1&&a->failures==0&&a->retry_after_ms==0);
 device_table_cleanup();a=device(100,40);b=device(200,40);a->rpm_limit=b->rpm_limit=30;device_cursor=0;
 scheduler_run();assert(g_request.device==a&&g_request.batch_count==30);request_clear();scheduler_run();assert(g_request.device==b&&g_request.batch_count==30);request_clear();
 for(unsigned i=0;i<40;i++)a->points[i].next_poll_ms=clock_ms+5000;
 a->points[0].next_poll_ms=a->points[1].next_poll_ms=0;a->point_cursor=0;assert(schedule_values(a,clock_ms));assert(g_request.batch_count==2);
 uint8_t ack[]={0x0c,0,0,0,0,0x1e,0x29,85,0x4e,0x44,0x41,0xac,0,0,0x4f,0x1f,0x0c,0,0,0,1,0x1e,0x29,85,0x5e,0x91,2,0x91,32,0x5f,0x1f};
 BACNET_ADDRESS src={0};BACNET_CONFIRMED_SERVICE_ACK_DATA svc={0};svc.invoke_id=8;
 gateway_rpm_ack(ack,sizeof(ack),&src,&svc);assert(!g_request.active&&published==1&&a->state==1);assert(a->points[1].retry_after_ms>clock_ms);
 for(unsigned n=1;n<sizeof(ack);n++){a->points[0].next_poll_ms=a->points[1].next_poll_ms=0;a->points[0].retry_after_ms=a->points[1].retry_after_ms=0;a->point_cursor=0;a->rpm_limit=30;assert(schedule_values(a,clock_ms));gateway_rpm_ack(ack,n,&src,&svc);assert(!g_request.active);}
 assert(object_type_has_present_value(OBJECT_SCHEDULE));assert(object_type_has_present_value(OBJECT_CALENDAR));assert(object_type_has_present_value(OBJECT_CHARACTERSTRING_VALUE));assert(!object_type_has_present_value(OBJECT_DEVICE));
 device_table_cleanup();a=device(300,30);a->rpm_limit=30;assert(schedule_values(a,clock_ms));
 gateway_reject_handler(&src,g_request.invoke_id,REJECT_REASON_UNRECOGNIZED_SERVICE);
 assert(!g_request.active && a->rpm_limit==1 && a->state==1);
 clock_ms+=10000;assert(schedule_values(a,clock_ms));assert(g_request.kind==REQ_POINT_PRESENT_VALUE);
 request_clear();device_table_cleanup();puts("PASS: fair rotation, point delay, offline backoff/recovery, bounded RPM, partial errors, truncated ACK cleanup, allowed types");
}

