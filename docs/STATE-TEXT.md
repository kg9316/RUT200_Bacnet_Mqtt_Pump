State text metadata (release 51)
================================

Binary input/output/value points read Inactive_Text and Active_Text. The compact
point field `s` maps string keys `0` and `1` to the returned labels.

Multistate input/output/value points read State_Text array length at index zero,
then each label individually. `s` uses the BACnet state number (starting at `1`).
Array lengths above 65535 are rejected. Labels must fit an unsegmented response.

Example point entry:
`{"t":"existing-guid","n":"Fan","u":"","d":"","s":{"1":"Off","2":"Low","3":"High"}}`

Labels are stored only in the existing tags.json registry and included unchanged
in the download export. State values remain in MQTT, not in persistent storage.
Missing/unsupported labels are omitted; previously cached labels survive read
failures. The mapping can be partial. Each metadata read yields to other devices
and value polling. Names/labels are refreshed on service startup. Changes use the
existing batched metadata flush, avoiding writes when text is unchanged.

Legacy GUID-only entries and compact point entries remain readable without
reissuing IDs. Existing GK Live imports ignore extra fields and keep working.
