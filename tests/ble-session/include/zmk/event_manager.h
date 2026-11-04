#pragma once
typedef int zmk_event_t;
#define ZMK_EV_EVENT_BUBBLE 0
#define ZMK_EVENT_DECLARE(n) int raise_##n(struct n event)
#define ZMK_EVENT_IMPL(n)
#define ZMK_LISTENER(n, h)                                                                         \
    static int (*const n##_ref)(const zmk_event_t *) __attribute__((unused)) = h
#define ZMK_SUBSCRIPTION(n, e) static const char n##_##e[] __attribute__((unused)) = #e
