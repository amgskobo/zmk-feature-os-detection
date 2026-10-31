# Override failure and profile boundaries

Run `python3 tests/override-boundary/run.py` from a resolved west workspace,
or set `NANOPB_SOURCE` to an already inspected nanopb checkout. Optimized and
ASan/UBSan builds compile the complete production core, settings adapter and
RPC handler with the real generated schema and nanopb codec. Endpoint, event
registration and Custom Settings APIs are controlled boundaries.

The settings substitute retains a new RAM value before returning a save error,
matching the selected owner's independently tested contract. Assertions cover
error propagation, exactly one effective-OS notification, unchanged-value/retry
deduplication, invalid OS/profile input, inactive transport/profile isolation,
AUTO fallback and full-width request indices including `0x80000000` and
`0xffffff00`. Invalid requests must neither succeed nor alter another profile.
The work queue is run inline: every `zmk_os_changed` must be raised from it,
never from the calling (Studio RPC or Bluetooth) thread, and RPC errors carry
their own messages, including an override applied in RAM but not saved. An
override written past this RPC (generic Custom Settings) raises the event too,
and so does settings-initialized for an override the boot load applied without
a change event. The subscription stub defines one symbol per subscribed event,
so dropping a subscription the harness relies on fails its build.

The real `os_detection_ble.c` runs on controlled Bluetooth callbacks: a bonded
host is fingerprinted, a new host paired into the same profile is fingerprinted
afresh although `connected` cannot map it yet (ZMK records its address only at
`pairing_complete`), a reused connection object after a bond cleared while
connected starts afresh too, and the cached guess applied on reconnect is not
written back.

This is policy/codec evidence, not actual NVS interruption, kernel scheduling,
USB/BLE delivery or fingerprint accuracy. The existing native test/studio
fixtures and downstream production builds remain separate checks.
