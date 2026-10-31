# Collecting fresh host fingerprint evidence

Start from the current [rules and evidence limits](fingerprints.md) and
[owner test contract](../tests/README.md). The historical July rig commands are
archived; its partition offsets, old dependency paths, debug addresses and
cache-cleanup workarounds are not current build instructions.

## Identify the selected build and host

Record the actual resolved west manifest and checkout revisions, local overrides,
full owner source identity/diff, selected board and central/peripheral role,
Kconfig, devicetree, toolchain and complete artifact hashes. Retain the ELF and
its matching build output. Record the host OS/version, transport, hub/KVM route,
profile and bond/discovery-cache starting conditions. A sibling checkout or a
short build tag does not identify the firmware on the board.

Build through the selected configuration owner's documented production path,
with all required features enabled. Flashing, resetting or changing bond/storage
state is a separate device action that needs the applicable authorization;
preparing or inspecting an artifact does not perform that action.

## Capture the observation and its effect separately

For USB, retain the raw ordered SETUP transactions from a real host USB capture
or an independently reviewed device capture. Include descriptor type/index,
direction, wLength, response lengths/content where needed, SET_ADDRESS,
SET_CONFIGURATION and the post-configuration window. Capture through late
REMOTE_WAKEUP and optional string requests; stopping at configuration previously
created a false Mac/iPhone distinction. Inspect the actual BOS bytes as well as
whether enumeration completes.

For BLE, retain the ordered relevant GATT reads, discovery/cache conditions,
connection/disconnection and settle boundaries, profile attribution and the
reported OS/override. Current production debug output reports settled aggregate
statistics; it does not retain a complete per-packet USB or GATT trace. Do not
label aggregate counters as a complete discovery transcript. A new capture probe
must be reviewed for timing and logging effects before using its evidence.

When RTT is used, derive symbols and buffer addresses from the exact ELF and
inspect the live control block. Decode its real read/write offsets and circular
wrap in order. A stale block, overwritten buffer, dropped logs or attachment
error makes the capture incomplete. Hardcoded addresses and unordered `strings`
output are insufficient evidence of an ordered trace.

Record the classifier result, selected effective OS and any generated event
independently from actual host input arrival. Pair matching device observations
with a host HID trace before claiming working keyboard delivery. Preserve failed
or partial retrieval and unexpected resets rather than treating silence as
success. Repeat matching conditions when comparing changes.

## Keep acceptance boundaries explicit

Software owner/native/board/Web results, successful USB enumeration, physical
BLE discovery, HID delivery, reconnect behavior, storage retention, power and
reset behavior are separate outcomes. Mark each PASS, FAIL, BLOCKED or NOT RUN
using its own evidence. Existing historical observations are not fresh evidence
for a changed source/artifact/configuration.

The complete old rig procedure is preserved byte-for-byte in
`$OS_DETECTION_HISTORY_ARCHIVE`, entry
`docs/hw-capture-recipe.md`, with SHA-256 in the archive manifest. It includes the
original direct-flash partition workaround and debug-memory operations for
historical review; no current test depends on those commands.
