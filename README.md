# zmk-feature-os-detection

## Reviewed fork

This fork's reviewed source is on `codex/os-detection-review` in
[`amgskobo/zmk-feature-os-detection`](https://github.com/amgskobo/zmk-feature-os-detection/tree/codex/os-detection-review).
Select that repository URL and revision in the module's west project entry.
Keep the module's documented imports enabled where required. The original
upstream installation examples below describe the upstream repository.

The dependency manifests select the available Custom Settings revision
`c4071b095c3007d1deb055c609603ca9b5897d14` from its reviewed fork;
production imports and isolated tests use the same registry implementation.

![ZMK Version](https://img.shields.io/badge/ZMK-cormoran%20fork-blue)
[![Test](https://github.com/cormoran/zmk-feature-os-detection/actions/workflows/zmk-module.yml/badge.svg?branch=main)](https://github.com/cormoran/zmk-feature-os-detection/actions/workflows/zmk-module.yml)

A ZMK module that detects the host OS (Windows / macOS·iOS / Linux / unknown)
over **USB** and **BLE**, raises a `zmk_os_changed` event, and exposes live
state + per-BLE-profile manual overrides through a Custom Studio RPC
subsystem and Web UI. It can optionally auto-switch a keymap layer per
detected OS too, though for that use case we recommend
[zmk-feature-default-layer](https://github.com/cormoran/zmk-feature-default-layer)
instead - see [Layer auto-switch](#layer-auto-switch-opt-in-not-recommended) below.

This uses the **unofficial** custom ZMK Studio RPC protocol from
[cormoran/zmk-module-template-with-custom-studio-rpc](https://github.com/cormoran/zmk-module-template-with-custom-studio-rpc).
License: MIT. This module does not reference or reuse any QMK (GPL-2.0)
source code.

## How detection works (short version)

- **USB**: a linker-level hook (`--wrap=usb_handle_bos`) observes every
  standard USB SETUP packet during enumeration - which `GET_DESCRIPTOR`
  requests are sent, at what `wLength`, and whether `GET_DESCRIPTOR(BOS)`
  is requested - without changing anything the host actually receives.
  After a short debounce once enumeration settles, the pattern is
  classified.
- **BLE**: `bt_gatt_authorization_cb_register()` observes which GATT
  characteristics the host reads while pairing/reconnecting (HID Report
  Map, HID Info, DIS PnP ID, GAP Appearance), plus ATT MTU and the initial
  connection interval - always authorizing the read, never blocking it.
- Detection results and any manual BLE override are combined into one
  **effective OS** per transport (override wins), and the effective OS
  for whichever endpoint is currently active is what
  `zmk_os_detection_current()` and the `zmk_os_changed` event report.

**Read [docs/fingerprints.md](docs/fingerprints.md) before relying on this
in production.** That guide defines the ordered USB/BLE rules, controlled
fixture patterns and verification limits. They do not establish general
accuracy across OS versions or physical acceptance of current firmware.
Manual BLE overrides remain necessary for ambiguous hosts.

## Installing

Add this module and the patched ZMK fork it depends on to your
`config/west.yml`:

```yaml
manifest:
  remotes:
    - name: cormoran
      url-base: https://github.com/cormoran
  projects:
    - name: zmk-feature-os-detection
      remote: cormoran
      revision: main
      import: true
    # Required: patched ZMK with Custom Studio RPC support
    - name: zmk
      remote: cormoran
      revision: main+custom-studio-protocol
      import:
        file: app/west.yml
```

`import: true` on `zmk-feature-os-detection` also pulls in its own
dependency, [zmk-feature-custom-settings](https://github.com/cormoran/zmk-feature-custom-settings)
(needed for BLE per-profile override persistence).

## Kconfig

Enable in your `config/<shield>.conf`:

```conf
CONFIG_ZMK_OS_DETECTION=y

# Pick the transport(s) you want detection on. Each requires the matching
# ZMK transport (ZMK_USB / ZMK_BLE) to already be enabled.
CONFIG_ZMK_OS_DETECTION_USB=y
CONFIG_ZMK_OS_DETECTION_BLE=y

# Optional: Custom Studio RPC + Web UI for live state and BLE overrides
CONFIG_ZMK_STUDIO=y
CONFIG_ZMK_OS_DETECTION_STUDIO_RPC=y
CONFIG_ZMK_CUSTOM_SETTINGS=y
CONFIG_ZMK_CUSTOM_SETTINGS_STUDIO_RPC=y
CONFIG_ZMK_STUDIO_RPC_RX_BUF_SIZE=128
CONFIG_ZMK_LOW_PRIORITY_THREAD_STACK_SIZE=2048
```

Full option list is in [Kconfig](./Kconfig).

## C API

```c
#include <cormoran/os-detection/os_detection.h>

enum zmk_os { ZMK_OS_UNKNOWN, ZMK_OS_WINDOWS, ZMK_OS_MACOS, ZMK_OS_LINUX, ZMK_OS_IOS, ZMK_OS_ANDROID };

enum zmk_os zmk_os_detection_current(void); // effective OS for the active endpoint

// Subscribe to zmk_os_changed via ZMK_LISTENER/ZMK_SUBSCRIPTION to react to changes.
```

## Layer auto-switch (opt-in, not recommended)

This module can optionally auto-activate a keymap layer per detected OS by
listening to its own `zmk_os_changed` event. **This overlaps with
[zmk-feature-default-layer](https://github.com/cormoran/zmk-feature-default-layer),
which we recommend instead** - it's a dedicated, more capable module for
per-host default layer selection. Use this module's built-in switch only if
you specifically want it driven by this module's OS detection alone, with
no extra module dependency.

It's off by default and must be explicitly enabled:

```conf
CONFIG_ZMK_OS_DETECTION_LAYER_AUTO_SWITCH=y

# Layer to auto-activate per detected OS (-1 = disabled, default)
CONFIG_ZMK_OS_DETECTION_LAYER_WINDOWS=1
CONFIG_ZMK_OS_DETECTION_LAYER_MACOS=2
CONFIG_ZMK_OS_DETECTION_LAYER_LINUX=3
CONFIG_ZMK_OS_DETECTION_LAYER_IOS=4
CONFIG_ZMK_OS_DETECTION_LAYER_ANDROID=5
CONFIG_ZMK_OS_DETECTION_LAYER_UNKNOWN=-1
```

## Web UI

See [web/README.md](./web/README.md) for local web development.

1. Connect Studio to your keyboard (Web Serial - Chrome/Edge only) at the
   published UI URL: https://cormoran.github.io/zmk-feature-os-detection/
2. The **USB** card shows whether USB is connected and the detected OS.
3. The **BLE Profiles** table lists every paired profile (bonded /
   connected / detected / override / effective), with the currently active
   profile highlighted. Use the dropdown in the **Override** column to pin
   a profile to Windows/macOS/Linux, or back to **AUTO**. The override is
   saved to the keyboard's persistent settings immediately and survives
   reboots.

**Publishing**: merging to `main` builds and deploys the Web UI to GitHub
Pages automatically (see `.github/workflows/web-ui.yml`).

## Known limitations

- **USB detection requires a valid BOS capability.** With
  `CONFIG_USB_DEVICE_BOS`, `bcdUSB` 2.01 advertises BOS support. A response
  with `wTotalLength=0` is shorter than the 5-byte header and invalid; failure
  to reach configuration cannot establish an OS signature. The module
  registers its USB 2.0 Extension capability before USB initialization.
  Use a revision containing that registration. See
  [docs/fingerprints.md](docs/fingerprints.md) and
  [docs/windows-usb-enumeration-issue.md](docs/windows-usb-enumeration-issue.md)
  for the full root-cause writeup.
- **USB fingerprints are OS-version-dependent.** The `wLength` pattern a
  given Windows/macOS/Linux version requests can change between OS
  releases; treat the classifier as a best-effort guess, not a guarantee.
- **Shared USB request patterns produce the same OS value.** A Linux-shaped
  pattern maps to `ZMK_OS_LINUX`, including a ChromeOS or Android USB-host/OTG
  connection that uses that shape. There is no separate ChromeOS value in
  `enum zmk_os`. `ZMK_OS_ANDROID` is an automatic BLE result only: Report Map
  and HIDS Info without GAP Appearance match that branch. It is a heuristic,
  not proof that another host cannot produce the same read set.
- **macOS and iOS are NOT distinguished over USB; both report `ZMK_OS_MACOS`.**
  The short-string-probe/full-read shape maps to MACOS.
  `SET_FEATURE(DEVICE_REMOTE_WAKEUP)`, including after configuration, does not
  split iOS from macOS. BLE also uses MACOS as its nonempty fallback. No
  ANCS/AMS probe is implemented; `ZMK_OS_IOS` remains available as a manual
  BLE profile override. See [the rule contract](docs/fingerprints.md).
- **KVM switches and some USB hubs don't force re-enumeration** on switch,
  so USB detection can miss a host change until the next physical
  reconnect.
- **The USB hook requires Zephyr's legacy USB device stack**
  (`CONFIG_USB_DEVICE_STACK`). If ZMK ever migrates to the newer
  `device_next` stack, `--wrap=usb_handle_bos` stops applying and the
  build will fail loudly (a `CMakeLists.txt` check turns this into a clear
  error rather than a silently-broken feature) - the long-term fix is an
  observation hook contributed to ZMK itself
  ([zmkfirmware/zmk#2553](https://github.com/zmkfirmware/zmk/issues/2553)).
- **BLE detection is inherently heuristic.** A profile settles after
  `CONFIG_ZMK_OS_DETECTION_BLE_SETTLE_MS` without another relevant discovery
  read, then latches until reconnect. Unrelated reads such as battery polling
  do not restart that timer. Debouncing reduces transient guesses; it does
  not resolve ambiguous fingerprints. Treat the per-profile manual override
  as the reliable way to select an OS value when automatic inference is wrong.
- **BLE cannot reliably distinguish Windows from Linux.** PnP ID plus GAP
  Appearance maps to `ZMK_OS_WINDOWS`; a Linux/BlueZ host with that read set
  receives the same value. PnP ID presence alone is not a discriminator.
  Use the manual per-profile override when inference is ambiguous. See
  [the rule contract](docs/fingerprints.md).
- **Custom Studio RPC is an unofficial protocol** and requires the
  `cormoran/zmk` fork (`main+custom-studio-protocol`) rather than upstream
  ZMK.
- **Split keyboards**: detection only runs on the central side, since only
  the central owns the USB/BLE host connection.
- **BLE cannot distinguish iOS from macOS either.** Report Map alone reaches
  the MACOS fallback, and automatic classification has no IOS return path.
  The controlled software fixtures exercise these rules without establishing
  physical USB/BLE accuracy or host HID delivery. See the current BLE rules in
  [docs/fingerprints.md](docs/fingerprints.md) and the separate
  [capture procedure](docs/hw-capture-recipe.md).

## Development

`python3 tests/override-boundary/run.py` checks override failure/retry and
full-width RPC profile indices with the complete production policy/handler
sources and real nanopb. Select a resolved workspace, or set `NANOPB_SOURCE`
explicitly. See [the boundary contract](tests/override-boundary/README.md).
The check covers a changed effective OS after a failed save: the storage error
is retained, and the changed RAM value still raises one local OS event.
Unchanged values and invalid requests do not raise another event. Existing
native classifier/registration fixtures and real transport/NVS acceptance
remain separate.

The complete owner entry also runs the kernel session and array-view cases in
`tests/ble-session/` and `tests/settings-view/`. They check reconnect while old
settle work is running, result publication, and override/cache identity during
cross-thread registry activity. Each case runs three times; see
[the owner contract](tests/README.md) for selected dependencies and evidence
limits.

### Setup

```bash
git clone https://github.com/cormoran/zmk-feature-os-detection
cd zmk-feature-os-detection
west init -l west --mf west-test-isolated.yml
west update --narrow
west zephyr-export
```

(Dev container users: this is done automatically.)

### Pre-commit

```bash
pip install pre-commit
pre-commit install
pre-commit run --all-files
```

If the `prettier`/`eslint`/`jest`/`web-build` hooks fail with `Error:
Cannot find module 'node:path'`, inspect the selected Node version, `PATH`
and the hook's managed environment. Prepare a compatible isolated container
environment and a fresh selected cache before retrying
`pre-commit run --all-files`. Do not bypass required checks with a permanent
`SKIP=` workaround or delete an unrelated shared cache.

### Tests

```bash
# Unit tests (native_sim, incl. TEST_INJECT USB/RPC scenarios) + build tests
python3 -m unittest discover -s tests -p test_owner.py
# Build tests only
west zmk-build tests/zmk-config
# Unit tests only
west zmk-test tests -m .
# Web UI tests
cd web && npm test
```

### Manual real-hardware verification

The current software matrix covers classifier, protocol, feature/role and Web
boundaries. [The fingerprint guide](docs/fingerprints.md) describes the rule
contract and controlled patterns; physical acceptance of a changed checkout
or artifact requires matching device evidence.

Follow [the current capture procedure](docs/hw-capture-recipe.md) for selected
source/configuration/artifact identity, ordered USB/GATT evidence and separate
host HID/override-persistence results. Current production debug logs contain
settled statistics, not a complete per-packet trace. Superseded local procedures
remain in the private history archive rather than active build instructions.

### Sync changes from template

Run `Actions > Sync Changes in Template > Run workflow` to pull in
upstream template changes as a pull request.

### Coding agent on actions

- Mention `@copilot`
- Setup `ANTHROPIC_API_KEY` secret and mention `@claude`
