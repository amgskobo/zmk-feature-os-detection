#!/usr/bin/env python3
"""Whole production policy/handler sources with real nanopb and controlled APIs."""

import os
import subprocess
import tempfile
import shutil
from pathlib import Path

root = Path(__file__).resolve().parents[2]
nanopb = Path(
    os.environ.get("NANOPB_SOURCE")
    or subprocess.check_output(
        ["west", "list", "nanopb", "-f", "{abspath}"], text=True
    ).strip()
)
headers = {
    "zephyr/kernel.h": """#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <errno.h>
#define IS_ENABLED(x) (x)
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define BUILD_ASSERT(x,msg) _Static_assert(x,msg)
#define ARG_UNUSED(x) (void)(x)
#define CONTAINER_OF(ptr, type, field) ((type *)((char *)(ptr) - offsetof(type, field)))
#define K_MSEC(ms) (ms)
#define K_FOREVER (-1)
#define CONFIG_APPLICATION_INIT_PRIORITY 90
#define SYS_INIT(fn, level, prio) \
    static int (*const fn##_init_ref)(void) __attribute__((unused)) = fn
struct k_work { void (*handler)(struct k_work *); };
#define K_WORK_DEFINE(name, h) struct k_work name = {.handler = (h)}
int k_work_submit(struct k_work *work);
struct k_work_delayable { struct k_work work; bool pending; int64_t due; };
void k_work_init_delayable(struct k_work_delayable *dwork, void (*handler)(struct k_work *));
int k_work_reschedule(struct k_work_delayable *dwork, int delay_ms);
int k_work_cancel_delayable(struct k_work_delayable *dwork);
struct k_work_delayable *k_work_delayable_from_work(struct k_work *work);
struct k_spinlock { int depth; };
typedef int k_spinlock_key_t;
k_spinlock_key_t k_spin_lock(struct k_spinlock *lock);
void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key);
struct k_mutex { int depth; };
int k_mutex_init(struct k_mutex *mutex);
int k_mutex_lock(struct k_mutex *mutex, int timeout);
int k_mutex_unlock(struct k_mutex *mutex);
int64_t k_uptime_get(void);
""",
    "zephyr/sys/util.h": "#include <zephyr/kernel.h>\n",
    "zephyr/logging/log.h": """#pragma once
#define LOG_MODULE_DECLARE(...)
#define LOG_MODULE_REGISTER(...)
#define LOG_DBG(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_WRN(...) ((void)0)
#define LOG_ERR(...) ((void)0)
""",
    "zephyr/bluetooth/bluetooth.h": """#pragma once
#include <zephyr/kernel.h>
typedef struct { uint8_t id; } bt_addr_le_t;
""",
    "zephyr/bluetooth/conn.h": """#pragma once
#include <zephyr/bluetooth/bluetooth.h>
struct bt_conn { bt_addr_le_t dst; };
enum { BT_CONN_TYPE_LE = 1 };
struct bt_conn_info { int type; struct { uint16_t interval; } le; };
const bt_addr_le_t *bt_conn_get_dst(const struct bt_conn *conn);
int bt_conn_get_info(const struct bt_conn *conn, struct bt_conn_info *info);
struct bt_conn_cb {
    void (*connected)(struct bt_conn *conn, uint8_t err);
    void (*disconnected)(struct bt_conn *conn, uint8_t reason);
    void (*le_param_updated)(struct bt_conn *conn, uint16_t interval, uint16_t latency,
                             uint16_t timeout);
};
int bt_conn_cb_register(struct bt_conn_cb *cb);
""",
    "zephyr/bluetooth/uuid.h": """#pragma once
#include <stdint.h>
struct bt_uuid { uint16_t val; };
int bt_uuid_cmp(const struct bt_uuid *a, const struct bt_uuid *b);
extern const struct bt_uuid uuid_report_map, uuid_hids_info, uuid_report, uuid_pnp_id,
    uuid_appearance, uuid_battery;
#define BT_UUID_HIDS_REPORT_MAP (&uuid_report_map)
#define BT_UUID_HIDS_INFO (&uuid_hids_info)
#define BT_UUID_HIDS_REPORT (&uuid_report)
#define BT_UUID_DIS_PNP_ID (&uuid_pnp_id)
#define BT_UUID_GAP_APPEARANCE (&uuid_appearance)
""",
    "zephyr/bluetooth/gatt.h": """#pragma once
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
struct bt_gatt_attr { const struct bt_uuid *uuid; };
struct bt_gatt_authorization_cb {
    bool (*read_authorize)(struct bt_conn *conn, const struct bt_gatt_attr *attr);
};
struct bt_gatt_cb { void (*att_mtu_updated)(struct bt_conn *conn, uint16_t tx, uint16_t rx); };
int bt_gatt_authorization_cb_register(const struct bt_gatt_authorization_cb *cb);
void bt_gatt_cb_register(struct bt_gatt_cb *cb);
""",
    "zmk/event_manager.h": """#pragma once
typedef int zmk_event_t;
#define ZMK_EV_EVENT_BUBBLE 0
#define ZMK_EVENT_DECLARE(n) int raise_##n(struct n event)
#define ZMK_EVENT_IMPL(n)
#define ZMK_LISTENER(n,h) static int (*const n##_ref)(const zmk_event_t *) __attribute__((unused)) = h
#define ZMK_SUBSCRIPTION(n,e) static const char n##_##e[] __attribute__((unused)) = #e
""",
    "zmk/events/endpoint_changed.h": "#define as_zmk_endpoint_changed(e) ((void)(e), (void *)0)\n",
    "zmk/events/usb_conn_state_changed.h": "#define as_zmk_usb_conn_state_changed(e) ((void)(e), (void *)0)\n",
    "zmk/events/ble_active_profile_changed.h": "#define as_zmk_ble_active_profile_changed(e) ((void)(e), (void *)0)\n",
    "zmk/endpoints.h": """#pragma once
enum zmk_transport { ZMK_TRANSPORT_NONE, ZMK_TRANSPORT_USB, ZMK_TRANSPORT_BLE };
struct zmk_endpoint_instance { enum zmk_transport transport; struct { uint8_t profile_index; } ble; };
struct zmk_endpoint_instance zmk_endpoint_get_selected(void);
""",
    "zmk/usb.h": "bool zmk_usb_is_hid_ready(void);\n",
    "zmk/ble.h": """#pragma once
#include <zephyr/bluetooth/bluetooth.h>
#define ZMK_BLE_PROFILE_COUNT 5
int zmk_ble_profile_index(const bt_addr_le_t *addr);
bool zmk_ble_profile_is_open(uint8_t index);
bool zmk_ble_profile_is_connected(uint8_t index);
int zmk_ble_active_profile_index(void);
""",
    "cormoran/zmk/custom_settings.h": """#pragma once
#include <zmk/event_manager.h>
enum zmk_custom_setting_write_mode { ZMK_CUSTOM_SETTING_WRITE_MODE_PERSIST };
enum { ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32 };
struct zmk_custom_setting { const char *subsystem; const char *key; uint8_t index; };
struct zmk_custom_setting_changed { const struct zmk_custom_setting *setting; };
const struct zmk_custom_setting_changed *as_zmk_custom_setting_changed(const zmk_event_t *eh);
bool zmk_custom_setting_matches(const struct zmk_custom_setting *setting, const char *subsystem,
                                const char *key, const char *prefix);
struct zmk_custom_setting_value { int type; int32_t int32_value; };
#define ZMK_CUSTOM_SETTING_VALUE_INT32(x) ((struct zmk_custom_setting_value){.type=ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,.int32_value=(x)})
#define ZMK_CUSTOM_SETTING_ARRAY_DEFAULT_INT32_DEFINE(n,...) static const int32_t n[] __attribute__((unused)) = {__VA_ARGS__}
#define ZMK_CUSTOM_SETTING_ARRAY_DEFINE(...)
const struct zmk_custom_setting *zmk_custom_setting_find_array_element(const char *,const char *,uint32_t);
int zmk_custom_setting_read(const struct zmk_custom_setting *,struct zmk_custom_setting_value *);
int zmk_custom_setting_write(const struct zmk_custom_setting *,const struct zmk_custom_setting_value *,enum zmk_custom_setting_write_mode);
int zmk_custom_setting_read_array_by_key(const char *, const char *, uint32_t, struct zmk_custom_setting_value *);
int zmk_custom_setting_write_array_by_key(const char *, const char *, uint32_t, const struct zmk_custom_setting_value *, enum zmk_custom_setting_write_mode);
""",
    "zmk/studio/core.h": """#pragma once
enum zmk_studio_core_lock_state {
    ZMK_STUDIO_CORE_LOCK_STATE_LOCKED = 0,
    ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED = 1,
};
enum zmk_studio_core_lock_state zmk_studio_core_get_lock_state(void);
""",
    "zmk/studio/custom.h": """#pragma once
#include <stdio.h>
#include <string.h>
typedef struct { struct { size_t size; uint8_t bytes[128]; } payload; } zmk_custom_CallRequest;
struct zmk_rpc_custom_subsystem_meta { const char *ui_urls; int security; };
#define ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS(url) .ui_urls=(url)
#define ZMK_STUDIO_RPC_HANDLER_UNSECURED 0
#define ZMK_RPC_CUSTOM_SUBSYSTEM(n,m,h) static bool h(const zmk_custom_CallRequest *,pb_callback_t *); static const void *const n##_meta_ref __attribute__((unused)) = m
#define ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER(n,t) static t response_##n
#define ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER_ALLOCATE(n,cb) ((void)(cb),memset(&response_##n,0,sizeof(response_##n)),&response_##n)
""",
}
with tempfile.TemporaryDirectory(prefix="os-override-boundary-") as temporary:
    work = Path(temporary)
    include = work / "include"
    generated = work / "generated"
    generated.mkdir()
    for rel, text in headers.items():
        p = include / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)
    shutil.copytree(nanopb / "generator", work / "generator")
    subprocess.run(
        [
            "python3",
            str(work / "generator/nanopb_generator.py"),
            "-I",
            str(root / "proto"),
            "-D",
            str(generated),
            str(root / "proto/cormoran/os-detection/os_detection.proto"),
        ],
        check=True,
    )
    flags = [
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wshadow",
        "-DCONFIG_ZMK_USB=1",
        "-DCONFIG_ZMK_OS_DETECTION_BLE=1",
        "-DCONFIG_ZMK_CUSTOM_SETTINGS=1",
        "-DCONFIG_ZMK_OS_DETECTION_LAYER_AUTO_SWITCH=0",
        "-DCONFIG_ZMK_OS_DETECTION_TEST_INJECT=0",
        "-DCONFIG_ZMK_OS_DETECTION_BLE_SETTLE_MS=1000",
        "-I" + str(include),
        "-I" + str(root / "include"),
        "-I" + str(generated),
        "-I" + str(nanopb),
    ]
    failed = 0
    for mode, options in [
        ("optimized", ["-O2"]),
        (
            "sanitized",
            [
                "-O1",
                "-g",
                "-fsanitize=address,undefined",
                "-fno-sanitize-recover=all",
                "-fno-pie",
                "-no-pie",
            ],
        ),
    ]:
        binary = work / mode
        subprocess.run(
            [
                "cc",
                *flags,
                *options,
                str(root / "tests/override-boundary/harness.c"),
                *map(str, generated.rglob("*.pb.c")),
                *[
                    str(nanopb / n)
                    for n in ("pb_common.c", "pb_encode.c", "pb_decode.c")
                ],
                "-o",
                str(binary),
            ],
            check=True,
        )
        result = subprocess.run(
            [str(binary)],
            env=dict(
                os.environ,
                ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                UBSAN_OPTIONS="halt_on_error=1",
            ),
        )
        print("OS override boundary", mode, "exit", result.returncode, flush=True)
        failed |= result.returncode != 0
    raise SystemExit(failed)
