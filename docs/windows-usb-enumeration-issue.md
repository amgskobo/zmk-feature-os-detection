# BOS validity and enumeration boundary

Advertising USB 2.01 with BOS enabled requires a valid BOS response. The invalid
byte sequence `05 0f 00 00 00` declares a total length below the five-byte header
and zero capabilities. A host may stop or retry without reaching configuration;
that failure must not become evidence that its OS avoids string descriptors.

## Current repair and classification consequence

`src/os_detection_usb.c` registers a USB 2.0 Extension capability with
`bmAttributes=0` at POST_KERNEL, before ZMK's APPLICATION USB initialization.
The expected descriptor bytes are `05 0f 0c 00 01 07 10 02 00 00 00 00`: total
length 12, one capability and no LPM claim. Compare actual output from a selected
artifact with those bytes during separately authorized hardware acceptance.

The controlled Windows-like and Linux-like sequences both read string
descriptors at length 255. The classifier therefore checks BOS length 255
before the Linux string rule. Alternative fixtures read BOS as 5 then 12.
Current rules and their limits are in [fingerprints.md](fingerprints.md).

## Evidence limits that remain visible

Reaching SET_CONFIGURATION and requesting HID REPORT is USB protocol evidence;
it does not establish host HID typing delivery. Physical acceptance needs
matching source/artifact identities, actual descriptor bytes, host diagnostics
and any driver-cache control stated by the procedure. The controlled software
fixtures do not supply those hardware results.

Use the [current owner matrix](../tests/README.md) for software checks and the
[current capture procedure](hw-capture-recipe.md) for fresh board evidence.
Local investigation phases and device-specific flash/debug procedures are not
active instructions. Full original source, hypotheses, raw sequences and
unresolved notes remain private in
`$OS_DETECTION_HISTORY_ARCHIVE`, entry
`docs/windows-usb-enumeration-issue.md`, with SHA-256 in its archive manifest.
