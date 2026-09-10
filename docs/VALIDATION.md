# Validation – 10 September 2026

Implemented locally; not pushed or deployed.

Passed:

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

- Full link and IPK generation with the exact Teltonika RUT2M SDK/cache v2.
- VuCI rendering and API integration on the device.
- Physical BACnet discovery/polling, router filesystem durability and package-upgrade
  preservation of GUIDs. Windows host tests adapt POSIX operations to Win32.
- End-to-end tag publication into GK Cloud. Connection tests deliberately sent no values.

Next device build: workflow 0, prepare caches with 1 and 2, then build with 3.
No credentials are embedded in the source or package configuration.
