local ConfigService = require("api/ConfigService")

-- Keep the service definition aligned with Teltonika's ConfigService example.
-- The primary section identifies the UCI configuration and section type;
-- a named section does not require the general_section service flag.
local Service = ConfigService:new({
	delete = false,
	create = false,
})

local Main = Service:section(
	"gk_bacnet_mqtt", -- UCI config name
	"gateway"         -- UCI section type
)
Main:make_primary()

local enabled = Main:option("enabled")
function enabled:validate(value)
	return self.dt:is_bool(value)
end

local bacnet_interface = Main:option("bacnet_interface")
bacnet_interface.maxlength = 32

local mqtt_host = Main:option("mqtt_host")
mqtt_host.maxlength = 128

local mqtt_port = Main:option("mqtt_port")
function mqtt_port:validate(value)
	local n = tonumber(value)
	return n ~= nil and n >= 1 and n <= 65535
end

local topic_root = Main:option("topic_root")
topic_root.maxlength = 128

local poll_ms = Main:option("poll_ms")
function poll_ms:validate(value)
	local n = tonumber(value)
	return n ~= nil and n >= 100 and n <= 3600000
end

local discovery_ms = Main:option("discovery_ms")
function discovery_ms:validate(value)
	local n = tonumber(value)
	return n ~= nil and n >= 1000 and n <= 3600000
end

local max_age_sec = Main:option("max_age_sec")
function max_age_sec:validate(value)
	local n = tonumber(value)
	return n ~= nil and n >= 1 and n <= 86400
end

function Service:PUT_after_commit_hook()
	os.execute("/etc/init.d/gk-bacnet-mqtt restart >/dev/null 2>&1 &")
end

return Service
