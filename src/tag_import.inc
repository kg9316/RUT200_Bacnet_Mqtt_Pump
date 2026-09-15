/* Included by tag_registry.c: imports are applied by the daemon's single thread. */
#include "mqtt_client.h"
#ifndef IMPORT_DIR
#define IMPORT_DIR "/tmp/gk-bacnet-mqtt-import"
#endif
#define IMPORT_LIMIT (2 * 1024 * 1024)

static struct json_object *import_field(struct json_object *o, const char *short_key, const char *long_key)
{
    struct json_object *v=NULL;
    if (!json_object_object_get_ex(o,short_key,&v) && long_key) json_object_object_get_ex(o,long_key,&v);
    return v;
}
static bool import_text(struct json_object *v, size_t max)
{
    return v && json_object_is_type(v,json_type_string) &&
        (size_t)json_object_get_string_len(v)<=max &&
        strlen(json_object_get_string(v))==(size_t)json_object_get_string_len(v);
}
static bool import_number(struct json_object *v, unsigned max, unsigned *out)
{
    if (!v || !json_object_is_type(v,json_type_int)) return false;
    int64_t n=json_object_get_int64(v);
    if (n<0 || n>max) return false;
    *out=(unsigned)n;return true;
}
static const char *import_entry(struct json_object *entries, const char *address, struct json_object *input)
{
    unsigned di,ot,oi;char extra,key[80];
    if (sscanf(address,"%u:%u:%u%c",&di,&ot,&oi,&extra)!=3 || di>4194303 || ot>1023 || oi>4194303)
        return "Invalid BACnet address";
    snprintf(key,sizeof(key),"%u:%u:%u",di,ot,oi);
    struct json_object *old;
    if (json_object_object_get_ex(entries,key,&old)) return "Duplicate BACnet address in file";
    struct json_object *tag=json_object_is_type(input,json_type_string)?input:import_field(input,"t","tag");
    if (!import_text(tag,36) || !valid_uuid(json_object_get_string(tag))) return "Invalid tag GUID";
    struct json_object *entry=json_object_new_object();
    json_object_object_add(entry,"t",json_object_get(tag));
    const char *shorts[]={"n","u","d"}, *longs[]={"name","unit","description"};
    size_t limits[]={NAME_LEN-1,UNIT_LEN-1,DESC_LEN-1};
    for (unsigned i=0;i<3;i++) {
        struct json_object *v=import_field(input,shorts[i],longs[i]);
        if (!v) continue;
        if (!import_text(v,limits[i])) { json_object_put(entry);return "Invalid or oversized point metadata"; }
        json_object_object_add(entry,shorts[i],json_object_get(v));
    }
    struct json_object *states=import_field(input,"s",NULL);
    if (states) {
        if (!json_object_is_type(states,json_type_object)) {json_object_put(entry);return "Invalid state text mapping";}
        json_object_object_foreach(states,state,text) {
            unsigned n;char tail,canonical[16];
            if (sscanf(state,"%u%c",&n,&tail)!=1 || n>65535 || !import_text(text,MAX_CHARACTER_STRING_BYTES)) {
                json_object_put(entry);return "Invalid state label";
            }
            snprintf(canonical,sizeof(canonical),"%u",n);
            if (strcmp(state,canonical)) {json_object_put(entry);return "Invalid state index";}
        }
        json_object_object_add(entry,"s",json_object_get(states));
    }
    json_object_object_add(entries,key,entry);return NULL;
}

