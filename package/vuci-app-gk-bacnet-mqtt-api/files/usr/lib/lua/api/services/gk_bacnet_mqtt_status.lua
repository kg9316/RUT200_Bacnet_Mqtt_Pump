local json = require("luci.jsonc")
local uci = require("uci")
local FunctionService = require("api/FunctionService")

local Service = FunctionService:new()

local function read_file(path)
	local f = io.open(path, "r")
	if not f then
		return nil
	end
	local data = f:read("*a")
	f:close()
	return data
end

function Service:GET_TYPE_status()
    local raw = read_file("/tmp/gk-bacnet-mqtt-status.json")
    local snapshot = raw and json.parse(raw) or { running = false }
    snapshot = snapshot or { running = false }
    snapshot.pointDetails = nil
    local control = read_file("/usr/local/lib/opkg/info/vuci-app-gk-bacnet-mqtt-ui.control")
        or read_file("/usr/lib/opkg/info/vuci-app-gk-bacnet-mqtt-ui.control") or ""
    snapshot.packageVersion = control:match("Version:%s*([^\r\n]+)") or "unknown"
    return self:ResponseOK({ running = snapshot.running == true, status = json.stringify(snapshot) })
end

function Service:GET_TYPE_points()
    local raw = read_file("/etc/gk-bacnet-mqtt/tags.json")
    local registry = raw and json.parse(raw)
    local points = {}
    -- Legacy address -> GUID strings and enriched entries share the same keys.
    for address, entry in pairs(registry or {}) do
        local device, object_type, instance = address:match("^(%d+):(%d+):(%d+)$")
        if device then
            local p = type(entry) == "string" and { t = entry } or entry
            points[#points + 1] = { t = p.t, n = p.n or "", u = p.u or "", d = p.d or "",
                di = tonumber(device), ot = tonumber(object_type), oi = tonumber(instance) }
        end
    end
    table.sort(points, function(a, b)
        if a.di ~= b.di then return a.di < b.di end
        if a.ot ~= b.ot then return a.ot < b.ot end
        return a.oi < b.oi
    end)
    local cursor = uci.cursor()
    local controller = cursor:get("gk_bacnet_mqtt", "main", "controller_id") or ""
    return self:ResponseOK({ schemaVersion = 2, controllerId = controller,
        exportedAt = os.date("!%Y-%m-%dT%H:%M:%SZ"), points = points })
end

function Service:GET_TYPE_interfaces()
	local interfaces = {}
	local p = io.popen("ls -1 /sys/class/net 2>/dev/null")
	if p then
		for name in p:lines() do
			if name ~= "" and name ~= "lo" then
				interfaces[#interfaces + 1] = name
			end
		end
		p:close()
	end
	return self:ResponseOK({ interfaces = interfaces })
end

function Service:GET_TYPE_log()
	local p = io.popen("logread -e gk-bacnet-mqtt 2>/dev/null | tail -n 200")
	local log = ""
	if p then
		log = p:read("*a") or ""
		p:close()
	end
	return self:ResponseOK({ log = log })
end

function Service:GET_TYPE_mqtt_log()
	local p = io.popen("logread -e gk-bacnet-mqtt 2>/dev/null | grep -E 'MQTT|GK ' | tail -n 200")
	local log = ""
	if p then log = p:read("*a") or ""; p:close() end
	return self:ResponseOK({ log = log })
end

return Service
