-- Called asynchronously with numeric socket addresses only. The daemon bounds
-- the lifetime of this process group, including ip/ubus, to three seconds.
local json = require("luci.jsonc")
local dst, src, mark = arg[1], arg[2], arg[3]
if not dst or not src or not mark or not dst:match("^[%x.:]+$") or
   not src:match("^[%x.:]+$") or not mark:match("^%d+$") then os.exit(1) end
local function read(command)
    local p = io.popen(command)
    if not p then return "" end
    local value = p:read("*a") or ""
    p:close()
    return value
end
local route = read("ip route get " .. dst .. " from " .. src .. " mark " .. mark .. " 2>/dev/null")
local dev = route:match("%sdev%s+([%w_.:%-]+)")
if not dev or dev:match("^tun") or dev:match("^tap") or dev:match("^wg") then os.exit(1) end
local data = json.parse(read("ubus -t 1 call network.interface dump 2>/dev/null"))
local mobile_protocols = {wwan=true, qmi=true, mbim=true, ncm=true, ["3g"]=true}
local found, mobile = false, false
for _, interface in ipairs(data and data.interface or {}) do
    if interface.up and (interface.l3_device == dev or interface.device == dev) then
        found = found or mobile_protocols[interface.proto] or interface.modem ~= nil or
            interface.proto == "dhcp" or interface.proto == "dhcpv6" or interface.proto == "static"
        mobile = mobile or mobile_protocols[interface.proto] or interface.modem ~= nil or
            (interface.interface or ""):match("^mob%d") ~= nil
    end
end
-- Unrecognised interfaces (including VPN tunnels) are deliberately unknown;
-- the daemon uses mobile limits until the underlying connection is known.
if not found then os.exit(1) end
io.write(mobile and "1 " or "0 ", dev, "\n")
