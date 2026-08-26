# GK BACnet MQTT Gateway

Native BACnet/IP → MQTT gateway for Teltonika RutOS devices.

The project is not tied to a single router model. The daemon, UCI service and installable package are named `gk-bacnet-mqtt`; hardware families are build targets.

## Current support

| Target | Devices | RutOS SDK | Status |
|---|---|---|---|
| `RUT2M` | RUT200, RUT241, RUT260 | `RUT2M_R_GPL_00.07.24.2.tar.gz` | Build verified |
| `RUT14X` | RUT140, RUT142, RUT145 | `RUT14X_R_GPL_00.07.24.2.tar.gz` | Pipeline target added; build/runtime still to be verified |

SDK checksums used by CI:

- RUT2M: `d94ad61ba377433ba96432de980b940a`
- RUT14X: `70eea784f08641d43a18f896e30022c4`

## Architecture

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

The current daemon performs BACnet/IP discovery, object enumeration, metadata reads, Present Value polling, MQTT reconnect handling, retained configuration topics, live value topics and publish-on-change with maximum-age refresh.

## RutOS dependencies

`gk-bacnet-mqtt` uses RutOS/Teltonika runtime packages instead of shipping duplicate libraries:

```text
gk-bacnet-mqtt
├── lib-bacnet
├── libmosquitto-ssl
└── libatomic
```

`lib-bacnet` is the official Teltonika BACnet runtime dependency used by Teltonika's own BACnet Router package. `libmosquitto-ssl` is the RutOS Mosquitto client library with TLS capability.

The Teltonika GPL SDK contains `libbacnet.so`, but does not expose `lib-bacnet` as a normal buildable package definition. To let the OpenWrt package build resolve `DEPENDS:=+lib-bacnet`, CI injects a metadata-only build shim from:

```text
package/build-shims/lib-bacnet/Makefile
```

The shim contains no runtime library and is not collected as a release artifact. The generated GK IPK still declares `lib-bacnet` as its runtime dependency, so the real package is expected to be supplied by RutOS Package Manager/opkg on the target device.

The build therefore produces only one GK package per target:

```text
gk-bacnet-mqtt_1.0.0-2_<arch>.ipk
```

At build time the gateway is linked against the `libbacnet.so` supplied by the corresponding RutOS SDK, while installation declares `lib-bacnet` as the runtime dependency.

## Installed files

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

The workflow is manual only (`workflow_dispatch`) and builds a matrix for `RUT2M` and `RUT14X`.

A prepared SDK is cached per target and SDK version. On a cache hit, the workflow skips SDK download, extraction, feed update and toolchain/Mosquitto preparation.

### Required repository secrets

RUT2M:

```text
TELTONIKA_SDK_URL
```

Direct URL for `RUT2M_R_GPL_00.07.24.2.tar.gz`.

RUT14X:

```text
RUT14X_SDK_URL
```

Direct URL for `RUT14X_R_GPL_00.07.24.2.tar.gz`.

The workflow verifies each SDK archive against the configured MD5 before preparing and caching it.

## Build artifacts

A successful matrix run creates one GitHub Actions artifact per target:

```text
gk-bacnet-mqtt-RUT2M-<commit>
gk-bacnet-mqtt-RUT14X-<commit>
```

Each artifact contains only the target-specific `gk-bacnet-mqtt` `.ipk` package.

## Installation

Install the GK package with RutOS Package Manager/opkg. The declared dependencies should resolve the official Teltonika/RutOS BACnet and Mosquitto runtime packages:

```sh
opkg install /tmp/gk-bacnet-mqtt_*.ipk
/etc/init.d/gk-bacnet-mqtt enable
/etc/init.d/gk-bacnet-mqtt start
```

The same package can be deployed remotely through Teltonika RMS Task Manager.

## Planned

- structured INFO/WARN/ERROR/DEBUG logging
- MQTT TLS configuration in UCI/WebUI
- runtime status endpoint/state file
- VuCI WebUI and API package
- additional RutOS hardware families after build/runtime validation
