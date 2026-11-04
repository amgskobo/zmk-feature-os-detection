/*
 * Copyright (c) 2026 cormoran
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/bos.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/usb_conn_state_changed.h>

#include "os_detection_internal.h"

#ifndef CONFIG_ZMK_OS_DETECTION_FORK_LOG_LEVEL
#define CONFIG_ZMK_OS_DETECTION_FORK_LOG_LEVEL 3
#endif
LOG_MODULE_REGISTER(zmk_os_detection_fork_os_detection_usb, CONFIG_ZMK_OS_DETECTION_FORK_LOG_LEVEL);

/* CONFIG_USB_DEVICE_BOS (selected by this module, see Kconfig) bumps the
 * device descriptor's bcdUSB from 2.00 to 2.01 (usb_descriptor.c), which is
 * exactly what makes a host request GET_DESCRIPTOR(BOS) at all - the signal
 * this module's Windows/macOS/Linux split depends on. But Zephyr's BOS
 * header (bos.c) starts with wTotalLength=0/bNumDeviceCaps=0 and is only
 * corrected by usb_bos_register_cap(). Without registration, a response with
 * wTotalLength=0 is shorter than its own 5-byte header and invalid. An
 * enumeration that stops without SET_CONFIGURATION, even after 3 retries,
 * must not establish an OS signature; tolerance by another host would not
 * make that response valid. See docs/windows-usb-enumeration-issue.md.
 * Registering the standard, always-truthful "no
 * LPM support" USB 2.0 Extension capability (bmAttributes=0, same as
 * Zephyr's own webusb sample) makes the BOS response spec-valid without
 * claiming any capability this stack doesn't actually have. Must run
 * before zmk_usb_init()'s usb_enable() (SYS_INIT APPLICATION/
 * CONFIG_ZMK_USB_INIT_PRIORITY in zmk/app/src/usb.c) - POST_KERNEL always
 * precedes APPLICATION regardless of priority number, so that's used here
 * rather than racing on priority numbers. */
USB_DEVICE_BOS_DESC_DEFINE_CAP struct usb_bos_capability_lpm bos_cap_lpm = {
    .bLength = sizeof(struct usb_bos_capability_lpm),
    .bDescriptorType = USB_DESC_DEVICE_CAPABILITY,
    .bDevCapabilityType = USB_BOS_CAPABILITY_EXTENSION,
    .bmAttributes = 0,
};

static int os_detection_usb_bos_init(void) {
    usb_bos_register_cap((void *)&bos_cap_lpm);
    return 0;
}

SYS_INIT(os_detection_usb_bos_init, POST_KERNEL, 0);

/* Ordered heuristic rules; see docs/fingerprints.md for their limits.
 * A malformed BOS with wTotalLength=0 is shorter than its 5-byte header and
 * cannot establish an OS signature. Failed enumeration, including retries
 * without SET_CONFIGURATION, must not become a "no strings means Windows"
 * rule. Both Windows-like and Linux-like fixtures read strings at length 255;
 * the BOS length-255 rule therefore precedes the Linux string rule. The
 * alternative fixture probes the 5-byte BOS header then its advertised
 * 12-byte total, including this module's one registered capability. These are
 * controlled request patterns, not a general accuracy or hardware claim. */
enum zmk_os zmk_os_classify_usb(const struct usb_fp_stats *stats) {
    if (stats->string_request_count == 0 && !stats->bos_requested) {
        return ZMK_OS_UNKNOWN;
    }

    bool short_probe_seen = stats->string_wlength_hist[USB_FP_WLENGTH_2] > 0;
    bool full_reread_seen = stats->string_wlength_other > 0;

    /* Apple-shaped strings use a 2-byte probe followed by another-length read.
     * Check this before BOS/Linux rules. The result is MACOS for both macOS
     * and iOS patterns; this observer cannot reliably distinguish them.
     * SET_FEATURE(DEVICE_REMOTE_WAKEUP), including after SET_CONFIGURATION,
     * is deliberately ignored. Shared request shapes can likewise make
     * Android/ChromeOS USB look like Linux. BLE has no ANCS/AMS probe either. */
    if (short_probe_seen && full_reread_seen) {
        return ZMK_OS_MACOS;
    }

    /* A BOS read at wLength=255 selects WINDOWS before the overlapping string
     * rule. Fixture differences such as CONFIGURATION 255 versus 9 then 34,
     * or one DEVICE_QUALIFIER read versus three, are not discriminators here.
     * A 5-byte BOS probe is not itself the WINDOWS rule. */
    if (stats->bos_requested && stats->bos_wlength == 255) {
        return ZMK_OS_WINDOWS;
    }

    /* A length-255 string read without a length-2 probe selects LINUX when
     * no earlier rule matched. The fixture includes index 0 (language IDs),
     * BOS 5 then 12, and three DEVICE_QUALIFIER (type 0x06) requests. Those
     * extra requests do not add classification conditions. */
    if (stats->string_wlength_hist[USB_FP_WLENGTH_255] > 0 && !short_probe_seen) {
        return ZMK_OS_LINUX;
    }

