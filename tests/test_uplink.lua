local script=arg[1]
local interfaces, device, output
package.preload['luci.jsonc']=function() return {parse=function() return {interface=interfaces} end} end
io.popen=function(cmd) return {read=function() return cmd:match('^ip ') and ('route dev '..device..' src 192.168.1.86') or '{}' end,close=function() end} end
io.write=function(...) output=table.concat({...}) end
os.exit=function(code) error('exit:'..code) end
local function check(dev, rows, expected)
 device=dev;interfaces=rows;output=nil;arg={'146.148.6.11','192.168.1.86','0'}
 local ok=pcall(dofile,script)
 if expected then assert(ok and output==expected,output or 'no output') else assert(not ok) end
end
check('wlan0-2',{{up=true,l3_device='wlan0-2',proto='dhcp'}},'0 wlan0-2\n')
check('wwan0',{{up=true,l3_device='wwan0',proto='wwan',interface='mob1s1a1'}},'1 wwan0\n')
check('wwan0',{{up=true,l3_device='wwan0',proto='qmi'}},'1 wwan0\n')
check('wlan0-2',{{up=true,l3_device='wlan0-2',proto='dhcp'},{up=true,l3_device='wwan0',proto='wwan'}},'0 wlan0-2\n')
check('tun0',{{up=true,l3_device='tun0',proto='static'}},nil)
check('unknown',{},nil)
print('PASS: Wi-Fi, mobile, simultaneous uplinks, unknown/VPN fallback')
