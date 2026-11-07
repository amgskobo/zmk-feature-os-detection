/* SPDX-License-Identifier: MIT */
#pragma once
#include <zmk/event_manager.h>
enum { ZMK_USB_CONN_NONE };
struct zmk_usb_conn_state_changed {
    int conn_state;
};
static inline const struct zmk_usb_conn_state_changed *
as_zmk_usb_conn_state_changed(const zmk_event_t *event) {
    return (const struct zmk_usb_conn_state_changed *)event;
}
