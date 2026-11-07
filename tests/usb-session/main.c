/* SPDX-License-Identifier: MIT */
/* Complete production observer; real kernel work, spinlocks and time. USB
 * packets, disconnect events and the nonblocking core report sink are controlled. */
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include OS_USB_SOURCE

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            printk("FAIL line=%d time=%lld: %s\n", __LINE__, k_uptime_get(), #expr);               \
            exit(3);                                                                               \
        }                                                                                          \
    } while (0)

static enum zmk_os reported = ZMK_OS_UNKNOWN;
static unsigned publications;
static int64_t last_observation, published_at;
K_SEM_DEFINE(entered, 0, 1);
K_SEM_DEFINE(continue_work, 0, 1);
K_SEM_DEFINE(callback_finished, 0, 1);
K_SEM_DEFINE(report_ready, 0, 1);

bool zmk_os_detection_report_usb(enum zmk_os detected) {
    bool changed = reported != detected;
    reported = detected;
    published_at = k_uptime_get();
    ++publications;
    printk("USB report os=%d time=%lld\n", detected, published_at);
    k_sem_give(&report_ready);
    return changed;
}

void zmk_os_detection_log_usb(enum zmk_os detected) { ARG_UNUSED(detected); }

int __real_usb_handle_bos(struct usb_setup_packet *setup, int32_t *len, uint8_t **data) {
    ARG_UNUSED(setup);
    ARG_UNUSED(len);
    ARG_UNUSED(data);
    return -ENOTSUP;
}

static void observe(uint8_t request, uint8_t type, uint16_t length) {
    struct usb_setup_packet setup = {
        .bmRequestType = 0x80,
        .bRequest = request,
        .wValue = (uint16_t)(type << 8),
        .wLength = length,
    };
    last_observation = k_uptime_get();
    zmk_os_detection_observe_setup(&setup);
    printk("USB observe request=%u type=%u length=%u time=%lld\n", request, type, length,
           last_observation);
}

static void (*original_handler)(struct k_work *);
static void gate_before_handler(struct k_work *work) {
    k_sem_give(&entered);
    CHECK(k_sem_take(&continue_work, K_SECONDS(2)) == 0);
    original_handler(work);
    k_sem_give(&callback_finished);
}

static void expect_windows(unsigned expected_publications) {
    /* A fixed upper bound permits ordinary scheduler delay; publication must
     * also satisfy the independent lower quiet-time bound below. */
    CHECK(k_sem_take(&report_ready, K_SECONDS(1)) == 0);
    CHECK(publications == expected_publications);
    CHECK(reported == ZMK_OS_WINDOWS);
    CHECK(published_at - last_observation >= CONFIG_ZMK_OS_DETECTION_USB_SETTLE_MS);
}

int main(void) {
    const char *scenario = getenv("OS_USB_CASE");
    CHECK(scenario);
    bool serial = !strcmp(scenario, "serial");
    bool reset = !strcmp(scenario, "reset");
    bool debounce = !strcmp(scenario, "debounce");
    bool refresh = !strcmp(scenario, "refresh");
    bool disconnected = !strcmp(scenario, "disconnected");
    CHECK(serial || reset || debounce || refresh || disconnected);

    observe(USB_SREQ_SET_ADDRESS, 0, 0);
    observe(USB_SREQ_GET_DESCRIPTOR, USB_DESC_BOS, 255);
    struct k_work_delayable *work = &usb_settle_work;
    original_handler = work->work.handler;
    if (!serial) {
        work->work.handler = gate_before_handler;
        CHECK(k_sem_take(&entered, K_SECONDS(2)) == 0);
        CHECK(k_work_busy_get(&work->work) & K_WORK_RUNNING);
        if (disconnected) {
            struct zmk_usb_conn_state_changed event = {.conn_state = ZMK_USB_CONN_NONE};
            os_detection_usb_listener((const zmk_event_t *)&event);
        } else if (!refresh) {
            observe(USB_SREQ_SET_ADDRESS, 0, 0);
        }
        if (debounce || refresh) {
            observe(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 255);
        }
        k_sem_give(&continue_work);
        CHECK(k_sem_take(&callback_finished, K_SECONDS(2)) == 0);
        work->work.handler = original_handler;
        CHECK(publications == 0);
        CHECK(reported == ZMK_OS_UNKNOWN);

        if (reset || disconnected) {
            k_sleep(K_MSEC(250));
            CHECK(publications == 0);
            observe(USB_SREQ_SET_ADDRESS, 0, 0);
        } else {
            k_sleep(K_MSEC(75));
            CHECK(publications == 0);
        }
        observe(USB_SREQ_GET_DESCRIPTOR, USB_DESC_BOS, 255);
    }
    expect_windows(1);

    /* Neither irrelevant descriptor reads nor other requests rearm work. */
    observe(USB_SREQ_GET_DESCRIPTOR, 1, 2);
    observe(0, USB_DESC_STRING, 2);
    k_sleep(K_MSEC(250));
    CHECK(publications == 1);

    /* The next enumeration is independent and remains usable. */
    observe(USB_SREQ_SET_ADDRESS, 0, 0);
    observe(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 2);
    observe(USB_SREQ_GET_DESCRIPTOR, USB_DESC_STRING, 24);
    CHECK(k_sem_take(&report_ready, K_SECONDS(1)) == 0);
    CHECK(publications == 2 && reported == ZMK_OS_MACOS);
    CHECK(published_at - last_observation >= CONFIG_ZMK_OS_DETECTION_USB_SETTLE_MS);
    printk("PASS usb-session case=%s publications=%u time=%lld\n", scenario, publications,
           k_uptime_get());
    exit(0);
}
