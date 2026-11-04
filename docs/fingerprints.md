# Current host fingerprint contract

This guide describes the current production rules and their evidence limits.
The module observes USB requests and BLE GATT discovery; it does not query an
operating-system identity. DYA is the supported core. Detection belongs to the
host-connected central or standalone role.

## USB rules, in evaluation order

The observer in `src/os_detection_usb.c` tracks string descriptor lengths and the
last BOS descriptor length. Only the request direction and descriptor kinds
used by that observer contribute; other requests do not restart its debounce.
SET_ADDRESS starts a new enumeration and clears the statistics. Disconnect
cancels pending settle work and clears them. A quiet interval of
`CONFIG_ZMK_OS_DETECTION_USB_SETTLE_MS` triggers classification.

| Observation | Result |
| --- | --- |
| No string request and no BOS request | UNKNOWN |
| A string header read at length 2 and a string read at another length | MACOS |
| Otherwise, BOS last requested at length 255 | WINDOWS |
| Otherwise, a string read at length 255 without a length 2 probe | LINUX |
| Other or incomplete patterns | UNKNOWN |

All four 16-bit string counters saturate at 65,535. Counts are presence evidence,
not an unbounded request total: repeated valid reads cannot erase an earlier
signal by wrapping to zero. Enumeration reset still clears them. This does not
change the rule order, ABI or debounce interval.

The module registers a USB 2.0 Extension BOS capability with no LPM support at
POST_KERNEL, before ZMK enables USB at APPLICATION. This makes its BOS length
and capability count valid. Malformed-BOS behavior and the acceptance boundary
are described in [the BOS note](windows-usb-enumeration-issue.md).

## BLE rules, in evaluation order

`src/os_detection_ble.c` keeps separate discovery statistics and settle work per
BLE profile. Relevant reads restart the per-profile debounce until its first
settle. Counters saturate at 255. Unrelated reads, such as battery polling, do not
extend the discovery window. After settle, reads are ignored until reconnect.
A first pairing that acquires its profile mapping after connection starts its
profile statistics on the first relevant read.

A callback already running at reconnect cannot consume the new profile's
statistics before its own quiet deadline. Session replacement and result/cache
publication are serialized per profile; other profiles remain independent.

| Observation | Result |
| --- | --- |
| No Report Map, HIDS Info, PnP ID or Appearance reads | UNKNOWN |
| PnP ID and Appearance were read | WINDOWS |
| Otherwise, Report Map and HIDS Info were read, with no Appearance read | ANDROID |
| Other nonempty patterns | MACOS |

Automatic BLE detection has no LINUX or IOS return path. A desktop Linux read
set can collide with Windows, and an Apple-shaped read set falls back to macOS.
No ANCS/AMS discovery probe is implemented. LINUX and IOS remain valid
manual profile overrides. MTU, connection interval and Report reads are recorded
but are not classification discriminators.

The previous detected guess is cached per profile and adopted immediately on
reconnect; a fresh settled result can later replace it. A manual override takes
precedence. Invalid persisted/imported enum values read as UNKNOWN without
rewriting the imported value. Cache persistence failure, notification behavior,
profile independence and retry are exercised separately from actual Flash.

## Controlled pattern examples

These sanitized examples preserve request values and classification boundaries.
They describe reproducible inputs, not raw capture transcripts or newly executed
hardware results. The original individual capture records remain private.

| Request pattern | Classification boundary |
| --- | --- |
| Windows-like USB: DEVICE 64 then 18; CONFIGURATION 255; BOS 255; STRING 255; DEVICE_QUALIFIER 10 once; SET_CONFIGURATION and HID REPORT | BOS 255 selects WINDOWS after the Apple-string rule. Failed enumeration without strings must not become a separate signature. Enumeration does not establish host HID arrivals. |
| Linux-like USB: DEVICE 64 then 18; BOS 5 then 12; DEVICE_QUALIFIER 10 three times; CONFIGURATION 9 then 34; STRING 255 | Selects LINUX. An Android OTG host with this shape receives the same result. Optional trailing length-254 string reads are not an adopted discriminator. |
| Apple-shaped USB: DEVICE 8 then 18; STRING 2 then 24/34; CONFIGURATION 9 then 34; BOS 5 then 12; REMOTE_WAKEUP after configuration | Selects MACOS for either macOS or iOS. REMOTE_WAKEUP and a late optional string read do not distinguish them. |
| Desktop BLE: Report Map, HIDS Info, PnP ID and Appearance | Selects WINDOWS, including a Linux/BlueZ host with this set. Read counts and connection parameters are not discriminators. |
| Android-shaped BLE: Report Map, HIDS Info and PnP ID, with no Appearance read | Selects ANDROID. A different host skipping Appearance can collide with it. |
| Other nonempty BLE: DIS/PnP ID alone, or Report Map without HIDS Info/PnP ID/Appearance | Uses the MACOS fallback without establishing a unique Apple OS identity. Address privacy is not an adopted signal. |

OS versions, cached discovery, hubs/KVMs that avoid enumeration, reconnect timing
and optional late requests can change these patterns. No general classifier
accuracy percentage is established. Manual overrides remain the way to select a
specific BLE OS when inference is ambiguous.

## Current verification and open boundaries

Run the entries in [the owner tests](../tests/README.md). The USB injected native
fixture preserves the exact Windows/Mac/Linux/Apple-remote-wakeup scenarios.
`tests/usb-boundary/` compiles the complete production USB source with controlled
kernel/USB APIs and checks 65,535/65,536/longer presence, ignored requests, reset
and independent classifications under optimization and ASan/UBSan.
`tests/override-boundary/` compiles complete core/settings/BLE/RPC sources with
real nanopb and controlled APIs. Board/native checks cover supported feature
variants and actual DYA registration. Web assertions live in `tests/web/`.

These checks do not establish current physical enumeration, radio delivery,
reconnect timing, persistent storage or host HID input. Follow
[the capture procedure](hw-capture-recipe.md) when collecting fresh hardware
evidence. `tests/ble-session/` separately exercises reconnect before an old
callback's snapshot, a new debounce armed during that callback, and a concurrent
disconnect during publication using the real kernel workqueue and mutexes.
Its Bluetooth peers and report/cache sinks remain controlled.

## Archived superseded material

The original chronological guide, including all raw request sequences, rejected
hypotheses and dated caveats, is retained privately in
`$OS_DETECTION_HISTORY_ARCHIVE`, entry
`docs/fingerprints.md`. Its adjacent archive manifest records SHA-256 values.
`OS_DETECTION_HISTORY_ARCHIVE` identifies the privately retained archive;
its exact path and original identity manifest remain in local records.
The obsolete iOS/probe rules, old fixture locations, pre-debounce BLE diagnosis
and sandbox-specific cleanup instructions were removed from active guidance.
