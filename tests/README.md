# Owner tests

Snapshots use the dedicated logger source prefix
`zmk_os_detection_fork_os_detection_core/handler`. Classification values,
return codes, sequence and timing are independent assertions; a logger-prefix
change must not auto-accept a behavioral mismatch. Original comparisons and
raw mismatches belong in private task evidence, without a dependency on an
individual integrating checkout's artifact paths.

This repository owns its executable test logic and fixtures under `tests/`.

`tests/web/lifecycle.spec.tsx` adds eight actual-provider/deferred-response
scenarios to the original fifteen Web scenarios. Retired connection polls and
overrides cannot replace current results or errors. An old completion cannot
clear a new refresh's loading state, and unmounted requests retire. Five cases
fail against the archived application; the unmount boundary and original
assertions remain. The complete visible panel state resets per connection/index;
request generations also prevent an older poll or override from completing over
a newer operation on that panel. The transport call itself is not cancelled by
this UI guard.
Two additional scenarios preserve the newest accepted state on one connection
and finish an override's loading state while an older poll remains pending. The
latter rejects an identified partial-repair negative control that retired the
poll without letting the override finish the loading state. The former's old
automatic-overlap timing was corrected to the single-flight polling contract:
its original call-count and latest-state assertions remain, with an added
no-overlap assertion before the older read completes. Foreground override versus
an older poll still exercises out-of-order retirement.

Four index-only cases retain the same provider value and connection reference
while changing the actual subsystem index from 2 to 7. Late poll and override
success/error outcomes cannot replace the new index's USB/BLE state or error,
clear its pending refresh, or hold admission after that refresh completes. Raw
RPC traces record the unchanged connection and the indices used by the SDK.
These cases also pass against the archived admission-before application as
positive controls; the existing connection/index retirement already covered
this boundary.

`tests/web/polling.spec.tsx` adds seven actual-provider scenarios. Ordered fake
clock traces retain valid 4,000ms replies across multiple 3,000ms intervals,
slow foreground overrides and the following poll, actual SDK deadline failure
and recovery, source cleanup, and empty/malformed/remote-error responses. Polls
admit only while idle; only the current request ticket releases admission and
loading. The four slow/admission scenarios fail on the archived pre-repair
application. No polling interval or SDK deadline is increased. These controlled
clock/SDK observations do not establish browser serial delivery or physical
transport timing.

## Current test ownership and compatibility

This owner supports DYA custom Studio RPC. Stock ZMK is NOT APPLICABLE;
no feature-disabled stock check is required or counted as compatibility.

Canonical entries (run from the repository root):

```bash
python3 -m unittest discover -v -s tests -p test_owner.py
bash tests/run-web.sh
```

[maintenance.json](maintenance.json) records supported cores, current commands
and explicit gaps. It is a reference map, not a passing test or a claim that
registration/coverage totals establish completeness. Preserve ordered outputs,
raw timestamps and the first failure. Read actual dependencies before execution.

`OWNER_TEST_OUTPUT=/absolute/new/directory` retains owner native/build outputs.
An existing directory fails before tests. Without it a fresh temporary directory
is printed and retained. No shared build directory is deleted. Use
`PYTHONPATH=tests python3 -m unittest -v test_owner.WestCommandsTests.<method>`
for a focused case; discovery runs the complete documented class.

Web assertions and Jest configuration live in `tests/web/`; application source
and installed dependencies remain in `web/`. The package scripts only dispatch
the owner configuration. No dependencies are installed implicitly by the tests.

Guarded production includes in `tests/support/` contain test-only scenarios.
Small production probes remain beside the state they expose. Public sample
settings and diagnostic/fault-injection APIs are production contracts, not
duplicated test algorithms.