    return ZMK_OS_UNKNOWN;
}

/* Written by the USB stack's control-request handling, read and reset on the
 * system work queue. */
static struct usb_fp_stats stats;
static struct k_spinlock stats_lock;

static void usb_settle_work_handler(struct k_work *work) {
    k_spinlock_key_t key = k_spin_lock(&stats_lock);
    struct usb_fp_stats settled = stats;
    k_spin_unlock(&stats_lock, key);

    enum zmk_os detected = zmk_os_classify_usb(&settled);
    LOG_DBG("os detection: USB fingerprint stats strings=%u other=%u bos=%d",
            settled.string_request_count, settled.string_wlength_other, settled.bos_requested);
    zmk_os_detection_report_usb(detected);
}

static K_WORK_DELAYABLE_DEFINE(usb_settle_work, usb_settle_work_handler);

static void reset_usb_fp_stats(void) {
    k_spinlock_key_t key = k_spin_lock(&stats_lock);
    memset(&stats, 0, sizeof(stats));
    k_spin_unlock(&stats_lock, key);
}

/* These counters preserve presence evidence within one enumeration. Repeated
 * valid descriptor reads must not wrap a previously observed signal to zero. */
static void increment_usb_counter(uint16_t *counter) {
    if (*counter < UINT16_MAX) {
        (*counter)++;
    }
}

/* Called (via the __wrap_usb_handle_bos hook below, or directly by the
 * CONFIG_ZMK_OS_DETECTION_TEST_INJECT shim) for every standard USB SETUP
 * packet. Observation only - never changes what's returned to the host. */
void zmk_os_detection_observe_setup(const struct usb_setup_packet *setup) {
    if (setup->bRequest == USB_SREQ_SET_ADDRESS) {
        /* Host (re)started enumeration; drop any previous cycle's stats. */
        reset_usb_fp_stats();
        return;
    }

    if (setup->bRequest != USB_SREQ_GET_DESCRIPTOR || !usb_reqtype_is_to_host(setup)) {
        return;
    }

    uint8_t descriptor_type = USB_GET_DESCRIPTOR_TYPE(setup->wValue);
    if (descriptor_type != USB_DESC_STRING && descriptor_type != USB_DESC_BOS) {
        return; /* not a signal we track; don't restart the debounce timer */
    }

    k_spinlock_key_t key = k_spin_lock(&stats_lock);
    if (descriptor_type == USB_DESC_STRING) {
        increment_usb_counter(&stats.string_request_count);
        if (setup->wLength == 255) {
            increment_usb_counter(&stats.string_wlength_hist[USB_FP_WLENGTH_255]);
        } else if (setup->wLength == 2) {
            increment_usb_counter(&stats.string_wlength_hist[USB_FP_WLENGTH_2]);
        } else {
            increment_usb_counter(&stats.string_wlength_other);
        }
    } else {
        stats.bos_requested = true;
        stats.bos_wlength = setup->wLength;
    }
    k_spin_unlock(&stats_lock, key);

    k_work_reschedule(&usb_settle_work, K_MSEC(CONFIG_ZMK_OS_DETECTION_USB_SETTLE_MS));
}

static int os_detection_usb_listener(const zmk_event_t *eh) {
    const struct zmk_usb_conn_state_changed *ev = as_zmk_usb_conn_state_changed(eh);
    if (ev && ev->conn_state == ZMK_USB_CONN_NONE) {
        k_work_cancel_delayable(&usb_settle_work);
        reset_usb_fp_stats();
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(os_detection_usb, os_detection_usb_listener);
ZMK_SUBSCRIPTION(os_detection_usb, zmk_usb_conn_state_changed);

#if IS_ENABLED(CONFIG_ZMK_OS_DETECTION_TEST_INJECT)
#include "../tests/support/os_detection_usb_assertions_0.inc"
#endif /* CONFIG_ZMK_OS_DETECTION_TEST_INJECT */

/* Always wrap the real hook, TEST_INJECT or not: --wrap=usb_handle_bos is
 * applied unconditionally by CMakeLists.txt whenever this file is compiled,
 * so __wrap_usb_handle_bos must always exist or the link fails. On
 * native_sim there's no real USB DC to generate SETUP packets, but
 * CONFIG_USB_DEVICE_BOS (selected by CONFIG_ZMK_OS_DETECTION_USB) still
 * compiles usb_handle_bos() in, so the wrap target exists there too. */
int __real_usb_handle_bos(struct usb_setup_packet *setup, int32_t *len, uint8_t **data);

int __wrap_usb_handle_bos(struct usb_setup_packet *setup, int32_t *len, uint8_t **data) {
    zmk_os_detection_observe_setup(setup);
    return __real_usb_handle_bos(setup, len, data);
}
