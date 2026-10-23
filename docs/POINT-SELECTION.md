# Manual BACnet devices and point selection (release 58)

The limit remains 10,000 points total, 10,000 per device, and 500 devices.

Manual endpoints are direct IPv4 BACnet/IP addresses with a UDP port (default 47808).
The gateway sends directed Who-Is requests, learns the Device ID from I-Am, and
uses the normal Object List / metadata discovery pipeline. An optional Device ID
allows direct reads without I-Am. Offline endpoints remain stored. All automatic
and manual discovery shares the Device ID table and point GUID registry.
A routed MS/TP device requires a routed BACnet address; entering its router's IP
and the downstream Device ID does not configure that route.

Manual discovery is spread at up to ten endpoints per second, followed by the
configured discovery interval. Known manual devices share the existing globally
bounded offline recovery probes. There are no per-point or per-device timers.
Removing a manual endpoint preserves existing point identities and metadata.

Open Points for a device to search name, description or numeric object ID (type:instance),
filter object type and polling state, and retrieve pages of at most 100 rows.
Search is ASCII case-insensitive; non-ASCII text is matched as entered.
Select a page, individual points, or all matches across pages. The applied filter,
not an unsaved search edit, determines the bulk selection. New matches discovered
before a bulk operation is applied are included in an all-matches operation.

Disabling polling excludes the point from new RP/RPM value requests and MQTT
publication. Metadata discovery continues. Already transmitted requests/messages
cannot be recalled. Re-enabling publishes on the next successful read, subject
to connection availability. The per-device new-point policy applies only to new
identities, never to restored or previously discovered points.

All settings live in tags.json. Existing address-to-GUID strings and enriched
entries remain readable. Point flag x is a boolean (true = polling disabled;
absent = enabled). Reserved _cfg contains m (manual endpoint array with ip, p,
optional id and n) and n (Device ID to boolean automatic-enable policy).
Exports include _cfg and x; imports preserve them and reject malformed settings.
Older external readers should ignore reserved keys; older importers may discard
these new controls. GUID addresses, names, units, descriptions and state texts
retain their existing representation. Values are never stored by these controls.

API commands are queued atomically through the existing daemon-owned import
channel. The UI waits for the matching request ID and reports persistence errors.
Point listing scans daemon memory and returns only the requested page; it does
not load the entire registry in the API worker or browser. Full export still
returns the complete registry, with the existing 2 MB import limit unchanged.

Device removal
--------------
The device table uses Teltonika checkboxes to select devices. Delete removes
all local points, GUID mappings, the device name, automatic-enable policy and
any manual endpoint associated with that Device ID. The confirmation lists the
selected IDs. Cloud history is not deleted. An active BACnet transaction is
cancelled before freeing memory; queued MQTT messages are discarded by reconnecting.
Other devices retain their GUIDs. A device that responds to later discovery
will return with newly generated GUIDs. Export before deletion if restoration
with the original GUIDs may be needed. Multi-device deletion is sequential;
on failure, already deleted devices stay deleted and remaining selections stay.
