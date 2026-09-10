# Validation - 10 September 2026

Package 1.0.0-47 built successfully and installed on the RUT200 running RutOS 00.07.24.2.
Build: https://github.com/kg9316/RUT200_Bacnet_Mqtt_Pump/actions/runs/34472729019
Source commit: e8bb9162345d5702f1202d7c04ac77937ce3c2b2
Bundle SHA-256: 0a11f295e4537369fa32b2a942fe50a727f51c4c8ebc943d599ab91851d3e817

Passed:
- CI C protocol, GUID migration/persistence, MQTT/TLS, runtime status, Lua configuration,
  Vue form and dynamic device-table tests; exact target SDK build and artifact checks.
- Dynamic table test: 10,000 points on one device, 500 device entries, preserved values
  and active request pointers across growth, upper bounds and cleanup.
- Target struct sizes: 600 bytes per point and 144 bytes per device; point capacity
  is allocated on demand. This is not a benchmark of total gateway memory or throughput.
- Upgrade from the legacy string GUID registry to compact metadata objects preserved
  all 22 identities. A subsequent upgrade/restart preserved them again and kept config.
- One persistent tags.json contains only t/n/u/d, keyed by BACnet address. No measured
  values or duplicate point metadata remain in the runtime status file.
- Router files: tags.json 2,435 bytes; runtime status approximately 350 bytes, compared
  with approximately 7,500 bytes combined before consolidation. Unchanged metadata file
  retained its modification time across normal polling and restart.
- Live GK TLS connection, 22 points, increasing MQTT sent counter and zero errors.
- Version is read from installed opkg metadata, not a hard-coded UI label.

No additional on-router memory stress test was run, following the user's instruction.
The configured 500-device/10,000-points-per-device limits do not promise that all five
million possible points fit in router memory. QoS 0 send completion does not prove
receipt by the broker or downstream application. No credentials are shipped in source.
- Installed browser UI passed: native RutOS dropdowns, package version 1.0.0-47, compact schemaVersion 2 download with controllerId and all 22 GUIDs; no JavaScript errors. New daemon log contains connection events and no periodic MQTT summaries or first-message line.
