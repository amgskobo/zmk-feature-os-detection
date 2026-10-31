import { stdout } from "node:process";
import { act, fireEvent, render, screen } from "@testing-library/react";
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

const recordTrace = (scenario: string, entries: unknown[]) => {
  stdout.write(JSON.stringify({ scenario, entries }) + "\n");
};

const wire = (response: Parameters<typeof Response.create>[0]) => ({
  custom: {
    call: { payload: Response.encode(Response.create(response)).finish() },
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
const app = () =>
  createConnectedMockZMKApp({ subsystems: [SUBSYSTEM_IDENTIFIER] });
const advance = async (milliseconds: number) => {
  await act(async () => {
    await jest.advanceTimersByTimeAsync(milliseconds);
  });
};

beforeEach(() => {
  jest.useFakeTimers();
  jest.setSystemTime(0);
});
afterEach(() => jest.useRealTimers());

it("accepts repeated valid 4000ms polls across several 3000ms intervals", async () => {
  const trace: { event: string; at: number }[] = [];
  let outstanding = 0,
    maximumOutstanding = 0;
  (call_rpc as jest.Mock).mockImplementation(() => {
    trace.push({ event: "read", at: Date.now() });
    maximumOutstanding = Math.max(maximumOutstanding, ++outstanding);
    return new Promise((resolve) =>
      setTimeout(() => {
        --outstanding;
        trace.push({ event: "reply", at: Date.now() });
        resolve(state(Os.OS_WINDOWS));
      }, 4000),
    );
  });
  const view = render(
    <ZMKAppProvider value={app()}>
      <OsDetectionSection />
    </ZMKAppProvider>,
  );
  try {
    await advance(17000);
    recordTrace("OS slow-poll ordered trace", trace);
    expect(screen.getByTestId("usb-detected")).toHaveTextContent("Windows");
    expect(
      trace.filter((entry) => entry.event === "read").map((entry) => entry.at),
    ).toEqual([0, 6000, 12000]);
    expect(
      trace.filter((entry) => entry.event === "reply").map((entry) => entry.at),
    ).toEqual([4000, 10000, 16000]);
    expect(maximumOutstanding).toBe(1);
    expect(screen.getByRole("button", { name: /Refresh/ })).toBeEnabled();
  } finally {
    view.unmount();
  }
});

it("lets a slow foreground override own completion and the next poll", async () => {
  const trace: { event: string; at: number }[] = [];
  let reads = 0,
    currentOs = Os.OS_ANDROID;
  (call_rpc as jest.Mock).mockImplementation((_connection, raw) => {
    const request = Request.decode(raw.custom.call.payload);
    if (request.setBleOverride) {
      trace.push({ event: "override", at: Date.now() });
      return new Promise((resolve) =>
        setTimeout(() => {
          currentOs = Os.OS_LINUX;
          trace.push({ event: "override reply", at: Date.now() });
          resolve(
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
        }, 4000),
      );
    }
    trace.push({ event: "read", at: Date.now() });
    if (++reads === 2)
      return new Promise((resolve) =>
        setTimeout(() => {
          trace.push({ event: "retired read reply", at: Date.now() });
          resolve(state(Os.OS_WINDOWS));
        }, 4000),
      );
    return Promise.resolve(state(currentOs));
  });
  const view = render(
    <ZMKAppProvider value={app()}>
      <OsDetectionSection />
    </ZMKAppProvider>,
  );
  try {
    await advance(3500);
    fireEvent.change(screen.getByLabelText("override-profile-0"), {
      target: { value: String(Os.OS_LINUX) },
    });
    await advance(3500);
    recordTrace("OS slow-override before completion trace", trace);
    expect(reads).toBe(2);
    expect(screen.getByRole("button", { name: /Refresh/ })).toBeDisabled();
    expect(screen.getByTestId("ble-effective-0")).toHaveTextContent("Android");
    await advance(500);
    expect(screen.getByTestId("ble-effective-0")).toHaveTextContent("Linux");
    expect(screen.getByRole("button", { name: /Refresh/ })).toBeEnabled();
    await advance(1500);
    recordTrace("OS slow-override completed ordered trace", trace);
    expect(
      trace.filter((entry) => entry.event === "read").map((entry) => entry.at),
    ).toEqual([0, 3000, 9000]);
    expect(screen.getByTestId("usb-detected")).toHaveTextContent("Linux");
    expect(screen.getByTestId("ble-effective-0")).toHaveTextContent("Linux");
  } finally {
    view.unmount();
  }
});

it("releases poll admission on a 5000ms failure and accepts slow recovery", async () => {
  const trace: { event: string; at: number }[] = [];
  let reads = 0;
  (call_rpc as jest.Mock).mockImplementation(() => {
    const first = ++reads === 1;
    trace.push({ event: "read", at: Date.now() });
    // The real SDK's unchanged 5000ms deadline rejects this unanswered call.
    if (first) return new Promise(() => {});
    return new Promise((resolve) =>
      setTimeout(() => {
        trace.push({ event: "reply", at: Date.now() });
        resolve(state(Os.OS_WINDOWS));
      }, 4000),
    );
  });
  const view = render(
    <ZMKAppProvider value={app()}>
      <OsDetectionSection />
    </ZMKAppProvider>,
  );
  try {
    await advance(5000);
    recordTrace("OS poll failure ordered trace", trace);
    expect(screen.getByText(/Operation timed out/)).toBeInTheDocument();
    trace.push({ event: "SDK timeout observed", at: Date.now() });
    expect(screen.getByRole("button", { name: /Refresh/ })).toBeEnabled();
    await advance(5000);
    recordTrace("OS poll recovery ordered trace", trace);
    expect(
      trace.filter((entry) => entry.event === "read").map((entry) => entry.at),
    ).toEqual([0, 6000]);
    expect(screen.getByTestId("usb-detected")).toHaveTextContent("Windows");
    expect(screen.queryByText(/Operation timed out/)).not.toBeInTheDocument();
    expect(screen.getByRole("button", { name: /Refresh/ })).toBeEnabled();
  } finally {
    view.unmount();
  }
});

it("retires a slow source and its interval without blocking a new source", async () => {
  const first = app(),
    second = app();
  const trace: { source: number; at: number }[] = [];
  (call_rpc as jest.Mock).mockImplementation((connection) => {
    const source = connection === first.state.connection ? 1 : 2;
    trace.push({ source, at: Date.now() });
    if (source === 1)
      return new Promise((resolve) =>
        setTimeout(() => resolve(state(Os.OS_ANDROID)), 4000),
      );
    return Promise.resolve(state(Os.OS_WINDOWS));
  });
  const old = render(
    <ZMKAppProvider value={first}>
      <OsDetectionSection />
    </ZMKAppProvider>,
  );
  await advance(3500);
  old.unmount();
  const fresh = render(
    <ZMKAppProvider value={second}>
      <OsDetectionSection />
    </ZMKAppProvider>,
  );
  try {
    await advance(7000);
    recordTrace("OS source cleanup ordered trace", trace);
    expect(trace.filter((entry) => entry.source === 1)).toEqual([
      { source: 1, at: 0 },
    ]);
    expect(
      trace.filter((entry) => entry.source === 2).map((entry) => entry.at),
    ).toEqual([3500, 6500, 9500]);
    expect(screen.getByTestId("usb-detected")).toHaveTextContent("Windows");
    expect(screen.getByRole("button", { name: /Refresh/ })).toBeEnabled();
  } finally {
    fresh.unmount();
  }
});

it.each(["empty", "malformed", "remote error"])(
  "releases admission after a %s response and accepts the next poll",
  async (outcome) => {
    let reads = 0;
    (call_rpc as jest.Mock).mockImplementation(() => {
      if (++reads > 1) return Promise.resolve(state(Os.OS_WINDOWS));
      if (outcome === "empty") return Promise.resolve({ custom: { call: {} } });
      if (outcome === "remote error")
        return Promise.resolve(
          wire({ error: { message: "controlled remote error" } }),
        );
      return Promise.resolve({
        custom: { call: { payload: Uint8Array.of(0x0a, 0x02, 0x0a) } },
      });
    });
    const view = render(
      <ZMKAppProvider value={app()}>
        <OsDetectionSection />
      </ZMKAppProvider>,
    );
    try {
      await advance(0);
      expect(reads).toBe(1);
      expect(screen.queryByTestId("usb-detected")).not.toBeInTheDocument();
      expect(screen.getByRole("button", { name: /Refresh/ })).toBeEnabled();
      if (outcome === "empty")
        expect(view.container.querySelector(".error-message")).toBeNull();
      else
        expect(view.container.querySelector(".error-message")).not.toBeNull();
      await advance(3000);
      expect(reads).toBe(2);
      expect(screen.getByTestId("usb-detected")).toHaveTextContent("Windows");
      expect(view.container.querySelector(".error-message")).toBeNull();
      expect(screen.getByRole("button", { name: /Refresh/ })).toBeEnabled();
    } finally {
      view.unmount();
    }
  },
);
