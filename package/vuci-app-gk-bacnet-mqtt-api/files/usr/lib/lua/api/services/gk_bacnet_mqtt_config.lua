local FunctionService = require("api/FunctionService")
local uci = require("uci")

-- Rewritten from ConfigService: PUT reliably reported success and echoed
-- back the correct new values while never touching /etc/config,
-- /tmp/.uci, or /tmp/.uci-vuci at any level. Root cause is inside
-- Teltonika's compiled ConfigService/put_logic.lua, which we can't read
-- beyond string constants. FunctionService (same base BasicService's
-- GET_TYPE_%s/PUT_TYPE_%s dispatch our working status/interfaces/log
-- handlers already use) plus a direct uci cursor sidesteps that layer
-- entirely - same approach as any plain OpenWrt Lua UCI script.

local Service = FunctionService:new()

local OPTIONS = {
	"enabled", "bacnet_interface", "mqtt_host", "mqtt_port",
	"topic_root", "poll_ms", "discovery_ms", "max_age_sec",
}

local function dump(value, depth)
	depth = depth or 0
	if depth > 2 then return "..." end
	if type(value) ~= "table" then
		return tostring(value)
	end
	local parts = {}
	for k, v in pairs(value) do
		parts[#parts + 1] = tostring(k) .. "=" .. dump(v, depth + 1)
	end
	return "{" .. table.concat(parts, ", ") .. "}"
end

function Service:GET_TYPE_config()
	local cursor = uci.cursor()
	local values = cursor:get_all("gk_bacnet_mqtt", "main") or {}
	local data = {}
	for _, opt in ipairs(OPTIONS) do
		data[opt] = values[opt]
	end
	return self:ResponseOK(data)
end

function Service:PUT_TYPE_config(arguments)
	os.execute(string.format(
		"logger -t gk-bacnet-mqtt-config 'PUT self.request=%s param=%s self.data=%s'",
		dump(self.request and self.request.data):gsub("'", ""),
		dump(arguments):gsub("'", ""),
		dump(self.data):gsub("'", "")
	))

	local body = arguments
	if type(body) ~= "table" and self.request and self.request.data then
		body = self.request.data.data or self.request.data
	end
	if type(body) ~= "table" then
		return self:ResponseError("No data in request")
	end

	local cursor = uci.cursor()
	for _, opt in ipairs(OPTIONS) do
		if body[opt] ~= nil then
			cursor:set("gk_bacnet_mqtt", "main", opt, tostring(body[opt]))
		end
	end
	cursor:commit("gk_bacnet_mqtt")
	os.execute("/etc/init.d/gk-bacnet-mqtt restart >/dev/null 2>&1 &")

	local values = cursor:get_all("gk_bacnet_mqtt", "main") or {}
	local data = {}
	for _, opt in ipairs(OPTIONS) do
		data[opt] = values[opt]
	end
	return self:ResponseOK(data)
end

return Service
