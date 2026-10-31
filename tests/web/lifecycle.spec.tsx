import { stdout } from "node:process";
import {
  act,
  fireEvent,
  render,
  screen,
  waitFor,
} from "@testing-library/react";
import {
  createConnectedMockZMKApp,
  ZMKAppProvider,
} from "@cormoran/zmk-studio-react-hook/testing";
import { call_rpc } from "@zmkfirmware/zmk-studio-ts-client";
import { OsDetectionSection, SUBSYSTEM_IDENTIFIER } from "../../web/src/App";
import {
  Os,
  Request,
  Response,
} from "../../web/src/proto/cormoran/os-detection/os_detection";

jest.mock("@zmkfirmware/zmk-studio-ts-client", () => ({ call_rpc: jest.fn() }));
const wire = (response: Parameters<typeof Response.create>[0]) => ({
  custom: {
    call: {
      payload: Response.encode(Response.create(response)).finish(),
    },
  },
});
const state = (os: Os) =>
  wire({
    state: {
      usb: { connected: true, detected: os },
      bleProfiles: [
        {
          index: 0,
          bonded: true,
          connected: true,
          detected: os,
          override: Os.OS_UNKNOWN,
          effective: os,
        },
      ],
      activeProfileIndex: 0,
      currentEffective: os,
    },
  });
function deferred<T>() {
  let resolve!: (value: T) => void, reject!: (error: Error) => void;
  const promise = new Promise<T>((done, fail) => {
    resolve = done;
    reject = fail;
  });
  return { promise, resolve, reject };
}
const apps = () => [
  createConnectedMockZMKApp({ subsystems: [SUBSYSTEM_IDENTIFIER] }),
  createConnectedMockZMKApp({ subsystems: [SUBSYSTEM_IDENTIFIER] }),
];

it.each(["success", "error"])(
  "ignores an old poll %s after a new connection loads",
  async (outcome) => {
    const [first, second] = apps();
    expect(first.state.connection).not.toBe(second.state.connection);
    const pending = deferred<ReturnType<typeof wire>>();
    let firstCalls = 0;
    (call_rpc as jest.Mock).mockImplementation((connection) => {
      if (connection === first.state.connection) {
        firstCalls++;
        return pending.promise;
      }
      return Promise.resolve(state(Os.OS_WINDOWS));
    });
    const view = render(
      <ZMKAppProvider value={first}>
        <OsDetectionSection />
      </ZMKAppProvider>,
    );
    await waitFor(() => expect(firstCalls).toBe(1));
    view.rerender(
      <ZMKAppProvider value={second}>
        <OsDetectionSection />
      </ZMKAppProvider>,
    );
    await waitFor(() =>
      expect(screen.getByTestId("usb-detected")).toHaveTextContent("Windows"),
    );
    await act(async () => {
      if (outcome === "success") pending.resolve(state(Os.OS_ANDROID));
      else pending.reject(new Error("retired poll failed"));
    });
    expect(screen.getByTestId("usb-detected")).toHaveTextContent("Windows");
    expect(screen.queryByText(/retired poll failed/)).not.toBeInTheDocument();
  },
);

it("keeps a new refresh loading when an old poll finishes", async () => {
  const [first, second] = apps();
  const old = deferred<ReturnType<typeof wire>>(),
    fresh = deferred<ReturnType<typeof wire>>();
  let firstCalls = 0,
    secondCalls = 0;
  (call_rpc as jest.Mock).mockImplementation((connection) => {
    if (connection === first.state.connection) {
      firstCalls++;
      return old.promise;
    }
    return ++secondCalls === 1
      ? Promise.resolve(state(Os.OS_WINDOWS))
      : fresh.promise;
  });
  const view = render(
    <ZMKAppProvider value={first}>
      <OsDetectionSection />
    </ZMKAppProvider>,
  );
  await waitFor(() => expect(firstCalls).toBe(1));
  view.rerender(
    <ZMKAppProvider value={second}>
      <OsDetectionSection />
    </ZMKAppProvider>,
  );
  await waitFor(() =>
    expect(screen.getByTestId("usb-detected")).toHaveTextContent("Windows"),
  );
  fireEvent.click(screen.getByRole("button", { name: /Refresh/ }));
  await waitFor(() => expect(secondCalls).toBe(2));
  await act(async () => old.resolve(state(Os.OS_ANDROID)));
  expect(screen.getByRole("button", { name: /Refresh/ })).toBeDisabled();
  await act(async () => fresh.resolve(state(Os.OS_WINDOWS)));
  expect(screen.getByTestId("usb-detected")).toHaveTextContent("Windows");
});

