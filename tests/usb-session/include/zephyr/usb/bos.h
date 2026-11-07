/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
#define USB_DEVICE_BOS_DESC_DEFINE_CAP
#define USB_BOS_CAPABILITY_EXTENSION 2
struct usb_bos_capability_lpm {
    uint8_t bLength, bDescriptorType, bDevCapabilityType;
    uint32_t bmAttributes;
};
static inline void usb_bos_register_cap(void *cap) { (void)cap; }
