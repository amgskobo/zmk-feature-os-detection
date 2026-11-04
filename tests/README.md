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
process exits, fatal assertions and every linked checked-init result before
comparing unchanged owner snapshots. A SYS_INIT error cannot be hidden by a
later matching key trace. Native scheduling and controlled APIs still do not
establish physical transport/retention. Test-only core/tool dependencies are
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

The test/tool static CI job installs pre-commit 4.7.0 and uses the pinned
Ruff/clang-format/formatting hooks. Its SKIP list avoids repeating the firmware
and Web jobs. Local full pre-commit execution may run those functional hooks;
prepare Web dependencies with the normal package lifecycle first where present.
A successful hook exit must also leave source inputs unchanged. Snapshot and
event-pattern bytes are excluded from whitespace rewriting.

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
`ble-session` and `settings-view`. Each family builds one actual native kernel
binary and runs all five cases three times in fresh process directories. Logs,
exits, binary/input identities and unchanged input checks are retained beneath
`OWNER_TEST_OUTPUT`; existing output directories are refused.

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
| `quiet-deadline` | `ble-session`: `early` must detect an old callback consuming the new session before its quiet deadline. |
| `publication-lock` | `ble-session`: `publication` must detect session replacement passing the gated old publication. |
| `borrowed-read` | `settings-view`: override/cache reads must detect wrong-view values after cross-thread pool churn. |
| `borrowed-write` | `settings-view`: override/cache writes must detect a changed foreign element or unchanged target. |

Compiler, setup and mutation-site failures are not successful defect controls.
The optimized/ASan/UBSan policy and USB boundaries, all native snapshots, board
profiles and Web checks remain required alongside these kernel controls.

Native result admission requires every `native_sim.keymap` fixture to have a
matching binary. `required_hooks.json` names the expected checked initialization
hooks and required feature/role configuration. Fixture-bound declarations also
reject missing configuration when both the request and build omit it; excluded
roles cannot replace the requested role. The verifier retains all three raw
runs before comparing unchanged ordered snapshots and rejects logged failures,
fatal output and missing/nonzero completions. `tests/test_native_verifier.py`
owns eleven real-ELF admission controls, including a deliberately missing
native scenario, wrong-role and unrelated-fixture controls.

`bash tests/run-web.sh` is the canonical schema-generation/Web test entry.
It runs all configured Jest scenarios in one worker process. The pre-commit
Jest hook dispatches this same owner entry so nested workers do not multiply
the global container budget. Build and lint retain their separate package entries.
