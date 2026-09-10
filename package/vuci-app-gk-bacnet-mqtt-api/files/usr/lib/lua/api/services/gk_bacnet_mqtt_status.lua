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
    snapshot.packageVersion = (read_file("/usr/share/gk-bacnet-mqtt/version") or "unknown"):gsub("%s+$", "")
    return self:ResponseOK({ running = snapshot.running == true, status = json.stringify(snapshot) })
end

function Service:GET_TYPE_points()
    local raw = read_file("/tmp/gk-bacnet-mqtt-status.json")
    local snapshot = raw and json.parse(raw) or {}
    local points = (snapshot or {}).pointDetails or {}
    -- Accept a snapshot from the previous daemon while an upgrade is in progress.
    for i, p in ipairs(points) do
        if p.tag ~= nil or p.deviceId ~= nil then
            points[i] = { t = p.tag, n = p.name or "", u = p.unit or "", d = p.description or "",
                di = p.deviceId, ot = p.objectType, oi = p.objectInstance }
        end
    end
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
