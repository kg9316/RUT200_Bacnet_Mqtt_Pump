# BACnet scheduling and availability (1.0.0-48)

The gateway uses one outstanding BACnet transaction globally. Device, discovery,
metadata and point cursors rotate after each attempted operation. A failed point
never restarts the global queue. BACnet-stack TSM drives retry/timeout using elapsed
monotonic time (3-second APDU timeout, one configured retry). Every completion,
error, cancellation and watchdog path releases the invoke ID. The extra watchdog
is bounded at three APDU timeout periods plus one second.

Three consecutive failed transactions mark a device offline. Probes wait 10, 30,
then 60 seconds. An addressed BACnet reply clears the offline state. BACnet property
errors indicate communication, not a dead device. Individual points retry after
10, 30, then 60 seconds. Optional metadata errors advance without clearing cached
text. Missing discovery responses defer discovery while other devices continue.

RPM reads up to 30 Present_Value properties per device, limited conservatively by
APDU size. Numeric results use a 20-byte budget including common error results.
String and Schedule outputs use single reads. Size/reject/malformed/group-timeout
failures halve the group for a subsequent turn; unrecognized RPM falls back to RP.
Successful values in a partial response are retained; failed/missing results back
off individually. The RPM parser is bounded by packet length and requested objects,
uses no response-driven heap allocation and rejects mismatched/duplicate results.
No segmentation is requested. One bad property does not disable RPM for every device.

Allowed types: Analog Input/Output/Value; Binary Input/Output/Value; Multistate
Input/Output/Value; CharacterString Value; Calendar and Schedule. Only Present_Value
is polled from Calendar/Schedule. There are no standard CharacterString Input/Output
object types. Unsupported proprietary string types are not guessed.

Known devices and allowed points are restored from the existing tags.json keys.
Legacy GUID strings and compact metadata entries remain supported; restore does not
write identities or measurements. Reachability starts unknown on every restart.
Device names are rediscovered; the existing registry does not persist device names.
RutOS displays device rows with its native tlt-table and tlt-badge components, including
last response and retry countdown. Point details remain download-only.

Validation: deterministic fault tests for device fairness, retries, recovery,
partial RPM errors, truncated packets and allowed types; existing GUID, MQTT,
status/API/UI checks; MIPS cross compilation. No on-router stress test.
