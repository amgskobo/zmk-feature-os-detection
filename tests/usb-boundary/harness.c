/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <limits.h>
#include "../../src/os_detection_usb.c"

static unsigned failures;
static enum zmk_os reported;
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            printf("FAIL line=%u expression=%s\n", __LINE__, #condition);                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

bool zmk_os_detection_report_usb(enum zmk_os detected) {
    bool changed = reported != detected;
    reported = detected;
    return changed;
}
void zmk_os_detection_log_usb(enum zmk_os detected) { (void)detected; }
int __real_usb_handle_bos(struct usb_setup_packet *setup, int32_t *len, uint8_t **data) {
    (void)setup;
    (void)len;
    (void)data;
    return -ENOTSUP;
}

static void observe(uint8_t request, uint8_t type, uint16_t length) {
    struct usb_setup_packet setup = {
        .bmRequestType = 0x80,
        .bRequest = request,
        .wValue = (uint16_t)(type << 8),
        .wLength = length,
    };
    zmk_os_detection_observe_setup(&setup);
}

static void settle_as(enum zmk_os expected, const char *scenario) {
    CHECK(usb_settle_work.pending);
    usb_settle_work.pending = false;
    boundary_now += CONFIG_ZMK_OS_DETECTION_USB_SETTLE_MS;
    usb_settle_work.work.handler(&usb_settle_work.work);
    printf("USB %s strings=%u short=%u full=%u other=%u expected=%d actual=%d\n", scenario,
           stats.string_request_count, stats.string_wlength_hist[USB_FP_WLENGTH_2],
           stats.string_wlength_hist[USB_FP_WLENGTH_255], stats.string_wlength_other, expected,
           reported);
    CHECK(reported == expected);
}

int main(void) {
    observe(USB_SREQ_SET_ADDRESS, 0, 0);
    observe(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 2);
    observe(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 24);
    settle_as(ZMK_OS_MACOS, "Apple initial");
    for (unsigned i = 0; i < UINT16_MAX - 1U; i++) {
        observe(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 2);
    }
    settle_as(ZMK_OS_MACOS, "Apple short 65535");
    observe(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 2);
    settle_as(ZMK_OS_MACOS, "Apple short 65536");
    for (unsigned i = 0; i < 129; i++) {
        observe(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 2);
    }
    settle_as(ZMK_OS_MACOS, "Apple short longer");
    CHECK(stats.string_request_count == UINT16_MAX);
    CHECK(stats.string_wlength_hist[USB_FP_WLENGTH_2] == UINT16_MAX);

    observe(USB_SREQ_SET_ADDRESS, 0, 0);
    observe(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 2);
    for (unsigned i = 0; i < UINT16_MAX; i++) {
        observe(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 24);
    }
    settle_as(ZMK_OS_MACOS, "Apple other 65535");
    observe(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 24);
    settle_as(ZMK_OS_MACOS, "Apple other 65536");
    for (unsigned i = 0; i < 129; i++) {
        observe(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 24);
    }
    settle_as(ZMK_OS_MACOS, "Apple other longer");
    CHECK(stats.string_wlength_other == UINT16_MAX);

    observe(USB_SREQ_SET_ADDRESS, 0, 0);
    for (unsigned i = 0; i < UINT16_MAX; i++) {
        observe(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 255);
    }
    settle_as(ZMK_OS_LINUX, "Linux full 65535");
    observe(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 255);
    settle_as(ZMK_OS_LINUX, "Linux full 65536");
    for (unsigned i = 0; i < 129; i++) {
        observe(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 255);
    }
    settle_as(ZMK_OS_LINUX, "Linux full longer");
    CHECK(stats.string_wlength_hist[USB_FP_WLENGTH_255] == UINT16_MAX);

    /* Invalid/irrelevant observation adds neither evidence nor debounce. */
    struct usb_fp_stats before = stats;
    struct usb_setup_packet invalid = {
        .bmRequestType = 0,
        .bRequest = USB_SREQ_GET_DESCRIPTOR,
        .wValue = USB_DESC_STRING << 8,
        .wLength = 2,
    };
    zmk_os_detection_observe_setup(&invalid);
    observe(USB_SREQ_GET_DESCRIPTOR, 1, 2);
    observe(0, USB_DESC_STRING, 2);
    CHECK(memcmp(&before, &stats, sizeof(stats)) == 0);
    CHECK(!usb_settle_work.pending);

    /* A new enumeration clears saturation and every prior fingerprint. */
    observe(USB_SREQ_SET_ADDRESS, 0, 0);
    CHECK(stats.string_request_count == 0 && !stats.bos_requested);
    CHECK(stats.string_wlength_hist[USB_FP_WLENGTH_2] == 0);
    CHECK(stats.string_wlength_hist[USB_FP_WLENGTH_255] == 0);
    CHECK(stats.string_wlength_other == 0);
    observe(USB_SREQ_GET_DESCRIPTOR, USB_DESC_BOS, 255);
    settle_as(ZMK_OS_WINDOWS, "reset then Windows");
    if (failures) {
        printf("USB observation boundary: FAIL checks=%u\n", failures);
        return 1;
    }
    puts("USB observation boundary: PASS repeated-presence/reset");
    return 0;
}
