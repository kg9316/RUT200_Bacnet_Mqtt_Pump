# GK BACnet MQTT Gateway

Native BACnet/IP → MQTT gateway for Teltonika RutOS devices.

## Current support

| Target | Devices | RutOS SDK | Status |
|---|---|---|---|
| `RUT2M` | RUT200, RUT241, RUT260 | `RUT2M_R_GPL_00.07.24.2.tar.gz` | Build verified |
| `RUT14X` | RUT140, RUT142, RUT145 | `RUT14X_R_GPL_00.07.24.2.tar.gz` | Build verified |

## Architecture

```text
src/
├── main.c
├── gateway.h
├── util.c
├── device_table.c/.h
├── mqtt_client.c/.h
├── bacnet_client.c/.h
├── logger.h
└── status.c/.h

package/
├── gk-bacnet-mqtt/
├── vuci-app-gk-bacnet-mqtt-api/
├── vuci-app-gk-bacnet-mqtt-ui/
└── build-shims/lib-bacnet/
```

The daemon performs BACnet/IP discovery, object enumeration, metadata reads, Present Value polling, MQTT reconnect handling, retained configuration topics, live value topics and publish-on-change with maximum-age refresh.

## Runtime packages

The workflow now builds three GK packages per target:

```text
gk-bacnet-mqtt
vuci-app-gk-bacnet-mqtt-api
vuci-app-gk-bacnet-mqtt-ui
```

The UI package depends on the API package and core daemon. The API package depends on the core daemon.

The core daemon uses RutOS/Teltonika runtime libraries:

```text
gk-bacnet-mqtt
├── lib-bacnet
├── libmosquitto-ssl
└── libatomic
```

`lib-bacnet` is Teltonika's BACnet runtime dependency. The GPL SDK contains `libbacnet.so` but does not expose `lib-bacnet` as a normal package definition, so CI injects a build-time shim that exposes the SDK library to OpenWrt's ELF dependency scanner. The shim itself is not published.

## Logging

The daemon opens the native RutOS syslog facility as `gk-bacnet-mqtt` and uses INFO/WARN/ERROR messages for startup, MQTT state, reconnects and failures. Existing BACnet stdout/stderr diagnostics remain captured by `procd`.

```sh
logread -e gk-bacnet-mqtt
logread -f | grep gk-bacnet-mqtt
```

## Runtime status

The daemon atomically updates:

```text
/tmp/gk-bacnet-mqtt-status.json
```

once per second. It contains service/MQTT state, discovered device and point counts, active MQTT endpoint/topic root and poll/discovery/max-age settings. The VuCI API reads this file; no extra HTTP daemon is required.

## VuCI WebUI

Native RutOS VuCI support is included using Teltonika's `ConfigService`, `FunctionService` and Vue/VuCI application structure.

The page provides:

- service running/stopped state
- MQTT connected/disconnected state
- discovered BACnet device count
- discovered BACnet point count
- enabled state
- BACnet interface
- MQTT host and port
- topic root
- poll interval
- discovery interval
- maximum publish age
- last 200 matching RutOS log lines

Saving configuration restarts `gk-bacnet-mqtt` so the new settings take effect.

## Default UCI configuration

```text
config gateway 'main'
        option enabled '1'
        option bacnet_interface 'br-lan'
        option mqtt_host '127.0.0.1'
        option mqtt_port '1883'
        option topic_root 'bacnet'
        option poll_ms '5000'
        option discovery_ms '10000'
        option max_age_sec '300'
```

## GitHub Actions

Workflow: `.github/workflows/build-gk.yml`.

The workflow is manual (`workflow_dispatch`) and builds both RUT2M and RUT14X. A prepared SDK is cached per target/version, so cache hits skip SDK download, feed update, toolchain preparation and Mosquitto compilation.

Required secrets:

```text
TELTONIKA_SDK_URL
RUT14X_SDK_URL
```

A successful target build collects all three target-specific GK `.ipk` packages into its GitHub Actions artifact.

## Installation

Install the core, API and UI packages together. Their declared dependencies allow `opkg`/RutOS Package Manager to resolve ordering and RutOS libraries.

```sh
opkg install /tmp/gk-bacnet-mqtt_*.ipk \
             /tmp/vuci-app-gk-bacnet-mqtt-api_*.ipk \
             /tmp/vuci-app-gk-bacnet-mqtt-ui_*.ipk

/etc/init.d/gk-bacnet-mqtt enable
/etc/init.d/gk-bacnet-mqtt start
```

## Still to validate on hardware

- VuCI rendering/API compatibility on the exact RUT200/RUT140 firmware build
- actual availability/resolution of Teltonika `lib-bacnet` through Package Manager
- BACnet discovery/polling against physical devices
- MQTT TLS configuration