it.each(["success", "error"])(
  "retires an old override %s after a new connection loads",
  async (outcome) => {
    const [first, second] = apps();
    const pending = deferred<ReturnType<typeof wire>>();
    let writes = 0;
    (call_rpc as jest.Mock).mockImplementation((connection, raw) => {
      const request = Request.decode(raw.custom.call.payload);
      if (connection === first.state.connection) {
        if (request.setBleOverride) {
          writes++;
          return pending.promise;
        }
        return Promise.resolve(state(Os.OS_ANDROID));
      }
      return Promise.resolve(state(Os.OS_WINDOWS));
    });
    const view = render(
      <ZMKAppProvider value={first}>
        <OsDetectionSection />
      </ZMKAppProvider>,
    );
    const select = await screen.findByLabelText("override-profile-0");
    fireEvent.change(select, { target: { value: String(Os.OS_LINUX) } });
    await waitFor(() => expect(writes).toBe(1));
    view.rerender(
      <ZMKAppProvider value={second}>
        <OsDetectionSection />
      </ZMKAppProvider>,
    );
    await waitFor(() =>
      expect(screen.getByTestId("ble-effective-0")).toHaveTextContent(
        "Windows",
      ),
    );
    await act(async () => {
      if (outcome === "success")
        pending.resolve(
          wire({
            setBleOverride: {
              profile: {
                index: 0,
                detected: Os.OS_ANDROID,
                override: Os.OS_LINUX,
                effective: Os.OS_LINUX,
              },
            },
          }),
        );
      else pending.reject(new Error("retired override failed"));
    });
    expect(screen.getByTestId("ble-effective-0")).toHaveTextContent("Windows");
    expect(
      screen.queryByText(/retired override failed/),
    ).not.toBeInTheDocument();
  },
);

it("retires an unmounted poll error without affecting a new section", async () => {
  const [first, second] = apps();
  const pending = deferred<ReturnType<typeof wire>>();
  let firstCalls = 0;
  (call_rpc as jest.Mock).mockImplementation((connection) => {
    if (connection === first.state.connection) {
      firstCalls++;
      return pending.promise;
    }
    return Promise.resolve(state(Os.OS_WINDOWS));
  });
  const view = render(
    <ZMKAppProvider value={first}>
      <OsDetectionSection />
    </ZMKAppProvider>,
  );
  await waitFor(() => expect(firstCalls).toBe(1));
  view.unmount();
  render(
    <ZMKAppProvider value={second}>
      <OsDetectionSection />
    </ZMKAppProvider>,
  );
  await waitFor(() =>
    expect(screen.getByTestId("usb-detected")).toHaveTextContent("Windows"),
  );
  await act(async () => pending.reject(new Error("unmounted poll failed")));
  expect(screen.getByTestId("usb-detected")).toHaveTextContent("Windows");
  expect(screen.queryByText(/unmounted poll failed/)).not.toBeInTheDocument();
});

it("serializes automatic polls and keeps the newest accepted state on one connection", async () => {
  jest.useFakeTimers();
  const app = apps()[0];
  const old = deferred<ReturnType<typeof wire>>();
  const fresh = deferred<ReturnType<typeof wire>>();
  let calls = 0;
  (call_rpc as jest.Mock).mockImplementation(() =>
    ++calls === 1 ? old.promise : fresh.promise,
  );
  const view = render(
    <ZMKAppProvider value={app}>
      <OsDetectionSection />
    </ZMKAppProvider>,
  );
  try {
    expect(calls).toBe(1);
    await act(async () => {
      jest.advanceTimersByTime(3000);
    });
    expect(calls).toBe(1);
    await act(async () => old.resolve(state(Os.OS_ANDROID)));
    expect(screen.getByTestId("usb-detected")).toHaveTextContent("Android");
    await act(async () => {
      jest.advanceTimersByTime(3000);
    });
    expect(calls).toBe(2);
    await act(async () => fresh.resolve(state(Os.OS_WINDOWS)));
    expect(screen.getByTestId("usb-detected")).toHaveTextContent("Windows");
  } finally {
    view.unmount();
    jest.useRealTimers();
  }
});

it("finishes an override independently of an older pending poll on the same connection", async () => {
  jest.useFakeTimers();
  const app = apps()[0];
  const old = deferred<ReturnType<typeof wire>>();
  const override = deferred<ReturnType<typeof wire>>();
  let reads = 0,
    writes = 0;
  (call_rpc as jest.Mock).mockImplementation((_connection, raw) => {
    const request = Request.decode(raw.custom.call.payload);
    if (request.setBleOverride) {
      writes++;
      return override.promise;
    }
    return ++reads === 1 ? Promise.resolve(state(Os.OS_ANDROID)) : old.promise;
  });
  const view = render(
    <ZMKAppProvider value={app}>
      <OsDetectionSection />
    </ZMKAppProvider>,
  );
  try {
    await act(async () => {});
    expect(screen.getByTestId("ble-effective-0")).toHaveTextContent("Android");
    await act(async () => {
      jest.advanceTimersByTime(3000);
    });
    expect(reads).toBe(2);
    fireEvent.change(screen.getByLabelText("override-profile-0"), {
      target: { value: String(Os.OS_LINUX) },
    });
    expect(writes).toBe(1);
    await act(async () =>
      override.resolve(
        wire({
          setBleOverride: {
            profile: {
              index: 0,
              detected: Os.OS_ANDROID,
              override: Os.OS_LINUX,
              effective: Os.OS_LINUX,
            },
          },
        }),
      ),
    );
    expect(screen.getByRole("button", { name: /Refresh/ })).toBeEnabled();
    await act(async () => old.resolve(state(Os.OS_WINDOWS)));
    expect(screen.getByTestId("ble-effective-0")).toHaveTextContent("Linux");
    expect(screen.getByRole("button", { name: /Refresh/ })).toBeEnabled();
  } finally {
    view.unmount();
    jest.useRealTimers();
  }
});

