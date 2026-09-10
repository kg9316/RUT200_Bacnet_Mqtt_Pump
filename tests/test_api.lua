local values = {
    enabled="1", mqtt_host="localhost", mqtt_port="1883", topic_root="bacnet",
    bacnet_interface="br-lan", poll_ms="5000", discovery_ms="10000", max_age_sec="300",
    mqtt_mode="generic", mqtt_tls="0", controller_license=string.rep("x", 1800),
}
local commits = 0
package.preload["uci"] = function()
    return {cursor=function() return {
        get_all=function() local copy={} for k,v in pairs(values) do copy[k]=v end return copy end,
        set=function(_,_,_,key,value) values[key]=value return true end,
        save=function() return true end,
        commit=function() commits=commits+1 return true end,
    } end}
end
package.preload["api/FunctionService"] = function()
    return {new=function() return {
        ResponseOK=function(_,data) return {ok=true,data=data} end,
        ResponseError=function(_,error) return {ok=false,error=error} end,
    } end}
end
local service = dofile(arg[1])
local result = service:GET_TYPE_config()
assert(result.ok and result.data.controller_license == nil and result.data.controller_license_configured)
service.arguments = {data={mqtt_mode="gk_cloud",controller_id="controller-123",controller_license=""}}
result = service:POST_TYPE_config()
assert(result.ok and #values.controller_license == 1800 and commits == 1)
assert(result.data.controller_license == nil)
service.arguments = {data={controller_license=string.rep("x",8192)}}
assert(not service:POST_TYPE_config().ok and commits == 1)
service.arguments = {data={controller_id="bad/+/id"}}
assert(not service:POST_TYPE_config().ok and commits == 1)
service.arguments = {data={mqtt_key_file="/key.pem",mqtt_cert_file=""}}
assert(not service:POST_TYPE_config().ok and commits == 1)
service.arguments = {data={mqtt_port="65536"}}
assert(not service:POST_TYPE_config().ok and commits == 1)
service.arguments = {data={controller_license=string.rep("z",2400)}}
result = service:POST_TYPE_config()
assert(result.ok and #values.controller_license == 2400 and commits == 2)
assert(result.data.controller_license == nil)
print("PASS: license redaction/preservation, long credentials, controller validation, TLS pair validation, port bounds")
