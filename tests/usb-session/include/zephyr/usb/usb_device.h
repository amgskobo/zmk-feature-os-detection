/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
#include <stdbool.h>

struct usb_setup_packet {
    uint8_t bmRequestType, bRequest;
    uint16_t wValue, wIndex, wLength;
};
enum {
    USB_SREQ_GET_DESCRIPTOR = 6,
    USB_SREQ_SET_ADDRESS = 5,
    USB_DESC_STRING = 3,
    USB_DESC_BOS = 15,
    USB_DESC_DEVICE_CAPABILITY = 16,
};
#define USB_GET_DESCRIPTOR_TYPE(value) ((value) >> 8)
static inline bool usb_reqtype_is_to_host(const struct usb_setup_packet *setup) {
    return (setup->bmRequestType & 0x80) != 0;
}