The native verifier runs each built binary three times in independent empty
initial-flash directories, retaining raw output before filtering. It checks real
process exits, emitted sanitizer diagnostics, fatal assertions and every linked
checked-init result before comparing unchanged owner snapshots. Recovering ASan
or UBSan diagnostics reject a run even when it exits zero and finishes the key
trace. Ordinary sanitizer names and configuration labels remain valid output.
A SYS_INIT error cannot be hidden by a later matching key trace. Native
scheduling and controlled APIs still do not establish physical transport/retention.
Test-only core/tool dependencies are
pinned; update pins only after reviewing the new sources and rerunning this
owner matrix. No network updates are implicit in the owner test command.

## Reproducible isolated checks

Use this owner's `west/west-test-isolated.yml` and its pinned test dependency
manifest. Test ZMK is `e5c9b6915b56801193e359dd9bad4a167ce0d1b8`;
Zephyr is `10ba6d0cb38bc3d258775d27982f707599320085`. Resolve the
manifest and verify actual checkout identities before running. Do not inherit
global CMake module overrides or moving production dependency references into
these isolated fixtures. The CI SDK image is pinned by digest in
`.github/workflows/zmk-module.yml`; production dependency manifests are separate.

Use a new `OWNER_TEST_OUTPUT` directory for every complete owner run. Native
repeats reuse one compiled binary with independent initial-flash state; keep
the first failure and raw combined stdout/stderr order. DYA-only compatibility
still means no stock-ZMK test requirement.

Kernel fixture `--output` paths must be absolute, have an existing parent and
be fresh: an existing file, directory or symlink (including a dangling one) is
rejected. The resolved parent must keep the result outside the owner and all
selected dependency sources, including explicit dependency overrides.
`tests/test_kernel_output.py` retains six lightweight argument controls for
these boundaries and a valid external parent alias. These controls run in the
canonical owner entry before any fixture compilation.

The test/tool static CI job installs pre-commit 4.7.0 and uses the pinned
Ruff/clang-format/formatting hooks. Its SKIP list avoids repeating the firmware
and Web jobs. Local full pre-commit execution may run those functional hooks;
prepare Web dependencies with the normal package lifecycle first where present.
A successful hook exit must also leave source inputs unchanged. Snapshot and
event-pattern bytes are excluded from whitespace rewriting.

### Sanitizer configurations and the selected-core boundary

The selected ZMK `native_sim/native/zmk_test_mock` variant selects `64BIT` in
its board Kconfig. Its ordinary native fixtures are therefore 64-bit checks;
the four ARM board profiles exercise the production architecture separately.

Native ZMK combined ASan/UBSan completion is currently **BLOCKED** on the pinned
DYA core
`e5c9b6915b56801193e359dd9bad4a167ce0d1b8`. Its
`app/module/drivers/kscan/kscan_mock.c:103` expands a signed `1 << 31` in
`kscan_mock_schedule_next_event_0`. The strict profile selects `CONFIG_ASAN=y`,
`CONFIG_UBSAN=y`, `CONFIG_ASAN_RECOVER=n` and explicit
`EXTRA_CFLAGS=-fno-sanitize-recover=all`; the effective final compiler flags
must contain that last recovery choice. `CONFIG_ASAN_RECOVER=n` alone does not
make every compiler invocation strict. The separate NSI Make runner retains its
actual flags and uses `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`; that is
a runtime stop policy, not a claim that its compiler used the firmware's
`-fno-sanitize-recover=all` flag. UBSan terminates each strict process at the
first core diagnostic. The same binary for each of the three fixtures
reproduces that failure in all three fresh-process runs.

The recovering profile selects `CONFIG_ASAN_RECOVER=y`, explicit
`EXTRA_CFLAGS=-fsanitize-recover=all`, and sanitizer `halt_on_error=0` options.
Both the actual firmware and NSI runner compiler flags include recovery. Each
fixture then exits zero and completes its unchanged snapshot, while emitting
five diagnostics from that same core source line per run. All three repetitions
remain BLOCKED. The verifier rejects every diagnosed run independently;
rejecting the first fixture does not validate the other fixtures. Classify every
message by its raw offset, exact source/line/body and hash of the compiled source;
an earlier known core diagnostic does not classify a later owner error.

