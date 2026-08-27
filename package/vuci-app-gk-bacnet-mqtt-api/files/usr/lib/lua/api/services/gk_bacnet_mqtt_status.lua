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
	return self:ResponseOK({
		running = raw ~= nil,
		status = raw or "{\"running\":false}"
	})
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

return Service
