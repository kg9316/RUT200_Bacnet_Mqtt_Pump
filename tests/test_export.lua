local registry
package.preload['luci.jsonc']=function()return {parse=function()return registry end}end
package.preload['uci']=function()return {cursor=function()return {get=function()return 'controller-A'end}end}end
package.preload['api/FunctionService']=function()return {new=function()return {ResponseOK=function(_,d)return d end}end}end
io.open=function()return {read=function()return '{}'end,close=function()end}end
local service=dofile(arg[1])
local id='aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa'
registry={['3:5:1']={t=id,s={}},_dn={},_cfg={m={},n={}}}
local d=service:GET_TYPE_points()
assert(d._cfg==nil and d.devices==nil and d.points[1].s==nil and d.points[1].t==id)
registry._cfg={n={['3']=false},m={{ip='10.0.0.1'}}};registry._dn={['3']='Controller'};registry['3:5:1'].s={['0']='Stopp',['1']='Drift'}
d=service:GET_TYPE_points()
assert(d._cfg.n['3']==false and d._cfg.m[1].ip=='10.0.0.1')
assert(d.devices['3']=='Controller' and d.points[1].s['1']=='Drift' and d.points[1].t==id)
print('PASS: export omits empty maps and preserves GUIDs, states, names and device settings')