describe.each(["poll", "override"] as const)(
  "index-only %s retirement",
  (operation) => {
    it.each(["success", "error"])(
      "retires a late %s while preserving the same provider and connection",
      async (outcome) => {
        jest.useFakeTimers();
        jest.setSystemTime(0);
        const owner = createConnectedMockZMKApp({
          subsystems: [{ index: 2, identifier: SUBSYSTEM_IDENTIFIER }],
        });
        const provider = owner;
        const connection = owner.state.connection;
        const old = deferred<ReturnType<typeof wire>>();
        const fresh = deferred<ReturnType<typeof wire>>();
        const trace: {
          index: number;
          operation: string;
          sameConnection: boolean;
          at: number;
        }[] = [];
        let newReads = 0;
        (call_rpc as jest.Mock).mockImplementation((actualConnection, raw) => {
          const request = Request.decode(raw.custom.call.payload);
          const index = raw.custom.call.subsystemIndex;
          trace.push({
            index,
            operation: request.setBleOverride ? "override" : "read",
            sameConnection: actualConnection === connection,
            at: Date.now(),
          });
          if (index === 2) {
            if (operation === "poll" || request.setBleOverride)
              return old.promise;
            return Promise.resolve(state(Os.OS_ANDROID));
          }
          if (index !== 7)
            throw new Error(`Unexpected subsystem index ${index}`);
          return ++newReads === 2
            ? fresh.promise
            : Promise.resolve(state(Os.OS_WINDOWS));
        });
        const view = render(
          <ZMKAppProvider value={provider}>
            <OsDetectionSection />
          </ZMKAppProvider>,
        );
        try {
          await act(async () => {});
          if (operation === "override") {
            fireEvent.change(screen.getByLabelText("override-profile-0"), {
              target: { value: String(Os.OS_LINUX) },
            });
          }
          const subsystem = owner.state.customSubsystems!.subsystems[0];
          subsystem.index = 7;
          expect(owner).toBe(provider);
          expect(owner.state.connection).toBe(connection);
          view.rerender(
            <ZMKAppProvider value={provider}>
              <OsDetectionSection />
            </ZMKAppProvider>,
          );
          await act(async () => {});
          expect(screen.getByTestId("usb-detected")).toHaveTextContent(
            "Windows",
          );
          expect(screen.getByRole("button", { name: /Refresh/ })).toBeEnabled();
          fireEvent.click(screen.getByRole("button", { name: /Refresh/ }));
          expect(newReads).toBe(2);
          await act(async () => {
            if (outcome === "error")
              old.reject(new Error("retired index failed"));
            else if (operation === "poll") old.resolve(state(Os.OS_ANDROID));
            else
              old.resolve(
                wire({
                  setBleOverride: {
                    profile: {
                      index: 0,
                      detected: Os.OS_ANDROID,
                      override: Os.OS_LINUX,
                      effective: Os.OS_LINUX,
                    },
                  },
                }),
              );
          });
          expect(screen.getByTestId("usb-detected")).toHaveTextContent(
            "Windows",
          );
          expect(screen.getByTestId("ble-effective-0")).toHaveTextContent(
            "Windows",
          );
          expect(
            screen.queryByText(/retired index failed/),
          ).not.toBeInTheDocument();
          expect(
            screen.getByRole("button", { name: /Refresh/ }),
          ).toBeDisabled();
          await act(async () => fresh.resolve(state(Os.OS_WINDOWS)));
          expect(screen.getByRole("button", { name: /Refresh/ })).toBeEnabled();
          await act(async () => {
            await jest.advanceTimersByTimeAsync(3000);
          });
          expect(newReads).toBe(3);
          expect(trace.every((entry) => entry.sameConnection)).toBe(true);
          expect(trace.map((entry) => entry.index)).toEqual(
            operation === "poll" ? [2, 7, 7, 7] : [2, 2, 7, 7, 7],
          );
          expect(
            trace.filter((entry) => entry.index === 7).map((entry) => entry.at),
          ).toEqual([0, 0, 3000]);
          stdout.write(
            JSON.stringify({
              scenario: `OS index-only ${operation} ${outcome}`,
              sameProvider: owner === provider,
              entries: trace,
            }) + "\n",
          );
          expect(screen.getByTestId("usb-detected")).toHaveTextContent(
            "Windows",
          );
          expect(screen.getByRole("button", { name: /Refresh/ })).toBeEnabled();
        } finally {
          view.unmount();
          jest.useRealTimers();
        }
      },
    );
  },
);
