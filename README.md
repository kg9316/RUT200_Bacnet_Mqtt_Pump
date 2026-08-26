# GK BACnet MQTT Gateway

Native BACnet/IP → MQTT gateway for Teltonika RutOS devices.

The project is no longer tied to a single router model. The daemon, UCI service and installable package are named `gk-bacnet-mqtt`; hardware families are build targets.

## Current support

| Target | Devices | RutOS SDK | Status |
|---|---|---|---|
| `RUT2M` | RUT200, RUT241, RUT260 | `RUT2M_R_GPL_00.07.24.2.tar.gz` | Build verified |
| `RUT14X` | RUT140, RUT142, RUT145 | `RUT14X_R_GPL_00.07.24.2.tar.gz` | Pipeline target added; hardware/package build still to be verified |

SDK checksums used by CI:

- RUT2M: `d94ad61ba377433ba96432de980b940a`
- RUT14X: `70eea784f08641d43a18f896e30022c4`

## Architecture

The gateway is split into modules:

```text
src/
├── main.c
├── gateway.h
├── util.c
├── device_table.c
├── device_table.h
├── mqtt_client.c
├── mqtt_client.h
├── bacnet_client.c
└── bacnet_client.h
```

The current daemon performs:

- BACnet/IP Who-Is / I-Am discovery
- device object-name discovery
- object-list enumeration
- point metadata reads
- Present Value polling
- MQTT reconnect handling
- retained MQTT configuration topics
- live MQTT value topics
- publish-on-change with maximum-age refresh

## RutOS packages

The build produces two packages per target:

```text
libbacnet-teltonika_1.3.8-1_<arch>.ipk
gk-bacnet-mqtt_1.0.0-1_<arch>.ipk
```

The installed application uses:

```text
/usr/sbin/gk-bacnet-mqtt
/etc/config/gk-bacnet-mqtt
/etc/init.d/gk-bacnet-mqtt
```

## Default configuration

```text
config gateway 'main'
        option enabled '1'
        option bacnet_interface 'br-lan'
        option mqtt_host '127.0.0.1'
        option mqtt_port '1883'
        option topic_root 'bacnet'
```

## GitHub Actions

Workflow:

```text
.github/workflows/build-gk.yml
```

The workflow is manual only (`workflow_dispatch`) and builds a matrix for `RUT2M` and `RUT14X`. It caches the RutOS staging/toolchain and Mosquitto build directories separately for each target.

### Required repository secrets

For RUT2M:

```text
TELTONIKA_SDK_URL
```

Set this to the direct download URL for:

```text
RUT2M_R_GPL_00.07.24.2.tar.gz
```

For RUT14X / RUT140:

```text
RUT14X_SDK_URL
```

Set this to the direct download URL for:

```text
RUT14X_R_GPL_00.07.24.2.tar.gz
```

The workflow verifies the downloaded SDK archive against the expected MD5 before extracting it.

## Build artifacts

A successful matrix run creates separate GitHub Actions artifacts, for example:

```text
gk-bacnet-mqtt-RUT2M-<commit>
gk-bacnet-mqtt-RUT14X-<commit>
```

Each artifact contains the target-specific `libbacnet-teltonika` and `gk-bacnet-mqtt` `.ipk` packages.

## Installation

Install the BACnet library package first, then the gateway package:

```sh
opkg install /tmp/libbacnet-teltonika_*.ipk
opkg install /tmp/gk-bacnet-mqtt_*.ipk
/etc/init.d/gk-bacnet-mqtt enable
/etc/init.d/gk-bacnet-mqtt start
```

The same installation can be deployed remotely through Teltonika RMS Task Manager.

## Planned

- structured INFO/WARN/ERROR/DEBUG logging
- runtime status endpoint/state file
- VuCI WebUI and API package
- additional RutOS hardware families after build/runtime validation
