/* SPDX-License-Identifier: MIT */
#pragma once
typedef int zmk_event_t;
#define ZMK_EVENT_DECLARE(n) int raise_##n(struct n event)
#define ZMK_EV_EVENT_BUBBLE 0
#define ZMK_LISTENER(n, h) static const void *n##_ref __attribute__((unused)) = h
#define ZMK_SUBSCRIPTION(...)
