# Validation – 10 September 2026

Version 1.0.0-41 was built successfully and installed on the RUT200 with RutOS 00.07.24.2.
Build: https://github.com/kg9316/RUT200_Bacnet_Mqtt_Pump/actions/runs/34467676444

Verified on the router:
- Upgrade preserved configuration and all GUIDs byte for byte.
- Two BACnet devices and 22 named points with unit, description and persistent GUID.
- GK TLS connection and increasing QoS 0 sent counter, zero MQTT errors.
- MQTT connection/authentication logs and periodic send summary; packet trace noise disabled.
- Browser download of all 22 point mappings succeeded.
- Browser testing identified that scoped dropdown CSS was not applied by the deployed UI.

Version 1.0.0-42 changes the dropdown colors to explicit inline styles, removes the point table,
and fetches the export on demand with controllerId from saved configuration. Runtime status
responses omit pointDetails to avoid repeatedly sending large point lists to the browser.

MQTT QoS 0 send completion does not prove receipt by the broker or downstream application.
No credentials are embedded in the source or package configuration.

Version 1.0.0-42 build succeeded:
https://github.com/kg9316/RUT200_Bacnet_Mqtt_Pump/actions/runs/34469209491
Source commit: a99aa799a3dfa20ea2f0420478b0deff5634bfa9
Bundle SHA-256: 7f055b417f817e889298ad746787da92cb175adb2b8e8add7e0babfbd4f1e0e2

Installed on the router; configuration and GUID registry hashes unchanged.
The live status API returns 22 points, connected MQTT with TLS, increasing sent count,
zero MQTT errors and no pointDetails in the periodic response.
The new export handler passed tests against the router's real Lua JSON/UCI libraries:
22 GUIDs and local metadata, controllerId from saved UCI, no live values or license.
The installed browser UI passed: point table absent, download contains controllerId and all 22 GUID mappings, dropdown foreground/background verified and screenshot inspected, no JavaScript errors.
