# Validation â€“ 10 September 2026

Pushed to main and built successfully with the exact RUT2M SDK. Not deployed to a router.

Build: https://github.com/kg9316/RUT200_Bacnet_Mqtt_Pump/actions/runs/34463555870
Source commit: `07d53a9d2d48796193caf9f055ad507740e66f02`
Package version: `1.0.0-40`
Bundle SHA-256: `68e9d45772ea12eecae899767427a298bd0282e902cc33e26a446178d0565a30`

Passed:

- Full RUT2M SDK build, VuCI preparation, gateway link, UI build and Package Manager bundle generation.
- C, Lua and UI regression tests on the GitHub Actions Linux runner.
- Downloaded artifact inspection: MIPS32 little-endian executable, required dynamic
  libraries and ABI package dependencies, empty license, empty initial GUID registry,
  and both configuration files marked for preservation on upgrade.

- Live HTTPS token request with the supplied controller credentials: token lifetime 3600 seconds.
- Live MQTT 3.1.1 CONNECT using the guide's client ID, username and token password:
  broker accepted; TLS 1.3 certificate/hostname validation succeeded. No PUBLISH sent.
- The actual `gk_cloud.c` token client authenticated against GK using libcurl/OpenSSL.
  Windows Schannel initially failed in the sandbox; selecting the OpenSSL DLL resolved
  the test-environment issue. The RUT2M build selects OpenSSL.
- Native C regression tests: unique/stable UUIDv4 tags, persistence across reopening,
  rejection of damaged registries, number/enum/digital/string JSON, string escaping,
  metadata omission, token renewal/expiry and oversized-response rejection.
- MQTT unit tests with mocked libmosquitto: correct GK identity/password, verified TLS
  configuration, reconnection with renewed tokens, disconnection on expiry, refusal
  to connect when TLS setup fails, and generic MQTT mode.
- Lua 5.1 API tests: license redaction and preservation, realistic long licenses,
  oversized-value rejection, controller-ID and certificate-pair validation.
- Node component-script tests: mode-specific fields, clearing the license input
  after saving, and handling unsuccessful save responses.
- Object compilation of all eight changed/new production C translation units for
  MIPS little-endian Linux/musl using Zig/Clang and upstream library headers.
- Workflow YAML parsing, Fast Build cache rules and source checks for credentials.

Not yet verified:

- VuCI rendering and API integration on the device.
- Physical BACnet discovery/polling, router filesystem durability and package-upgrade
  preservation of GUIDs. Windows host tests adapt POSIX operations to Win32.
- End-to-end tag publication into GK Cloud. Connection tests deliberately sent no values.

Use the `Build GK Cloud TLS package` workflow for the complete build chain.
Prepared SDK and VuCI caches are reused; the fast package workflow is also available separately.
No credentials are embedded in the source or package configuration.
