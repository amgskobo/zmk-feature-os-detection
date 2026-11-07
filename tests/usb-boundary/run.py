#!/usr/bin/env python3
"""Exercise the complete USB observer/classifier with controlled kernel APIs."""

import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
headers = {
    "zephyr/kernel.h": r"""#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#define IS_ENABLED(x) (x)
#define K_MSEC(ms) (ms)
#define K_FOREVER -1
static int64_t boundary_now;
static inline int64_t k_uptime_get(void) { return boundary_now; }
struct k_work { void (*handler)(struct k_work *); };
struct k_work_delayable { struct k_work work; bool pending; };
#define K_WORK_DELAYABLE_DEFINE(n, h) struct k_work_delayable n = {.work.handler = h}
static inline int k_work_reschedule(struct k_work_delayable *w, int delay) {
    w->pending = delay != K_FOREVER; return 0;
}
static inline int k_work_cancel_delayable(struct k_work_delayable *w) {
    w->pending = false; return 0;
}
struct k_spinlock { int unused; };
typedef int k_spinlock_key_t;
static inline k_spinlock_key_t k_spin_lock(struct k_spinlock *l) { (void)l; return 0; }
static inline void k_spin_unlock(struct k_spinlock *l, k_spinlock_key_t k) {(void)l;(void)k;}
""",
    "zephyr/init.h": "#define SYS_INIT(fn,...) static const void *fn##_ref __attribute__((unused)) = fn\n",
    "zephyr/logging/log.h": "#define LOG_MODULE_REGISTER(...)\n#define LOG_DBG(...) ((void)0)\n",
    "zephyr/usb/usb_device.h": r"""#pragma once
#include <zephyr/kernel.h>
struct usb_setup_packet { uint8_t bmRequestType, bRequest; uint16_t wValue, wIndex, wLength; };
enum { USB_SREQ_GET_DESCRIPTOR = 6, USB_SREQ_SET_ADDRESS = 5,
       USB_DESC_STRING = 3, USB_DESC_BOS = 15, USB_DESC_DEVICE_CAPABILITY = 16 };
#define USB_GET_DESCRIPTOR_TYPE(value) ((value) >> 8)
static inline bool usb_reqtype_is_to_host(const struct usb_setup_packet *s) {
    return (s->bmRequestType & 0x80) != 0;
}
""",
    "zephyr/usb/bos.h": r"""#pragma once
#define USB_DEVICE_BOS_DESC_DEFINE_CAP
#define USB_BOS_CAPABILITY_EXTENSION 2
struct usb_bos_capability_lpm {
    uint8_t bLength, bDescriptorType, bDevCapabilityType; uint32_t bmAttributes;
};
static inline void usb_bos_register_cap(void *cap) { (void)cap; }
""",
    "zmk/event_manager.h": r"""#pragma once
typedef int zmk_event_t;
#define ZMK_EVENT_DECLARE(n) int raise_##n(struct n event)
#define ZMK_EV_EVENT_BUBBLE 0
#define ZMK_LISTENER(n,h) static const void *n##_ref __attribute__((unused)) = h
#define ZMK_SUBSCRIPTION(...)
""",
    "zmk/events/usb_conn_state_changed.h": r"""#pragma once
enum { ZMK_USB_CONN_NONE };
struct zmk_usb_conn_state_changed { int conn_state; };
static inline const struct zmk_usb_conn_state_changed *as_zmk_usb_conn_state_changed(
    const zmk_event_t *event) { (void)event; return 0; }
""",
}
with tempfile.TemporaryDirectory(prefix="os-usb-boundary-") as directory:
    work = Path(directory)
    for relative, text in headers.items():
        target = work / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text)
    failed = False
    for mode, options in [
        ("optimized", ["-O2"]),
        (
            "sanitized",
            [
                "-O1",
                "-g",
                "-fsanitize=address,undefined",
                "-fno-sanitize-recover=all",
                "-fno-pie",
                "-no-pie",
            ],
        ),
    ]:
        binary = work / mode
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wshadow",
                "-Wno-unused-parameter",
                "-DCONFIG_ZMK_OS_DETECTION_TEST_INJECT=0",
                "-DCONFIG_ZMK_OS_DETECTION_USB_SETTLE_MS=100",
                "-I" + str(work),
                "-I" + str(root / "include"),
                *options,
                str(root / "tests/usb-boundary/harness.c"),
                "-o",
                str(binary),
            ],
            check=True,
        )
        result = subprocess.run(
            [str(binary)],
            env=dict(
                os.environ,
                ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                UBSAN_OPTIONS="halt_on_error=1",
            ),
        )
        print("USB observation boundary", mode, "exit", result.returncode, flush=True)
        failed |= result.returncode != 0
    raise SystemExit(failed)
