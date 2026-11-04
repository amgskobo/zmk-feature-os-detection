/* SPDX-License-Identifier: MIT */
/* Complete OS adapter + selected registry, real kernel/mutex/event manager.
 * Storage returns and the cross-thread view-reuse handoff are controlled. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <cormoran/zmk/custom_settings.h>
#include "../../src/os_detection_internal.h"

LOG_MODULE_REGISTER(zmk, 0);

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            printk("FAIL line=%d time=%lld: %s\n", __LINE__, k_uptime_get(), #expr);               \
            exit(3);                                                                               \
        }                                                                                          \
    } while (0)

ZMK_CUSTOM_SETTING_ARRAY_DEFAULT_INT32_DEFINE(foreign_defaults, 100, 101, 102, 103, 104, 105, 106,
                                              107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117,
                                              118, 119);
ZMK_CUSTOM_SETTING_ARRAY_DEFINE(foreign, "fixture", "foreign", ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
                                20, 20, foreign_defaults,
                                ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC,
                                ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,
                                ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,
                                ZMK_CUSTOM_SETTING_NO_CONSTRAINT);
extern struct k_mutex custom_settings_lock;
static bool handoff;
static unsigned handoffs;
static int save_error;
static unsigned saves;
static const char *target_key;
int __wrap_settings_save_one(const char *name, const void *value, size_t size) {
    ARG_UNUSED(name);
    ARG_UNUSED(value);
    ARG_UNUSED(size);
    ++saves;
    return save_error;
}
static void churn(struct k_work *work) {
    ARG_UNUSED(work);
    for (unsigned i = 0; i < 20; ++i) {
        CHECK(zmk_custom_setting_find_array_element("fixture", "foreign", i));
    }
}
K_WORK_DEFINE(churn_work, churn);
static void handoff_if_safe(const char *subsystem, const char *key) {
    if (!handoff || strcmp(subsystem, OS_DETECTION_SUBSYSTEM_ID) || strcmp(key, target_key) ||
        custom_settings_lock.owner == k_current_get())
        return;
    handoff = false;
    CHECK(k_work_submit(&churn_work) > 0);
    struct k_work_sync sync;
    k_work_flush(&churn_work, &sync);
    ++handoffs;
}
const struct zmk_custom_setting *
__real_zmk_custom_setting_find_array_element(const char *, const char *, uint32_t);
const struct zmk_custom_setting *__wrap_zmk_custom_setting_find_array_element(const char *subsystem,
                                                                              const char *key,
                                                                              uint32_t index) {
    const struct zmk_custom_setting *view =
        __real_zmk_custom_setting_find_array_element(subsystem, key, index);
    handoff_if_safe(subsystem, key);
    return view;
}
int __real_zmk_custom_setting_read_array_by_key(const char *, const char *, uint32_t,
                                                struct zmk_custom_setting_value *);
int __wrap_zmk_custom_setting_read_array_by_key(const char *subsystem, const char *key,
                                                uint32_t index,
                                                struct zmk_custom_setting_value *value) {
    int ret = __real_zmk_custom_setting_read_array_by_key(subsystem, key, index, value);
    handoff_if_safe(subsystem, key);
    return ret;
}
int __real_zmk_custom_setting_write_array_by_key(const char *, const char *, uint32_t,
                                                 const struct zmk_custom_setting_value *,
                                                 enum zmk_custom_setting_write_mode);
int __wrap_zmk_custom_setting_write_array_by_key(const char *subsystem, const char *key,
                                                 uint32_t index,
                                                 const struct zmk_custom_setting_value *value,
                                                 enum zmk_custom_setting_write_mode mode) {
    int ret = __real_zmk_custom_setting_write_array_by_key(subsystem, key, index, value, mode);
    handoff_if_safe(subsystem, key);
    return ret;
}
static int32_t raw_value(const char *subsystem, const char *key, unsigned index) {
    struct zmk_custom_setting_value value;
    CHECK(zmk_custom_setting_read_array_by_key(subsystem, key, index, &value) == 0);
    return value.int32_value;
}
int main(void) {
    const char *scenario = getenv("OS_SETTINGS_CASE");
    CHECK(scenario);
    bool serial = !strcmp(scenario, "serial");
    bool cache = !strncmp(scenario, "cache-", 6);
    bool reading = strstr(scenario, "read") != NULL;
    target_key = cache ? "ble_detected" : "ble_override";
    CHECK(serial || !strcmp(scenario, "override-write") || !strcmp(scenario, "override-read") ||
          !strcmp(scenario, "cache-write") || !strcmp(scenario, "cache-read"));
    CHECK(settings_subsys_init() == 0);
    if (serial || reading) {
        CHECK(zmk_custom_setting_write_array_by_key(OS_DETECTION_SUBSYSTEM_ID, target_key, 0,
                                                    &ZMK_CUSTOM_SETTING_VALUE_INT32(ZMK_OS_WINDOWS),
                                                    ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY) == 0);
    }
    if (reading) {
        for (unsigned i = 0; i < 20; ++i) {
            CHECK(zmk_custom_setting_write_array_by_key(
                      "fixture", "foreign", i, &ZMK_CUSTOM_SETTING_VALUE_INT32(ZMK_OS_MACOS),
                      ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY) == 0);
        }
    }
    handoff = !serial;
    int result = reading ? (int)(cache ? zmk_os_detection_settings_get_detected_cache(0)
                                       : zmk_os_detection_settings_get_override(0))
                         : (cache ? zmk_os_detection_settings_set_detected_cache(0, ZMK_OS_WINDOWS)
                                  : zmk_os_detection_settings_set_override(0, ZMK_OS_WINDOWS));
    CHECK(!handoff && handoffs == (serial ? 0 : 1));
    CHECK(result == (reading ? ZMK_OS_WINDOWS : 0));
    CHECK(raw_value(OS_DETECTION_SUBSYSTEM_ID, target_key, 0) == ZMK_OS_WINDOWS);
    CHECK(raw_value(OS_DETECTION_SUBSYSTEM_ID, target_key, 1) == ZMK_OS_UNKNOWN);
    CHECK(raw_value(OS_DETECTION_SUBSYSTEM_ID, cache ? "ble_override" : "ble_detected", 0) ==
          ZMK_OS_UNKNOWN);
    for (unsigned i = 0; i < 20; ++i) {
        CHECK(raw_value("fixture", "foreign", i) == (reading ? ZMK_OS_MACOS : 100 + (int)i));
    }
    unsigned before = saves;
    CHECK(zmk_os_detection_settings_set_override(255, ZMK_OS_WINDOWS) == -ENODEV);
    CHECK(zmk_os_detection_settings_set_override(0, (enum zmk_os)6) == -EINVAL);
    CHECK(zmk_os_detection_settings_get_override(255) == ZMK_OS_UNKNOWN && saves == before);
    save_error = -EIO;
    CHECK(zmk_os_detection_settings_set_override(2, ZMK_OS_LINUX) == -EIO);
    CHECK(zmk_os_detection_settings_get_override(2) == ZMK_OS_LINUX);
    save_error = 0;
    CHECK(zmk_os_detection_settings_set_override(2, ZMK_OS_LINUX) == 0);
    CHECK(zmk_os_detection_settings_get_override(2) == ZMK_OS_LINUX);
    printk("PASS settings-view case=%s handoffs=%u saves=%u time=%lld\n", scenario, handoffs, saves,
           k_uptime_get());
    exit(0);
}