The USB and RPC checked initialization hooks each return zero before the later
core fault; the feature-disabled fixture has no checked owner hook. Those hook
results do not establish a clean whole-process result. Keep the raw order and
classify the lane as BLOCKED, rather than
changing the core, dropping fixtures or disabling OS features to clear it.

Normal, ASan-only and optimized native configurations retain all three fixtures
and three verified repeats. The optimized configuration uses
`CONFIG_SPEED_OPTIMIZATIONS=y` for firmware C and the NSI Make runner's
`NSI_OPT=-O2`; inspect both actual sets of flags. The ASan-only configuration
keeps the same DYA features and separately passes the owner's
exit/checked-init/unchanged-snapshot validator. The optimized and combined
ASan/UBSan host boundaries are independent checks. The standalone kernel families do not use
ZMK's kscan mock: all five cases in each of `usb-session`, `ble-session` and
`settings-view` also complete three times under combined ASan/UBSan.

Select a strict kernel configuration with Zephyr's `EXTRA_CONF_FILE` environment
variable and an external, separately retained overlay containing
`CONFIG_ASAN=y`, `CONFIG_ASAN_RECOVER=n` and `CONFIG_UBSAN=y`, plus
`EXTRA_CFLAGS=-fno-sanitize-recover=all`. Use ASan and UBSan
`halt_on_error=1` options. The recovering kernel profile uses
`CONFIG_ASAN_RECOVER=y`, `EXTRA_CFLAGS=-fsanitize-recover=all` and
`halt_on_error=0` options instead. Both profiles retain all five cases and three
repeats in every family. `kernel_result_passed()` applies the same diagnostic
recognition as native admission; a PASS marker and zero exit cannot hide a
recovering diagnostic.

Run the unchanged `tests/kernel_boundary.py` entry for every family with fresh
absolute outputs. Retain overlay/config/binary hashes, actual built objects,
compiler inputs and final flags before the first launch. The NSI runner is a
separate Make build: retain its Makefile, `nsi_config`, `.d` files and actual
C/header inputs alongside the root compilation database. Planned compilation
database rows without objects do not establish compiled or linked coverage;
check Ninja dependency records, the link command and map. Native ZMK, standalone
kernel and host sanitizer results remain separate; none establishes physical USB/BLE or Flash
acceptance.

The `test_override_boundaries` owner case compiles the complete core, settings
reader and RPC handler with real nanopb under ASan/UBSan and optimization.
Invalid raw stored/imported OS values (-1, 6 and both int32 limits) read as
UNKNOWN, preserve the underlying value, leave other profiles independent and
allow the valid detected OS to remain effective. Actual response encoding and
decoding must retain that fallback; valid UNKNOWN through ANDROID values remain
unchanged. The settings backend is controlled, so physical persistence remains
an independent gate.

`test_usb_observation_boundaries` runs `tests/usb-boundary/run.py`, compiling
the complete production USB observer and classifier under optimization and
ASan/UBSan. It feeds valid descriptor reads through the public observation
entry. Short, full-255, other-length and aggregate counters retain presence at
65,535, 65,536 and longer sequences; Apple/Linux classifications remain exact.
Ignored requests add no evidence or debounce. SET_ADDRESS clears every counter
and an independent Windows BOS pattern still classifies as Windows. The
pre-repair source makes these repeated-presence assertions fail; no scenario or
snapshot was removed. Kernel work/locking and the USB controller are controlled,
so this fixture does not establish physical enumeration or USB delivery.

The owner command boundary limits `zmk-build` to one target at a time by
default. `OWNER_TEST_BUILD_PARALLELISM` selects an explicit positive target
count; account for compiler jobs and concurrent owners in the global budget.
`tests/test_build_budget.py` checks default/explicit forwarding and rejects
invalid budgets before launching west. The canonical owner entry includes
these controls without dropping any native or board scenario.

## Kernel session and settings-view boundaries

