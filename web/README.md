# zmk-feature-os-detection - Web Frontend

Web UI for `zmk-feature-os-detection`'s Custom Studio RPC subsystem: shows
live USB/BLE OS-detection state and lets you set a per-BLE-profile manual
override. Built from cormoran's ZMK module template.

## Features

- **Device Connection**: Connect to the keyboard over Web Serial
- **State view**: USB connection + detected OS, full BLE profile table
- **BLE overrides**: pin a profile to Windows/macOS/Linux, or back to AUTO
- **react-zmk-studio**: Uses the `@cormoran/zmk-studio-react-hook` library for
  simplified ZMK integration

State polling checks every three seconds and starts a read only while no current
operation is active. A valid four-second reply can therefore finish within the
locked SDK's unchanged five-second deadline. A foreground override can retire an
already active background read; subsequent automatic polls wait for that
override to finish or fail. Only the current operation can clear busy state.
Connection/subsystem changes and unmount retire the panel and its interval.
These guards ignore obsolete results without cancelling an already sent SDK call.
Index-only provider tests retain the same connection and provider value while
checking late poll/override success and error, independent refresh completion,
and subsequent polling against the new subsystem index.

## Quick Start

```bash
# Install dependencies
npm install

# Generate TypeScript types from proto
npm run generate

# Run development server
npm run dev

# Build for production
npm run build

# Run tests
npm test
```

## Project Structure

```
src/
├── main.tsx              # React entry point
├── App.tsx               # Connection UI + OsDetectionSection (state/overrides)
├── requestLifetime.ts   # Connection ownership and poll admission
├── App.css               # Styles
└── proto/                # Generated protobuf TypeScript types (gitignored)
    └── cormoran/os-detection/
        └── os_detection.ts

../tests/web/
├── App.spec.tsx                  # Tests for App component
├── OsDetectionSection.spec.tsx   # State table + override RPC
├── lifecycle.spec.tsx            # Connection/request ownership
└── polling.spec.tsx              # Slow replies, admission, recovery and cleanup
```

## How It Works

### 1. Protocol Definition

The protobuf schema is defined in
`../proto/cormoran/os-detection/os_detection.proto`.

### 2. Code Generation

TypeScript types are generated using `ts-proto`:

```bash
npm run generate
```

This runs `buf generate` which uses the configuration in `buf.gen.yaml`.

### 3. Using react-zmk-studio

The app uses the `@cormoran/zmk-studio-react-hook` library:

```typescript
import { useZMKApp, ZMKCustomSubsystem } from "@cormoran/zmk-studio-react-hook";

// Connect to device
const { state, connect, findSubsystem, isConnected } = useZMKApp();

// Find your subsystem
const subsystem = findSubsystem("cormoran__os_detection");

// Create service and make RPC calls
const service = new ZMKCustomSubsystem(state.connection, subsystem.index);
const response = await service.callRPC(payload);
```

## Testing

Tests, watch and coverage generate the current local protobuf types before Jest
collection; a clean checkout does not need pre-existing `src/proto/` output.
After explicit lockfile dependency preparation, the canonical entry from the
owner root is `bash tests/run-web.sh`. No dependency install is implicit.

```bash
# Run all tests
npm test

# Run tests in watch mode
npm run test:watch

# Run tests with coverage
npm run test:coverage
```

### Writing Tests

Use the test helpers from `@cormoran/zmk-studio-react-hook/testing`:

```typescript
import {
  createConnectedMockZMKApp,
  ZMKAppProvider,
} from "@cormoran/zmk-studio-react-hook/testing";

const mockZMKApp = createConnectedMockZMKApp({
  deviceName: "Test Device",
  subsystems: ["cormoran__os_detection"],
});

render(
  <ZMKAppProvider value={mockZMKApp}>
    <YourComponent />
  </ZMKAppProvider>
);
```

## Publishing

Merging a PR into `main` deploys this app to
https://cormoran.github.io/zmk-feature-os-detection/ via
`.github/workflows/web-ui.yml`. Pull requests get a Cloudflare Workers
preview deployment instead (requires `CLOUDFLARE_API_TOKEN` /
`CLOUDFLARE_ACCOUNT_ID` repo secrets).

## Console privacy

The runtime console guard is installed before the app and Studio SDK imports.
It preserves a first string argument as a fixed label, keeps numeric/boolean
values, redacts later strings and limits errors to fixed categories. Plain
objects retain their keys with values redacted to a bounded depth. Transport,
exception propagation and visible UI messages are preserved.

`tests/web/sdkConsole.spec.ts` audits the two pinned SDKs' actual shipped
JavaScript with the TypeScript AST. Every console use must match a reviewed
literal-label site; a new site fails that test for review. The production build
alone does not run this audit. The owner's `sdk-console-site` defect control
adds a dormant dynamic-label call to an isolated installed SDK and must fail
that exact assertion. Browser/framework diagnostics and unrelated extensions
remain separate acceptance domains.

## Dependency security

`package.json` overrides the Studio client's transitive `protobufjs` dependency
to `7.6.5`, including the patched UTF-8 helper. Use `npm ci` with this lockfile.
After changing dependencies, run `npm audit --omit=dev`, the Web tests and the
production build.

The development transitive lock patches preserve the exact root/direct
dependencies, Git SDK/hook commits, tool versions and production dependency
graph. Their changed incoming semver ranges must remain satisfied. On
2026-11-07 the full audit reported 14 moderate and five low findings, with no
high or critical findings; `npm audit` still exited 1. The production-only
audit reported zero findings. These retained results are dated observations;
rerun both audits when reviewing a new dependency selection.

Both workflow versions, Node 22.23.4 and 24.18.0, use clean-cache `npm ci` with
lifecycle scripts enabled. The Linux postinstall packages are `@bufbuild/buf`,
`@cormoran/zmk-studio-react-hook`, `@zmkfirmware/zmk-studio-ts-client`,
`protobufjs` and `unrs-resolver`. Retain the actual installed files, including
runtime JavaScript, declarations, licenses and protobuf schemas. The clean
Git-package installs contain shipped `lib/` files; donor `src/` trees and their
locally regenerated declarations are references, not the installed inputs.