static const char *import_document(struct json_object *doc, unsigned *added, unsigned *updated)
{
    const char *error=NULL;
    bool identities_changed=false;
    struct json_object *entries=json_object_new_object(), *candidate=NULL, *seen=NULL, *counts=NULL;
    struct json_object *points=NULL,*names=NULL,*controller=NULL;
    *added=*updated=0;
    if (!registry) {error="GUID registry unavailable";goto done;}
    if (!doc || !json_object_is_type(doc,json_type_object)) {error="Expected a JSON object";goto done;}
    controller=import_field(doc,"controllerId","controller_id");
    if (controller && (!import_text(controller,127) || strcmp(json_object_get_string(controller),g_mqtt_settings.controller_id))) {
        error="Controller ID does not match this gateway";goto done;
    }
    if (json_object_object_get_ex(doc,"points",&points)) {
        if (!json_object_is_type(points,json_type_array)) {error="Invalid points array";goto done;}
        names=import_field(doc,"devices",NULL);
        for (size_t i=0;i<json_object_array_length(points);i++) {
            struct json_object *p=json_object_array_get_idx(points,i);
            unsigned di,ot,oi;char address[80];
            if (!import_number(import_field(p,"di","deviceId"),4194303,&di) ||
                !import_number(import_field(p,"ot","objectType"),1023,&ot) ||
                !import_number(import_field(p,"oi","objectInstance"),4194303,&oi)) {error="Missing or invalid BACnet point address";goto done;}
            snprintf(address,sizeof(address),"%u:%u:%u",di,ot,oi);
            if ((error=import_entry(entries,address,p))) goto done;
        }
    } else {
        names=import_field(doc,"_dn",NULL);
        json_object_object_foreach(doc,key,value) {
            if (!strcmp(key,"_dn")) continue;
            if ((error=import_entry(entries,key,value))) goto done;
        }
    }
    if (!json_object_object_length(entries)) {error="File contains no points";goto done;}
    candidate=json_tokener_parse(json_object_to_json_string_ext(registry,JSON_C_TO_STRING_PLAIN));
    if (!candidate) {error="Not enough memory to import";goto done;}
    { json_object_object_foreach(entries,key,value) {
        struct json_object *old;
        if (json_object_object_get_ex(candidate,key,&old)) {
            if (strcmp(entry_tag(old),entry_tag(value))) identities_changed=true;
            if (json_object_is_type(old,json_type_object)) {
                json_object_object_foreach(old,field,v) {
                    struct json_object *unused;
                    if (!json_object_object_get_ex(value,field,&unused)) json_object_object_add(value,field,json_object_get(v));
                }
            }
            ++*updated;
        } else ++*added;
        json_object_object_add(candidate,key,json_object_get(value));
    }
    }
    if (names) {
        if (!json_object_is_type(names,json_type_object)) {error="Invalid device names";goto done;}
        struct json_object *dest;
        if (!json_object_object_get_ex(candidate,"_dn",&dest)) {
            dest=json_object_new_object();json_object_object_add(candidate,"_dn",dest);
        }
        json_object_object_foreach(names,key,name) {
            unsigned di;char extra,canonical[16];
            if (sscanf(key,"%u%c",&di,&extra)!=1 || di>4194303 || !import_text(name,NAME_LEN-1)) {error="Invalid device name";goto done;}
            snprintf(canonical,sizeof(canonical),"%u",di);
            json_object_object_add(dest,canonical,json_object_get(name));
        }
    }
    seen=json_object_new_object();counts=json_object_new_object();
    json_object_object_foreach(candidate,key,value) {
        if (!strcmp(key,"_dn")) continue;
        const char *tag=entry_tag(value);struct json_object *v;unsigned di,ot,oi;char device[16];
        if (!tag || json_object_object_get_ex(seen,tag,&v)) {error="Duplicate GUID assigned to different points";goto done;}
        json_object_object_add(seen,tag,json_object_new_boolean(true));
        if (sscanf(key,"%u:%u:%u",&di,&ot,&oi)!=3) {error="Invalid existing registry";goto done;}
        snprintf(device,sizeof(device),"%u",di);
        unsigned count=json_object_object_get_ex(counts,device,&v)?json_object_get_int(v):0;
        if (count>=MAX_POINTS_PER_DEVICE) {error="Too many points for one device";goto done;}
        json_object_object_add(counts,device,json_object_new_int(count+1));
    }
    struct json_object *all_names;
    if (json_object_object_get_ex(candidate,"_dn",&all_names)) {
        json_object_object_foreach(all_names,key,value) {
            struct json_object *v;(void)value;
            if (!json_object_object_get_ex(counts,key,&v)) json_object_object_add(counts,key,json_object_new_int(0));
        }
    }
    if (json_object_object_length(seen)>MAX_TOTAL_POINTS) {error="Too many points in total (maximum 10000)";goto done;}
    if (json_object_object_length(counts)>MAX_DEVICES) {error="Too many BACnet devices";goto done;}
    if (strlen(json_object_to_json_string_ext(candidate,JSON_C_TO_STRING_PLAIN))>IMPORT_LIMIT) {error="Merged registry exceeds 2 MB";goto done;}
    struct json_object *previous=registry;
    registry=candidate;candidate=NULL;
    if (!persist()) {
        /* A failed directory sync may follow a successful rename: fail closed. */
        tag_registry_cleanup();json_object_put(previous);
        error="Unable to save import durably; gateway requires registry recovery";goto done;
    }
    json_object_put(previous);
    if (identity_index) json_object_put(identity_index);
    identity_index = json_object_get(seen);
    tag_registry_restore_devices();
    /* Drop queued publications using superseded IDs. The next connection
     * clears COV publication state and messages resolve the restored GUIDs. */
    if (identities_changed) mqtt_client_reconnect();
done:
    if (entries) json_object_put(entries);
    if (candidate) json_object_put(candidate);
    if (seen) json_object_put(seen);
    if (counts) json_object_put(counts);
    return error;
}

void tag_registry_import_if_due(void)
{
    static uint64_t next;
    uint64_t now=monotonic_ms();
    if (now<next) return;
    next=now+1000;
    struct stat st;
    if (stat(IMPORT_DIR "/request.json",&st)!=0) return;
    struct json_object *request=NULL,*result=json_object_new_object(),*id=NULL;
    unsigned added=0,updated=0;
    const char *error="Invalid import request";
    if (st.st_size>0 && st.st_size<=IMPORT_LIMIT+1024) request=json_object_from_file(IMPORT_DIR "/request.json");
    if (request) {
        id=import_field(request,"id",NULL);
        if (import_text(id,64)) error=import_document(import_field(request,"document",NULL),&added,&updated);
    }
    if (id && import_text(id,64)) json_object_object_add(result,"id",json_object_get(id));
    json_object_object_add(result,"ok",json_object_new_boolean(!error));
    json_object_object_add(result,"error",json_object_new_string(error?error:""));
    json_object_object_add(result,"added",json_object_new_int(added));
    json_object_object_add(result,"updated",json_object_new_int(updated));
    if (json_object_to_file_ext(IMPORT_DIR "/result.tmp",result,JSON_C_TO_STRING_PLAIN)==0) {
        chmod(IMPORT_DIR "/result.tmp",0644);
        rename(IMPORT_DIR "/result.tmp",IMPORT_DIR "/result.json");
    }
    unlink(IMPORT_DIR "/request.json");
    rmdir(IMPORT_DIR "/busy");
    LOG_INFOF("GUID import %s: added=%u existing=%u",error?"rejected":"completed",error?0:added,error?0:updated);
    if (request) json_object_put(request);
    json_object_put(result);
}
