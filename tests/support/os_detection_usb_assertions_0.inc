#include "init_result.h"
/* SPDX-License-Identifier: MIT */

/* Synthetic SETUP sequences approximating each OS's enumeration pattern, per
 * the (unverified - see docs/fingerprints.md) heuristics above. Used only to
 * unit-test the classifier on native_sim, which has no real USB DC. */
static void inject_setup(uint8_t bRequest, uint8_t descriptor_type, uint16_t wLength) {
    struct usb_setup_packet setup = {
        .bmRequestType = 0x80, /* device-to-host, standard, device recipient */
        .bRequest = bRequest,
        .wValue = (uint16_t)(descriptor_type << 8),
        .wLength = wLength,
    };
    zmk_os_detection_observe_setup(&setup);
}

/* Both a real Mac and a real iPhone send this right after SET_CONFIGURATION
 * (2026-07-19 side-by-side capture) - it is deliberately NOT a classification
 * signal anymore (see zmk_os_classify_usb()). Injected here only to prove the
 * classifier ignores it and still reports macOS for the Apple pattern. */
static void inject_set_feature_remote_wakeup(void) {
    struct usb_setup_packet setup = {
        .bmRequestType = 0x00, /* host-to-device, standard, device recipient */
        .bRequest = USB_SREQ_SET_FEATURE,
        .wValue = USB_SFS_REMOTE_WAKEUP,
    };
    zmk_os_detection_observe_setup(&setup);
}

/* Matches the *second*, re-verified real Windows capture in
 * docs/fingerprints.md (2026-07-05, after the os_detection_usb_bos_init()
 * fix above) - not the module's original Windows capture, which turned out
 * to be Windows failing to enumerate at all against this module's old
 * spec-invalid BOS response (see docs/windows-usb-enumeration-issue.md).
 * With a valid BOS, real Windows now enumerates successfully and *does*
 * request string descriptors (directly at wLength=255, indistinguishable
 * by itself from Linux) - the only signal that still discriminates it from
 * Linux is BOS itself being fetched directly at wLength=255 in one blind
 * shot, vs. Linux's genuine two-step header-then-advertised-length read
 * (see inject_linux_like()). Device descriptor probed at wLength=64 (not
 * macOS's 8), configuration fetched directly at wLength=255 (like BOS,
 * unlike Linux's two-step), and DEVICE_QUALIFIER requested only once
 * (Linux retries it 3x) are recorded here for fidelity/future use but
 * aren't tracked signals yet. */
static void inject_windows_like(void) {
    inject_setup(USB_SREQ_SET_ADDRESS, 0, 0);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_DEVICE, 64);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_DEVICE, 18);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_CONFIGURATION, 255);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_BOS, 255);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 255);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 255);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 255);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_DEVICE_QUALIFIER, 10);
}

/* Matches the real capture in docs/fingerprints.md: short header probe (2
 * bytes) then full re-read for each of 3 string descriptors, then BOS read
 * once at exactly its minimal header length. */
static void inject_macos_like(void) {
    inject_setup(USB_SREQ_SET_ADDRESS, 0, 0);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 2);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 24);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 2);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 24);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 2);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 34);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_BOS, 5);
}

/* Matches the 2026-07-19 real captures in docs/fingerprints.md: a real Mac
 * and a real iPhone captured side by side enumerated IDENTICALLY through this
 * point - device probed at wLength=8, short-probe-then-full re-read on every
 * string and on configuration, BOS read as a two-step 5-then-12 dance, then
 * SET_FEATURE(DEVICE_REMOTE_WAKEUP). Because macOS and iOS are indistinguishable
 * over USB, this scenario asserts the classifier reports macOS (ZMK_OS_MACOS,
 * value 2) - specifically guarding against the earlier regression where the
 * REMOTE_WAKEUP packet made a real Mac misdetect as iOS. */
static void inject_apple_with_remote_wakeup(void) {
    inject_setup(USB_SREQ_SET_ADDRESS, 0, 0);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_DEVICE, 8);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_DEVICE, 18);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 2);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 24);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 2);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 24);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 2);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 34);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_CONFIGURATION, 9);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_CONFIGURATION, 34);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_BOS, 5);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_BOS, 12);
    inject_set_feature_remote_wakeup();
}

/* Matches the *second*, re-verified real Linux capture in
 * docs/fingerprints.md (2026-07-05, after the os_detection_usb_bos_init()
 * fix above, captured from this workspace's own sandbox host - a real
 * Linux kernel): device descriptor probed at wLength=64 (same as Windows,
 * not macOS's 8); BOS now read as a genuine two-step header-then-full
 * dance (5-byte header probe, then 12 bytes - the wTotalLength that header
 * now correctly advertises once this module registers one BOS
 * capability), unlike the single pre-fix 5-byte read this scenario used to
 * inject - this final 12-byte read is what the classifier actually keys
 * off (Windows' single-shot 255-byte read never matches it); DEVICE_QUALIFIER
 * retried 3 times (not tracked as a signal, included for fidelity, and the
 * one place real Windows still differs - it doesn't retry at all);
 * configuration read as header-then-full like macOS; every string
 * descriptor (including index 0, the language-ID list) read directly at
 * the full 255-byte buffer with no short probe first - unchanged from
 * before the fix. */
static void inject_linux_like(void) {
    inject_setup(USB_SREQ_SET_ADDRESS, 0, 0);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_DEVICE, 64);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_DEVICE, 18);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_BOS, 5);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_BOS, 12);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_DEVICE_QUALIFIER, 10);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_DEVICE_QUALIFIER, 10);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_DEVICE_QUALIFIER, 10);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_CONFIGURATION, 9);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_CONFIGURATION, 34);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 255);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 255);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 255);
    inject_setup(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 255);
}

/* Run every scenario sequentially in this one process/port-3240 owner,
 * pausing past the settle debounce between each so every scenario gets
 * classified (and logged) before the stats are reset by the next one's
 * SET_ADDRESS. See the ZMK_OS_DETECTION_TEST_INJECT Kconfig help for why
 * this isn't split into one native_sim process per scenario. */
static int os_detection_test_inject_init(void) {
    const uint32_t settle_margin_ms = CONFIG_ZMK_OS_DETECTION_USB_SETTLE_MS + 20;

    inject_windows_like();
    k_sleep(K_MSEC(settle_margin_ms));

    inject_macos_like();
    k_sleep(K_MSEC(settle_margin_ms));

    inject_linux_like();
    k_sleep(K_MSEC(settle_margin_ms));

    /* Last so its macOS result is distinct from the previous scenario's
     * (Linux) - zmk_os_detection_report_usb() only logs on a change, so two
     * adjacent macOS(2) scenarios would collapse to one snapshot line. */
    inject_apple_with_remote_wakeup();
    k_sleep(K_MSEC(settle_margin_ms));

    return 0;
}

OWNER_CHECKED_SYS_INIT(os_detection_test_inject_init, APPLICATION,
                       CONFIG_APPLICATION_INIT_PRIORITY);
