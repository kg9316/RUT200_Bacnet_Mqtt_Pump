# GK BACnet MQTT Gateway

Native BACnet/IP → MQTT gateway for Teltonika RutOS devices.

## Current support

| Target | Devices | RutOS SDK | Status |
|---|---|---|---|
| `RUT2M` | RUT200, RUT241, RUT260 | `RUT2M_R_GPL_00.07.24.2.tar.gz` | Current CI target |
| `RUT14X` | RUT140, RUT142, RUT145 | `RUT14X_R_GPL_00.07.24.2.tar.gz` | Planned/under validation |

RUT140 is a relevant target for this project because it runs RutOS and can host native packages/services in the same general way as RUT200. It requires its own `RUT14X` SDK build and must not be assumed binary-compatible with RUT2M packages.

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
└── vuci-app-gk-bacnet-mqtt-ui/
```

The daemon performs BACnet/IP discovery, object enumeration, metadata reads, Present Value polling, MQTT reconnect handling, retained configuration topics, live value topics and publish-on-change with maximum-age refresh.

## Runtime packages

The build produces three GK packages:

```text
gk-bacnet-mqtt
vuci-app-gk-bacnet-mqtt-api
vuci-app-gk-bacnet-mqtt-ui
```

The UI package depends on the API package and core daemon. The API package depends on the core daemon.

The core daemon links bacnet-stack 1.3.8 statically (fetched and compiled by
CI, `BACDL_BIP` with `BBMD_ENABLED=0`) rather than depending on Teltonika's
proprietary `lib-bacnet`/`libbacnet.so`. Its only runtime library dependencies
are:

```text
gk-bacnet-mqtt
├── libmosquitto-ssl
└── libatomic
```

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

Saving configuration takes effect live, typically within a couple of seconds -
`gk-bacnet-mqtt` watches its own UCI config file and reloads every setting
in-process (including rebinding the BACnet datalink on an interface change,
and pausing/resuming on enabled/disabled), so no restart is ever needed from
the UI.

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

The old monolithic full-validation/cache workflow has been removed. RUT2M CI is deliberately split into independent stages so a late VuCI failure cannot force the expensive SDK/toolchain build to run again.

Run the workflows in this order:

```text
0 - Validate GK Build Workflows
        │
        ▼
1 - Prepare RUT2M SDK Base Cache
        │
        ▼
2 - Prepare RUT2M VuCI Cache
        │
        ▼
3 - Fast Build GK BACnet MQTT IPKs - RUT2M
```

### 0 - Validate GK Build Workflows

File: `.github/workflows/validate-gk-workflows.yml`

Performs cheap repository/workflow checks. It also guards Fast Build against operations that would invalidate the prepared SDK, such as rebuilding the toolchain or VuCI core.

### 1 - Prepare RUT2M SDK Base Cache

File: `.github/workflows/prepare-sdk-base.yml`

This is the expensive and rarely-run stage. It prepares the RUT2M SDK, tools, toolchain, Mosquitto and Teltonika BACnet prerequisites, then saves the result before VuCI is built.

Base cache:

```text
gk-sdk-base-RUT2M-00.07.24.2-v1-<runner-os>
```

Once this cache exists, a later VuCI failure does not require another tools/toolchain build.

### 2 - Prepare RUT2M VuCI Cache

File: `.github/workflows/prepare-vuci-cache.yml`

Restores the exact base cache, registers/selects the GK packages and prebuilds `vuci-ui-core`. It then saves the SDK state used by Fast Build.

VuCI cache:

```text
gk-sdk-vuci-RUT2M-00.07.24.2-v1-<runner-os>
```

If this stage fails, fix/retry this stage only. The expensive base cache remains available.

### 3 - Fast Build

File: `.github/workflows/build-gk-fast.yml`

Fast Build requires an exact VuCI cache hit. It refreshes only GK-owned source/package files and builds only:

```text
package/gk-bacnet-mqtt/compile
package/vuci-app-gk-bacnet-mqtt-api/compile
package/vuci-app-gk-bacnet-mqtt-ui/compile
```

Fast Build must not run `tools/install`, `toolchain/install`, feed refresh, `make defconfig`, `rm -rf tmp`, package clean operations or `vuci-ui-core/compile`.

A successful Fast Build creates the three `.ipk` packages plus the Teltonika Package Manager bundle `gk_bacnet_mqtt.tar.gz`.

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

- VuCI rendering/API compatibility on the exact RUT200 firmware build
- BACnet discovery/polling against physical devices
- MQTT TLS configuration
- RUT140/RUT14X SDK target, runtime libraries and package compatibility before enabling RUT14X CI
