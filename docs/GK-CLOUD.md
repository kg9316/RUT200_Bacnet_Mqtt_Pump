# GK Cloud MQTT

Select `gk_cloud` in the gateway WebUI and enter the controller ID and license.
The license input is masked. Saving an empty license preserves the saved value;
the API returns only whether a license is configured, never the license itself.
No deployment credentials are included in this repository or the package.

In this mode the gateway uses:

- Token endpoint: `https://login.gkcloud.no/v2/token`
- Form fields: `grant_type=client_credentials`, `client_id=controller:<CONTROLLER_ID>`,
  `client_secret=<LICENSE>`, with URL encoding applied to both credentials.
- Broker: `edge-broker.gkcloud.no`, port `8883`, verified TLS.
- MQTT client ID: `c/<CONTROLLER_ID>/stream/Connector`
- Username: controller ID; password: access token.

Tokens stay in process memory. Renewal starts before expiry (60 seconds early
for a one-hour token); the gateway reconnects using the new token. An expired
token stops MQTT publishing until a new token is available. Failed token requests
retry at 30-second intervals. HTTPS requests use libcurl's multi interface, a
20-second timeout, no redirects, certificate/hostname verification and TLS 1.2
or later. MQTT also requires certificate/hostname verification and TLS 1.2 or
later. There is no automatic fallback to plaintext.

The default trust store is `/etc/ssl/certs/ca-certificates.crt`, installed by
`ca-bundle`. Set the CA path only when a different PEM trust bundle is needed.
The router clock must be correct for certificate validation.

## GUID tags and metadata

Every BACnet `(device instance, object type, object instance)` gets a random
UUIDv4/GUID. GUIDs are checked for local duplicates before being persisted.
They are independent of BACnet names and remain stable after restart, reconnect,
point renaming and package upgrade. MQTT publishes to:

```text
c/<CONTROLLER_ID>/t/<GUID>
```

Only these fields are sent:

```json
{"timestamp":1788180633795,"dataType":"number","value":1.23}
```

Strings use `valueText`; enums use integer `value`; digital values use boolean
`value`. Binary BACnet objects are digital; other enumerated values are enums.
Timestamps are Unix milliseconds. The optional `name`, `description` and `unit`
fields are deliberately omitted, and the old retained metadata topics are not
published in GK Cloud mode. String values are JSON-escaped.

The durable mapping is `/etc/gk-bacnet-mqtt/tags.json`. This file contains only
BACnet keys and GUIDs; a later REST/database service can associate the same GUIDs
with names, descriptions and units. That REST service/database is not implemented.

New IDs are saved with an atomic file replacement and fsync before publication.
The file is registered as an opkg conffile so upgrades preserve it. Back it up
when replacing a router or performing a factory reset. A missing/corrupt registry
or a persistence failure prevents new GUID publication instead of silently
changing identities. Restore the registry and restart the service after such a
failure. Do not copy one registry to multiple independent gateways: that would
reuse its existing GUIDs.

## Generic MQTT

Existing installations default to `generic`, preserving the previous topic
layout. TLS is optional in this mode. Enable `mqtt_tls`, set the broker's TLS
port, and use the system CA bundle or a custom CA PEM file. Optional client
certificate/private key PEM paths must be supplied together. Files must already
exist on the router; certific…403 tokens truncated…tched at most once per 30 seconds; unchanged
metadata does not rewrite flash. New GUIDs remain durable before publication.
The small temporary runtime status file contains only service status and counters,
without point metadata or values. Export reads the persistent registry on demand.
Package version in the UI comes directly from the installed opkg package metadata.
Dropdowns use RutOS's native tlt-select component and follow the selected theme.

The registration limits are 500 BACnet devices and 10,000 points per device.
Point arrays grow dynamically; empty devices do not reserve point storage.
These are independent upper bounds, not a guarantee that 5 million points fit in RAM.
The BACnet address cache is also compiled for 500 devices.
