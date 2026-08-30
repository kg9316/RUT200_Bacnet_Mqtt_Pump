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

local function do_put(self)
	-- Confirmed empirically via a temporary debug dump (now removed): the
	-- dispatcher sets self.arguments directly rather than passing the
	-- parsed body as a function parameter, and the actual submitted
	-- fields are one level further in, under .data (matching the
	-- frontend's {data: config} body).
	local body = self.arguments and self.arguments.data
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
	local restart = io.popen("/etc/init.d/gk-bacnet-mqtt restart >/dev/null 2>&1 &")
	if restart then restart:close() end

	local values = cursor:get_all("gk_bacnet_mqtt", "main") or {}
	local data = {}
	for _, opt in ipairs(OPTIONS) do
		data[opt] = values[opt]
	end
	return self:ResponseOK(data)
end

-- PUT_TYPE_config (mirroring the confirmed-working GET_TYPE_%s
-- convention) got "PUT not implemented" back. Turns out put_logic.lua/
-- get_logic.lua/post_logic.lua in Teltonika's own api-core source are
-- ConfigService-only internals (full of section/.type-specific
-- concepts) - FunctionService never requires them, so it likely has no
-- PUT dispatch at all, only GET (the simple TYPE convention we already
-- use) and POST (an "action" mechanism per its own string constants).
-- Switch to POST, and again register several plausible names against
-- the same handler rather than guess one at a time.
Service.POST_TYPE_config = do_put
Service.POST_config = do_put
Service.POST_TYPE_general = do_put
Service.POST_general = do_put
Service.POST = do_put
Service.POST_TYPE = do_put
Service.PUT_TYPE_config = do_put

return Service
