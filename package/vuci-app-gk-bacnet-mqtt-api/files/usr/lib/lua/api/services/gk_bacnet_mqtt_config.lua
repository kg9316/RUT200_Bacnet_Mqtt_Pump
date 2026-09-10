local FunctionService = require("api/FunctionService")
local uci = require("uci")
local Service = FunctionService:new()
local OPTIONS = {
    "enabled", "bacnet_interface", "mqtt_host", "mqtt_port", "topic_root",
    "poll_ms", "discovery_ms", "max_age_sec", "mqtt_mode", "mqtt_tls",
    "mqtt_ca_file", "mqtt_cert_file", "mqtt_key_file", "controller_id",
}

local function public_config(values)
    local data = {}
    for _, key in ipairs(OPTIONS) do data[key] = values[key] end
    data.controller_license_configured = (values.controller_license or "") ~= ""
    return data
end

function Service:GET_TYPE_config()
    local cursor = uci.cursor()
    return self:ResponseOK(public_config(cursor:get_all("gk_bacnet_mqtt", "main") or {}))
end

local function validate(values)
    local mode = values.mqtt_mode or "generic"
    if mode ~= "generic" and mode ~= "gk_cloud" then return "Invalid MQTT mode" end
    for _, key in ipairs({"enabled", "mqtt_tls"}) do
        if values[key] ~= nil and values[key] ~= "0" and values[key] ~= "1" then
            return "Invalid switch value"
        end
    end
    for key, limits in pairs({mqtt_port={1,65535}, poll_ms={100,3600000}, discovery_ms={100,3600000}, max_age_sec={1,86400}}) do
        local n = tonumber(values[key])
        if not n or n % 1 ~= 0 or n < limits[1] or n > limits[2] then return "Invalid " .. key end
    end
    for _, key in ipairs({"mqtt_ca_file", "mqtt_cert_file", "mqtt_key_file"}) do
        local value = values[key] or ""
        if #value > 255 or (value ~= "" and value:sub(1,1) ~= "/") or value:find("[%c]") then
            return "Certificate paths must be absolute and at most 255 bytes"
        end
    end
    if ((values.mqtt_cert_file or "") == "") ~= ((values.mqtt_key_file or "") == "") then
        return "Specify both client certificate and private key, or neither"
    end
    if #(values.mqtt_host or "") > 127 or #(values.topic_root or "") > 127 or #(values.bacnet_interface or "") > 63 then
        return "Configuration value too long"
    end
    if #(values.controller_id or "") > 127 or #(values.controller_license or "") > 8191 then return "Credentials too long" end
    if mode == "gk_cloud" then
        if not (values.controller_id or ""):match("^[%w_%-]+$") then return "Controller ID is required and must be a topic-safe ID" end
        if (values.controller_license or "") == "" then return "Controller license is required" end
    end
end

local function do_put(self)
    local body = self.arguments and self.arguments.data
    if type(body) ~= "table" then return self:ResponseError("No data in request") end
    local cursor = uci.cursor()
    local values = cursor:get_all("gk_bacnet_mqtt", "main") or {}
    for _, key in ipairs(OPTIONS) do
        if body[key] ~= nil then
            if type(body[key]) ~= "string" then return self:ResponseError("Expected string settings") end
            values[key] = body[key]
        end
    end
    -- An empty password input preserves the saved license. Never send it back.
    if body.controller_license ~= nil then
        if type(body.controller_license) ~= "string" then return self:ResponseError("Invalid license") end
        if body.controller_license ~= "" then values.controller_license = body.controller_license end
    end
    local error = validate(values)
    if error then return self:ResponseError(error) end
    for _, key in ipairs(OPTIONS) do
        if values[key] ~= nil then cursor:set("gk_bacnet_mqtt", "main", key, values[key]) end
    end
    if values.controller_license then cursor:set("gk_bacnet_mqtt", "main", "controller_license", values.controller_license) end
    if not cursor:save("gk_bacnet_mqtt") or not cursor:commit("gk_bacnet_mqtt") then
        return self:ResponseError("Unable to save configuration")
    end
    return self:ResponseOK(public_config(values))
end

Service.POST_TYPE_config = do_put
Service.POST_config = do_put
Service.POST_TYPE_general = do_put
Service.POST_general = do_put
Service.POST = do_put
Service.POST_TYPE = do_put
Service.PUT_TYPE_config = do_put
return Service
