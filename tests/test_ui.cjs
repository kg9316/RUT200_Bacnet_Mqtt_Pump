const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const file = process.argv[2];
const source = fs.readFileSync(file, 'utf8').match(/<script>([\s\S]*?)<\/script>/)[1];
const context = { module: { exports: {} } };
vm.runInNewContext(source.replace('export default', 'module.exports ='), context);
const component = context.module.exports;
const page = { ...component.data(), ...component.methods, $t: s => s };
for (const [name, getter] of Object.entries(component.computed)) {
  Object.defineProperty(page, name, { get: () => getter.call(page) });
}
const fields = () => component.computed.configFields.call(page).map(f => f.key);
assert(fields().includes('mqtt_host') && !fields().includes('controller_license'));
page.config.mqtt_mode = 'gk_cloud';
assert(!fields().includes('mqtt_host') && !fields().includes('mqtt_tls'));
assert(fields().includes('controller_license') && fields().includes('controller_id'));
page.config.controller_license = 'private-license';
let posted;
page.$axios = { post: async (url, body) => {
  posted = JSON.parse(JSON.stringify(body));
  return { data: { enabled: '1', mqtt_host: 'localhost', controller_license_configured: true } };
}};
(async () => {
  await page.saveConfig();
  assert.equal(posted.data.controller_license, 'private-license');
  assert.equal(page.config.controller_license, '');
  assert.equal(page.saveError, false);
  page.$axios.post = async () => ({ data: { error: 'save failed' } });
  await page.saveConfig();
  assert.equal(page.saveError, true);
  console.log('PASS: GK/generic form fields, license clearing, rejected save response');
})().catch(e => { console.error(e); process.exitCode = 1; });
