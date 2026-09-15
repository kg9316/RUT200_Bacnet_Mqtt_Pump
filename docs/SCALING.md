# Capacity and recovery scheduling

The gateway accepts up to 500 devices and 10,000 points **in total**, also
enforced on JSON imports. Existing registry files are not truncated or rewritten
to remove entries above that limit; excess entries cannot be loaded for polling.

Point lookup uses a fixed-size hash index containing array indices, which remain
valid when the point arrays grow. The index adds about 512 KiB on MIPS. Completed
metadata is copied to the registry once, and fully completed devices stop scanning
their metadata lists. An empty value scan is deferred for 100 ms before retrying.

WhoIs/I-Am detects network presence; a successful read detects reading capability.
I-Am updates presence without clearing read failures. An offline device without
an I-Am in the last three discovery intervals is not probed. It becomes eligible
again after responding to discovery, subject to its backoff and the recovery budget.

After the first read timeout, subsequent work on that device is a Device Name
recovery read. All suspect/offline devices share a recovery budget: at most one
attempt starts per `10 * read timeout + 1 second` (31 seconds by default). These
attempts wait at most one read-timeout window (3 seconds by default). Normal reads
still use the BACnet stack retry policy: a first unexpected failure can delay
other reads by about 6 seconds. Many simultaneously failing online devices can
therefore still cause initial delays. MQTT processing continues during BACnet waits.
Recovery can be slower when many devices compete for the budget.

New GUIDs and metadata are saved together, at most once per 30 seconds, through
the existing atomic file replacement and file/directory fsync. New GUIDs cannot
be published before that succeeds. A failed save leaves them unpublished and
retries later with the same in-memory identities. A power loss before saving can
discard an **unpublished** identity; previously saved/published GUIDs are preserved.
The JSON schema and legacy string-entry compatibility are unchanged. Measurements
are not written to the registry. First publication of a new point can be delayed
by the batching interval plus its next successful value poll.

Host regression tests cover recovery throttling, stack timeout/retry handling,
100-device/10,000-point identity lookup and limits, batched saves, save failure,
restart persistence, legacy files and imports. These are not a throughput or
memory benchmark on the router, and do not guarantee a particular polling period.