The canonical owner entry also runs `tests/kernel_boundary.py` for each of
`usb-session`, `ble-session` and `settings-view`. Each family builds one actual native kernel
binary and runs all five cases three times in fresh process directories. Logs,
exits, binary/input identities and unchanged input checks are retained beneath
`OWNER_TEST_OUTPUT`; existing output directories are refused.

`usb-session` compiles the complete USB observer/classifier with real kernel
delayable work, spinlocks and time. Five cases cover ordinary settle, reset of
an already-running callback, a new enumeration's read before that old callback
returns, another relevant read in the same enumeration, and disconnect while
old work is running. Ordered packet/report timestamps require at least the
configured 200ms quiet interval after the latest relevant read. Retired work
must not publish; every case also checks ignored requests and a later independent
Apple-shaped enumeration. The original source passes the serial case three
times and fails all three repeats of each other case at `publications == 0`.
USB requests/events and the nonblocking core report sink are controlled; these
kernel observations do not establish physical enumeration or USB delivery.

`ble-session` compiles the complete production BLE observer with real kernel
workqueues, mutexes, timers and threads. Controlled Bluetooth callbacks cover
normal reconnect, an old running callback entering after reconnect, a new read
arming that same work before the old callback returns, disconnect without a new
session, and session replacement while result publication is gated. Every case
also checks independent profile state, ignored battery polls, a latched result,
and another ordinary reconnect. The source uses thread-context per-profile
mutexes to keep a session through its result/cache publication; its quiet
deadline is the only permission to consume statistics. Parking old work uses
`K_FOREVER` rescheduling so new reads do not encounter cancellation state.

`settings-view` compiles the complete OS settings adapter, selected Custom
Settings registry/array implementation and DYA event manager on the real kernel.
An actual cross-thread handoff churns twenty foreign elements through the
sixteen-view pool. Override/cache reads and writes must retain the intended
profile/key and preserve every unrelated element. The handoff never waits for
a consumer while the caller owns the registry mutex; guarded by-key calls
finish their lookup/use before the handoff. Serial operation, invalid enum and
profile indices, persistence-error RAM state and retry remain controls.
`CUSTOM_SETTINGS_MODULE` can explicitly select another inspected dependency
checkout; otherwise the runner resolves the owner's pinned west project. This
selection does not change its manifest or qualify another revision implicitly.

These fixtures control Bluetooth peers and storage/report APIs; they establish
kernel ownership and registry identity, not physical radio or Flash acceptance.

## Review ownership and negative controls

The selected Zephyr work implementation permits an already-running handler to
finish after asynchronous cancellation. Rescheduling replaces its delayable
timeout; `K_FOREVER` leaves no active timeout and does not enter work cancellation.
An older invocation therefore checks the current connection, settled state and
quiet deadline under the profile mutex before consuming data. The same mutex
is held through classification and result/cache publication. This prevents a
module session replacement during publication; it does not bound time spent
waiting for persistent storage or prove physical disconnect/HID timing.

The adapter's by-key functions resolve and consume the recyclable array view
under the Custom Settings guard. `settings-view` covers that direct path.
The separate generic Custom Settings `WriteSetting` path is dependency-owned:
the selected registry guards lookup, permission, conversion and mutation, retains
the view, and rejects keyspace delete/reuse through an identity epoch. The
Custom Settings owner's `tests/support/review_rpc_cases.inc` exercises generic
handler view churn and request-decoded keyspace reuse; its `rpc-epoch` defect
control belongs to that owner. These are distinct coverage boundaries.

The production and isolated-test manifests select Custom Settings
`c4071b095c3007d1deb055c609603ca9b5897d14`, which includes those generic-handler
identity guards. The OS fixture covers its direct by-key calls; the registry
owner's controls are still required for the generic RPC path. For another
dependency candidate, select it explicitly with `CUSTOM_SETTINGS_MODULE` in
container storage and record both source identities. A future dependency
advance needs the reviewed commit, updated pins/lock and owner/integration
checks. No OS test implicitly fetches or changes that revision.

