# Historical Windows BOS enumeration failure

The 2026-07-05 investigation found that enabling USB detection selected BOS and
advertised USB 2.01 while the legacy stack had no registered BOS capability.
The device returned `05 0f 00 00 00`: a total length below the five-byte header
and zero capabilities. The original Windows trace stopped after BOS and retried
without reaching configuration. It was a failed enumeration, not evidence that
Windows normally avoids string descriptors.

## Current repair and classification consequence

`src/os_detection_usb.c` registers a USB 2.0 Extension capability with
`bmAttributes=0` at POST_KERNEL, before ZMK's APPLICATION USB initialization.
The historical direct Linux-host control read after the repair returned
`05 0f 0c 00 01 07 10 02 00 00 00 00`: total length 12, one capability and no LPM
claim. The Windows trace then reached SET_CONFIGURATION and HID REPORT reads.

The successful Windows trace also read string descriptors at length 255, like
Linux. The classifier therefore checks BOS length 255 before the Linux string
rule. Linux and the later Mac/iPhone recapture read BOS as 5 then 12. Current
rules and their finite evidence are in [fingerprints.md](fingerprints.md).

## Evidence limits that remain visible

The historical Windows Device Manager text/error code and a separate driver-cache
negative control were not retained. Reaching SET_CONFIGURATION and requesting
HID REPORT is USB protocol evidence; an explicit matching host HID typing trace
was not retained. Mac/iPhone USB were recaptured on 2026-07-19, superseding the
original plan's statement that they were unavailable. These dated observations
do not establish physical acceptance of today's checkout.

Use the [current owner matrix](../tests/README.md) for software checks and the
[current capture procedure](hw-capture-recipe.md) for fresh board evidence.
The old unexecuted investigation phases, dependency-patching fallback and
rig-specific flash/erase instructions are retired. Full original source,
hypotheses, raw sequences and unresolved notes remain byte-for-byte in
`$OS_DETECTION_HISTORY_ARCHIVE`, entry
`docs/windows-usb-enumeration-issue.md`, with SHA-256 in its archive manifest.
