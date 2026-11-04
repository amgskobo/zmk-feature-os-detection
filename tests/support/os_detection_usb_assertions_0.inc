#include "init_result.h"
/* SPDX-License-Identifier: MIT */

/* Synthetic SETUP sequences covering the ordered rules and ambiguous patterns
 * in docs/fingerprints.md. Used only to
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

/* REMOTE_WAKEUP after SET_CONFIGURATION is not an OS classification signal.
 * Inject it to require that the Apple-shaped pattern remains MACOS. */
static void inject_set_feature_remote_wakeup(void) {
    struct usb_setup_packet setup = {
        .bmRequestType = 0x00, /* host-to-device, standard, device recipient */
        .bRequest = USB_SREQ_SET_FEATURE,
        .wValue = USB_SFS_REMOTE_WAKEUP,
    };
    zmk_os_detection_observe_setup(&setup);
}

/* Windows-shaped fixture: DEVICE 64 then 18, CONFIGURATION/BOS/STRING 255,
 * and one DEVICE_QUALIFIER request. BOS 255 selects WINDOWS ahead of the
 * overlapping Linux string rule. Device/configuration shape and qualifier
 * count remain controlled inputs, not additional classifier conditions.
 * Failed enumeration against an invalid BOS cannot establish an OS signal. */
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

/* Apple-shaped fixture: short header probe (2
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

/* Extended Apple-shaped fixture: DEVICE 8 then 18, STRING 2 then 24/34,
 * CONFIGURATION 9 then 34, BOS 5 then 12 and REMOTE_WAKEUP. The latter must
 * not split iOS from macOS: require ZMK_OS_MACOS (value 2), preserving the
 * negative scenario against using REMOTE_WAKEUP as an iOS discriminator. */
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

/* Linux-shaped fixture: DEVICE 64 then 18, BOS 5 then its advertised 12-byte
 * total (one capability), three DEVICE_QUALIFIER requests, CONFIGURATION 9
 * then 34, and full length-255 strings including language-ID index 0. No
 * length-2 string probe occurs, so the LINUX rule matches after the BOS-255
 * rule is excluded. Qualifier retries and configuration shape are retained
 * inputs, not classification conditions. */
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