For local defect controls, prepare a fresh isolated source copy in the container
and run `python3 tests/review_negative_control.py FAULT "$ISOLATED_OWNER"`
from the unchanged owner. The operator rejects the authoring source. Execute
the unchanged kernel fixture in that isolated copy, retaining all five cases
and three runs per case:

| Fault | Family and expected executable failure |
|---|---|
| `kernel-recover` | `usb-session` with the recovering kernel profile: a test-only signed shift before the PASS marker must produce a diagnostic and be rejected for every case/repeat, even with process exit zero. The original runner accepts all fifteen; replay the same ELF through the fixed admission rule without rebuilding. |
| `output-preflight` | `test_kernel_output.py`: missing absolute/fresh/source-exclusion rejection must fail the specified `assertRaisesRegex` controls; no build is launched. |
| `quiet-deadline` | `ble-session`: `early` must detect an old callback consuming the new session before its quiet deadline. |
| `publication-lock` | `ble-session`: `publication` must detect session replacement passing the gated old publication. |
| `borrowed-read` | `settings-view`: override/cache reads must detect wrong-view values after cross-thread pool churn. |
| `borrowed-write` | `settings-view`: override/cache writes must detect a changed foreign element or unchanged target. |

Compiler, setup and mutation-site failures are not successful defect controls.
The optimized/ASan/UBSan policy and USB boundaries, all native snapshots, board
profiles and Web checks remain required alongside these kernel controls.
The two host boundary runners explicitly compile their sanitized binaries with
`-fno-sanitize-recover=all` and runtime halt options; they do not declare a
recovering profile. Their acceptance and exact flags remain separate from the
kernel runner's externally selectable recovering profile.

Native result admission requires every `native_sim.keymap` fixture to have a
matching binary. `required_hooks.json` names the expected checked initialization
hooks and required feature/role configuration. Fixture-bound declarations also
reject missing configuration when both the request and build omit it; excluded
roles cannot replace the requested role. The verifier retains each raw run
before filtering and comparing its unchanged ordered snapshot. A successful
fixture requires all three repeats; a failure stops that invocation, so diagnosed
fixtures need independent replays to cover the complete matrix. It rejects logged failures,
fatal output, sanitizer diagnostics and missing/nonzero completions.
`tests/test_native_verifier.py` owns sixteen real-ELF admission controls. The
original eleven missing-scenario, checked-init, exit, snapshot, wrong-role and
unrelated-fixture controls remain. Additional controls produce recovering ASan
on stdout and UBSan on stderr, each with zero exit, checked-init completion and
the unchanged snapshot. The original verifier fails exactly the two specified
missing-rejection assertions; the fixed verifier also rejects fresh replays of
those original ELF bytes. Benign sanitizer/config labels remain positive native
and kernel controls, and a real recovering kernel-style result is rejected.

The existing pinned-SDK console-site audit is
`tests/web/sdkConsole.spec.ts`. For its `sdk-console-site` negative control,
prepare an isolated owner with an actual installed SDK copy, then run the same
operator and canonical Web entry. The operator appends a dormant console call
to the installed client's `lib/index.js`; it refuses a target that resolves
outside the isolated owner. The AST audit must fail its named reviewed-sites
assertion, with one additional dynamic-label site. The dormant call is never
executed. This checks the existing audit boundary without adding a new runtime
suppression mechanism; a production build alone does not execute that audit.

`bash tests/run-web.sh` is the canonical schema-generation/Web test entry.
It runs all configured Jest scenarios in one worker process. The pre-commit
Jest hook dispatches this same owner entry so nested workers do not multiply
the global container budget. Build and lint retain their separate package entries.
Exercise both workflow versions, Node 22.23.4 and 24.18.0, using clean-cache
`npm ci` with lifecycle scripts enabled. The packaged SDK/hook runtime and types
are the actual installed inputs; an older donor's `src/` reference tree is a
different artifact and cannot replace the clean install's prebuilt declarations.
