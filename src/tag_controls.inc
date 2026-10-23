/* Registry-owned controls. All operations execute on the daemon thread. */
#include <ctype.h>

static struct json_object *field(struct json_object *o,const char *key)
{
    struct json_object *v=NULL;
    if (o && json_object_is_type(o,json_type_object)) json_object_object_get_ex(o,key,&v);
    return v;
}
static bool number(struct json_object *v,int64_t lo,int64_t hi)
{
    return v && json_object_is_type(v,json_type_int) && json_object_get_int64(v)>=lo && json_object_get_int64(v)<=hi;
}
static bool text_limit(struct json_object *v,size_t max)
{
    return v && json_object_is_type(v,json_type_string) && (size_t)json_object_get_string_len(v)<=max &&
        strlen(json_object_get_string(v))==(size_t)json_object_get_string_len(v);
}
static bool manual_address(struct json_object *row,BACNET_ADDRESS *a)
{
    unsigned b[4]; char tail;
    struct json_object *ip=field(row,"ip"),*port=field(row,"p");
    if (!text_limit(ip,15) || !number(port,1,65535) ||
        sscanf(json_object_get_string(ip),"%u.%u.%u.%u%c",&b[0],&b[1],&b[2],&b[3],&tail)!=4 ||
        b[0]==0 || b[0]>=224 || b[0]==127 || b[1]>255 || b[2]>255 || b[3]>255) return false;
    memset(a,0,sizeof(*a)); a->mac_len=6;
    for(unsigned i=0;i<4;i++) a->mac[i]=(uint8_t)b[i];
    unsigned p=(unsigned)json_object_get_int(port); a->mac[4]=p>>8;a->mac[5]=p&255;
    return true;
}
static bool valid_controls(struct json_object *cfg)
{
    if (!json_object_is_type(cfg,json_type_object)) return false;
    struct json_object *manual=field(cfg,"m"),*defaults=field(cfg,"n");
    if (manual) {
        if (!json_object_is_type(manual,json_type_array) || json_object_array_length(manual)>MAX_DEVICES) return false;
        for(size_t i=0;i<json_object_array_length(manual);i++) {
            struct json_object *r=json_object_array_get_idx(manual,i); BACNET_ADDRESS a;
            if (!manual_address(r,&a) || (field(r,"id") && !number(field(r,"id"),0,4194303)) ||
                (field(r,"n") && !text_limit(field(r,"n"),NAME_LEN-1))) return false;
            for(size_t j=0;j<i;j++) {
                struct json_object *other=json_object_array_get_idx(manual,j); BACNET_ADDRESS b;
                manual_address(other,&b);
                if (!memcmp(a.mac,b.mac,6) || (field(r,"id") && field(other,"id") &&
                    json_object_get_int(field(r,"id"))==json_object_get_int(field(other,"id")))) return false;
            }
        }
    }
    if (defaults) {
        if (!json_object_is_type(defaults,json_type_object) || json_object_object_length(defaults)>MAX_DEVICES) return false;
        json_object_object_foreach(defaults,key,v) {
            unsigned id;char tail;
            if(sscanf(key,"%u%c",&id,&tail)!=1 || id>4194303 || !json_object_is_type(v,json_type_boolean)) return false;
        }
    }
    return true;
}
static struct json_object *controls(void)
{
    struct json_object *cfg=field(registry,"_cfg");
    if (!cfg && registry) {cfg=json_object_new_object();json_object_object_add(registry,"_cfg",cfg);}
    return cfg;
}
bool tag_registry_new_points_enabled(uint32_t device)
{
    char key[16];snprintf(key,sizeof(key),"%u",device);
    struct json_object *v=field(field(field(registry,"_cfg"),"n"),key);
    return !v || json_object_get_boolean(v);
}
void tag_registry_init_point(DEVICE_STATE *d,POINT_STATE *p)
{
    if (tag_registry_lookup(d->device_id,p->object_type,p->object_instance)) return;
    if (!tag_registry_get(d->device_id,p->object_type,p->object_instance)) return;
    p->poll_disabled=!tag_registry_new_points_enabled(d->device_id);
    if(p->poll_disabled) {
        tag_registry_set_metadata(d->device_id,p->object_type,p->object_instance,p->name,p->unit,p->description);
        char key[80];snprintf(key,sizeof(key),"%u:%u:%u",d->device_id,(unsigned)p->object_type,p->object_instance);
        json_object_object_add(field(registry,key),"x",json_object_new_boolean(true));metadata_dirty=true;
    }
}
bool tag_registry_manual_target(size_t index,BACNET_ADDRESS *a,int32_t *id)
{
    struct json_object *m=field(field(registry,"_cfg"),"m");
    if(!m || index>=json_object_array_length(m)) return false;
    struct json_object *row=json_object_array_get_idx(m,index),*v=field(row,"id");
    *id=v?json_object_get_int(v):-1;
    return manual_address(row,a);
}
static void refresh_manual_flags(void)
{
    for(size_t i=0;i<MAX_DEVICES;i++)g_devices[i].manual_endpoint=false;
    struct json_object *m=field(field(registry,"_cfg"),"m");
    for(size_t i=0;m && i<json_object_array_length(m);i++) {
        struct json_object *row=json_object_array_get_idx(m,i),*id=field(row,"id");
        if(!id)continue;
        DEVICE_STATE *d=get_or_create_device((uint32_t)json_object_get_int(id));
        if(d) {
            d->manual_endpoint=true;
            if(!d->have_name && field(row,"n"))safe_copy(d->name,sizeof(d->name),json_object_get_string(field(row,"n")));
        }
    }
}
bool tag_registry_is_manual(uint32_t device)
{
    DEVICE_STATE *d=find_device(device);return d && d->manual_endpoint;
}
void tag_registry_manual_seen(uint32_t device,const BACNET_ADDRESS *a)
{
    if(a->net || a->mac_len!=6) return; /* routed devices need their own network address */
    struct json_object *m=field(field(registry,"_cfg"),"m");
    for(size_t i=0;m && i<json_object_array_length(m);i++) {
        struct json_object *row=json_object_array_get_idx(m,i); BACNET_ADDRESS target;
        if(!manual_address(row,&target) || memcmp(a->mac,target.mac,6))continue;
        struct json_object *id=field(row,"id");
        if(id && (uint32_t)json_object_get_int(id)!=device) continue;
        if(!id) {
            /* One device identity must not be associated with two endpoints. */
            if(tag_registry_is_manual(device))continue;
            json_object_object_add(row,"id",json_object_new_int64(device));metadata_dirty=true;
        }
        DEVICE_STATE *d=get_or_create_device(device);if(d)d->manual_endpoint=true;
        if(field(row,"n"))tag_registry_set_device_name(device,json_object_get_string(field(row,"n")));
    }
}
static bool contains_ci(const char *text,const char *query)
{
    if(!*query)return true;
    for(;*text;text++) {
        size_t i=0;while(query[i] && text[i] && tolower((unsigned char)text[i])==tolower((unsigned char)query[i]))++i;
        if(!query[i])return true;
    }
    return false;
}
static bool point_matches(POINT_STATE *p,struct json_object *req)
{
    struct json_object *type=field(req,"type"),*state=field(req,"state");
    if(type && json_object_get_int(type)!=(int)p->object_type)return false;
    if(state && strcmp(json_object_get_string(state),"all")) {
        bool disabled=!strcmp(json_object_get_string(state),"disabled");
        if(disabled!=p->poll_disabled)return false;
    }
    const char *q=field(req,"q")?json_object_get_string(field(req,"q")):"";
    char address[40];snprintf(address,sizeof(address),"%u:%u",(unsigned)p->object_type,p->object_instance);
    return contains_ci(p->name,q)||contains_ci(p->description,q)||contains_ci(address,q);
}
static const char *control_request(struct json_object *req,struct json_object *result)
{
    if(!registry)return "GUID registry unavailable";
    const char *action=json_object_get_string(field(req,"action"));
    if(!action)return "Missing action";
    if(!strcmp(action,"manual_list")) {
        struct json_object *m=field(field(registry,"_cfg"),"m");
        json_object_object_add(result,"manual",m?json_object_get(m):json_object_new_array());return NULL;
    }
    if(!strcmp(action,"manual_save") || !strcmp(action,"manual_remove")) {
        struct json_object *row=field(req,"target");BACNET_ADDRESS target;
        if(!manual_address(row,&target))return "Enter a valid unicast IPv4 address and port";
        if((field(row,"id")&&!number(field(row,"id"),0,4194303)) || (field(row,"n")&&!text_limit(field(row,"n"),NAME_LEN-1)))return "Invalid device ID or name";
        struct json_object *cfg=controls(),*old=field(cfg,"m"),*next=json_object_new_array();
        for(size_t i=0;old && i<json_object_array_length(old);i++) {
            struct json_object *v=json_object_array_get_idx(old,i); BACNET_ADDRESS a;manual_address(v,&a);
            if(memcmp(a.mac,target.mac,6))json_object_array_add(next,json_object_get(v));
        }
        if(!strcmp(action,"manual_save"))json_object_array_add(next,json_object_get(row));
        size_t reserved=device_count();
        for(size_t i=0;i<json_object_array_length(next);i++) {
            struct json_object *id=field(json_object_array_get_idx(next,i),"id");
            if(!id || !find_device((uint32_t)json_object_get_int(id)))++reserved;
        }
        if(reserved>MAX_DEVICES){json_object_put(next);return "Device limit reached (500 including pending manual endpoints)";}
        struct json_object *candidate=json_object_new_object();json_object_object_add(candidate,"m",next);
        if(!valid_controls(candidate)) {json_object_put(candidate);return "Duplicate device ID or too many manual devices";}
        json_object_object_add(cfg,"m",json_object_get(next));json_object_put(candidate);
        metadata_dirty=true;
        if(!persist()) {tag_registry_cleanup();return "Unable to save device settings; restart after checking storage";}
        refresh_manual_flags();
        return NULL;
    }
    unsigned id;
    if(!number(field(req,"device"),0,4194303))return "Invalid device ID";
    id=(unsigned)json_object_get_int(field(req,"device"));
    DEVICE_STATE *d=find_device(id);
    if(!d)return "Device not found";
    if(!strcmp(action,"new_points")) {
        struct json_object *enabled=field(req,"enabled");
        if(!json_object_is_type(enabled,json_type_boolean))return "Invalid enabled value";
        struct json_object *cfg=controls(),*defaults=field(cfg,"n");
        if(!defaults) {defaults=json_object_new_object();json_object_object_add(cfg,"n",defaults);}
        char key[16];snprintf(key,sizeof(key),"%u",id);json_object_object_add(defaults,key,json_object_get(enabled));
        metadata_dirty=true;
        if(!persist()){tag_registry_cleanup();return "Unable to save settings";}return NULL;
    }
    bool edit=!strcmp(action,"point_set");
    if(!edit && strcmp(action,"point_list"))return "Unknown action";
    if((field(req,"type")&&!number(field(req,"type"),0,1023)) || (field(req,"q")&&!text_limit(field(req,"q"),192)))return "Invalid filter";
    struct json_object *state=field(req,"state");
    if(state && (!text_limit(state,8) || (strcmp(json_object_get_string(state),"all") && strcmp(json_object_get_string(state),"enabled") && strcmp(json_object_get_string(state),"disabled"))))return "Invalid state filter";
    unsigned page=0;
    if(field(req,"page")) {if(!number(field(req,"page"),0,MAX_TOTAL_POINTS/100))return "Invalid page";page=json_object_get_int(field(req,"page"));}
    struct json_object *keys=field(req,"keys");
    bool all=field(req,"all") && json_object_get_boolean(field(req,"all"));
    if(edit && (!json_object_is_type(field(req,"enabled"),json_type_boolean) ||
        (field(req,"all") && !json_object_is_type(field(req,"all"),json_type_boolean)) ||
        (!all && (!json_object_is_type(keys,json_type_array) || json_object_array_length(keys)>100))))return "Invalid selection";
    if(edit && !all)for(size_t j=0;j<json_object_array_length(keys);j++)if(!text_limit(json_object_array_get_idx(keys,j),40))return "Invalid point key";
    struct json_object *rows=json_object_new_array();unsigned total=0,changed=0;
    for(size_t i=0;i<d->point_count;i++) {
        POINT_STATE *p=&d->points[i];if(!point_matches(p,req))continue;
        char key[80],shortkey[40];snprintf(key,sizeof(key),"%u:%u:%u",id,(unsigned)p->object_type,p->object_instance);
        snprintf(shortkey,sizeof(shortkey),"%u:%u",(unsigned)p->object_type,p->object_instance);
        if(edit) {
            bool selected=all;
            for(size_t j=0;!selected && keys && j<json_object_array_length(keys);j++)selected=!strcmp(shortkey,json_object_get_string(json_object_array_get_idx(keys,j)));
            if(selected) {
                tag_registry_init_point(d,p);
                if(!tag_registry_lookup(id,p->object_type,p->object_instance)){json_object_put(rows);return "Unable to allocate point identity";}
                tag_registry_set_metadata(id,p->object_type,p->object_instance,p->name,p->unit,p->description);
                struct json_object *entry=field(registry,key);
                bool disabled=!json_object_get_boolean(field(req,"enabled"));
                json_object_object_add(entry,"x",json_object_new_boolean(disabled));
                p->poll_disabled=disabled;p->next_poll_ms=0;p->retry_after_ms=0;
                if(!disabled){p->have_published_value=false;p->have_value=false;p->config_published=false;}
                ++changed;metadata_dirty=true;
            }
        } else if(total>=page*100 && total<page*100+100) {
            struct json_object *r=json_object_new_object();
            json_object_object_add(r,"key",json_object_new_string(shortkey));
            json_object_object_add(r,"name",json_object_new_string(p->name));
            json_object_object_add(r,"description",json_object_new_string(p->description));
            json_object_object_add(r,"unit",json_object_new_string(p->unit));
            json_object_object_add(r,"enabled",json_object_new_boolean(!p->poll_disabled));
            json_object_array_add(rows,r);
        }
        ++total;
    }
    d->values_check_ms=0;
    json_object_object_add(result,"rows",rows);json_object_object_add(result,"total",json_object_new_int(total));
    json_object_object_add(result,"changed",json_object_new_int(changed));
    json_object_object_add(result,"newEnabled",json_object_new_boolean(tag_registry_new_points_enabled(id)));
    if(edit && !persist()){tag_registry_cleanup();return "Unable to save point selection; restart after checking storage";}
    return NULL;
}
