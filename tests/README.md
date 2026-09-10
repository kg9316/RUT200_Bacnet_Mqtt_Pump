# MQTT / GK regression tests

On Linux, install a C compiler, libcurl and json-c development packages,
libmosquitto headers, Lua 5.1 and Node.js. Set `BACNET_INCLUDE` to the `src`
directory of bacnet-stack 1.3.8 (the same release as the gateway build).

```sh
cc -I"$BACNET_INCLUDE" tests/test_cloud.c -lcurl -ljson-c -lm -o /tmp/test-cloud
/tmp/test-cloud
cc -I"$BACNET_INCLUDE" tests/test_mqtt.c -o /tmp/test-mqtt
/tmp/test-mqtt
lua5.1 tests/test_api.lua package/vuci-app-gk-bacnet-mqtt-api/files/usr/lib/lua/api/services/gk_bacnet_mqtt_config.lua
node tests/test_ui.cjs package/vuci-app-gk-bacnet-mqtt-ui/src/src/views/services/GkBacnetMqtt.vue
```

The cloud test writes `./tags-test.json`, exercises the actual JSON and GUID
code, then removes that test file. Run from a writable test directory without
an existing file of that name. On Windows its file-operation adapters use
Windows random generation and file replacement; directory fsync must still be
verified on RutOS. The MQTT test mocks libmosquitto to exercise connection
ordering, TLS errors and token lifecycle without a broker or credentials.
The API test mocks UCI/FunctionService. The UI test exercises the component
script, not VuCI rendering.

No test contains production credentials or publishes to GK Cloud.
