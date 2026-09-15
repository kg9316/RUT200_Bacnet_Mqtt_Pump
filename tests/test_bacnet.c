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
const char *tag_registry_get(uint32_t d,unsigned t,uint32_t i){(void)d;(void)t;(void)i;return "test";}
void tag_registry_set_metadata(uint32_t d,unsigned t,uint32_t i,const char*n,const char*u,const char*s){(void)d;(void)t;(void)i;(void)n;(void)u;(void)s;}
void tag_registry_set_state_text(DEVICE_STATE*d,POINT_STATE*p,uint32_t v,const char*t){(void)d;(void)p;(void)v;(void)t;}
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
 request_clear();
 assert(a->rpm_unsupported);
 bacnet_client_set_rpm_max(50);assert(a->rpm_limit==1);
 b=device(400,80);b->rpm_limit=1;b->rpm_confirmed=true;b->max_apdu=1460;
 bacnet_client_set_rpm_max(60);assert(b->rpm_limit==0 && a->rpm_limit==1);
 assert(schedule_values(b,clock_ms));assert(g_request.batch_count==60);request_clear();
 bacnet_client_set_rpm_max(100);b->point_cursor=0;
 assert(schedule_values(b,clock_ms));assert(g_request.batch_count<=73 && g_request.batch_count>60);request_clear();
 bacnet_client_set_rpm_max(5);assert(schedule_values(b,clock_ms));assert(g_request.batch_count==5);request_clear();
 bacnet_client_set_rpm_max(1);assert(schedule_values(b,clock_ms));assert(g_request.kind==REQ_POINT_PRESENT_VALUE && b->last_poll_count==1 && b->rpm_confirmed);request_clear();
 bacnet_client_set_rpm_max(0);bacnet_client_set_rpm_max(101);assert(g_rpm_batch_max==1);
 device_table_cleanup();
 a=device(500,0);POINT_STATE *p=add_point(a,OBJECT_BINARY_VALUE,1);p->metadata_step=3;
 assert(schedule_metadata(a,p) && g_request.kind==REQ_POINT_INACTIVE_TEXT);request_failed(false);
 assert(p->metadata_step==4 && !p->metadata_complete);
 assert(schedule_metadata(a,p) && g_request.kind==REQ_POINT_ACTIVE_TEXT);request_failed(false);assert(p->metadata_complete);
 p=add_point(a,OBJECT_MULTI_STATE_VALUE,2);p->metadata_step=3;
 assert(schedule_metadata(a,p) && g_request.kind==REQ_POINT_STATE_COUNT);request_failed(false);assert(p->metadata_complete);
 p->metadata_complete=false;p->metadata_step=4;p->state_text_count=3;p->state_text_index=1;
 assert(schedule_metadata(a,p) && g_request.kind==REQ_POINT_STATE_TEXT);request_failed(false);assert(p->state_text_index==2 && !p->metadata_complete);
 p->next_poll_ms=0;assert(schedule_poll(a,p,clock_ms));request_clear();
 state_text_advance(p,REQ_POINT_STATE_TEXT);assert(p->state_text_index==3);
 state_text_advance(p,REQ_POINT_STATE_TEXT);assert(p->metadata_complete);
 device_table_cleanup();
 next_recovery_ms=0;device_cursor=0;
 a=device(700,1); b=device(701,1); DEVICE_STATE*c=device(702,1);
 a->state=b->state=2;a->failures=b->failures=3;
 a->last_iam_ms=b->last_iam_ms=clock_ms;
 scheduler_run();assert(g_request.device==a && g_request.kind==REQ_DEVICE_NAME);
 clock_ms+=g_rp_timeout_ms;scheduler_run();assert(!g_request.active);
 scheduler_run();assert(g_request.device==c);request_clear();
 assert(b->last_response==0); /* second offline device cannot consume another timeout */
 request_clear();next_recovery_ms=0;device_cursor=0;
 a->retry_after_ms=b->retry_after_ms=0;
 a->last_iam_ms=b->last_iam_ms=0;
 scheduler_run();assert(g_request.device==c);request_clear(); /* silent offline devices are skipped */
 a->last_iam_ms=clock_ms;device_cursor=0;
 scheduler_run();assert(g_request.device==a);request_clear(); /* fresh presence permits a budgeted probe */
 device_table_cleanup();puts("PASS: live RPM limits, APDU bounds, state metadata scheduling, optional-property errors and polling during metadata reads");
}

