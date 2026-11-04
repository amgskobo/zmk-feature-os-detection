/*
 * Copyright (c) 2026 cormoran
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include <zmk/ble.h>

#include "os_detection_internal.h"

#ifndef CONFIG_ZMK_OS_DETECTION_FORK_LOG_LEVEL
#define CONFIG_ZMK_OS_DETECTION_FORK_LOG_LEVEL 3
#endif
LOG_MODULE_REGISTER(zmk_os_detection_fork_os_detection_ble, CONFIG_ZMK_OS_DETECTION_FORK_LOG_LEVEL);

/* Ordered BLE read-set heuristics; see docs/fingerprints.md for limitations.
 * PnP ID plus Appearance selects WINDOWS, including an ambiguous Linux/BlueZ
 * read set. Report Map plus HIDS Info without Appearance selects ANDROID.
 * PnP ID alone does not identify Windows/Linux. Shared USB shapes may instead
 * classify Android as LINUX. Automatic BLE detection has no LINUX or IOS
 * return path; those values remain available as manual profile overrides. */
enum zmk_os zmk_os_classify_ble(const struct ble_fp_stats *stats) {
    if (stats->report_map_reads == 0 && stats->hids_info_reads == 0 && stats->pnp_id_reads == 0 &&
        stats->appearance_reads == 0) {
        return ZMK_OS_UNKNOWN;
    }

    /* Check PnP ID plus GAP Appearance first. This ambiguous desktop read set
     * maps to WINDOWS and cannot establish a unique Windows/Linux identity. */
    if (stats->pnp_id_reads > 0 && stats->appearance_reads > 0) {
        return ZMK_OS_WINDOWS;
    }

    /* Report Map plus HIDS Info without Appearance maps to ANDROID. PnP ID
     * may be present or absent; a different host with this shape can collide. */
    if (stats->report_map_reads > 0 && stats->hids_info_reads > 0 && stats->appearance_reads == 0) {
        return ZMK_OS_ANDROID;
    }

    /* Any other nonempty read set maps to MACOS, including Report Map alone.
     * No ANCS/AMS client probe distinguishes macOS from iOS here. */
    return ZMK_OS_MACOS;
}

static struct ble_fp_stats profile_stats[ZMK_BLE_PROFILE_COUNT];

/* Per-profile debounce, mirroring the USB settle timer in os_detection_usb.c.
 * A host reads the fingerprint-relevant characteristics in a burst during GATT
 * service discovery, then keeps polling unrelated ones (Battery Level, 0x2a19)
 * for the life of the connection. `settled` latches once that discovery burst
 * has gone quiet so subsequent reads - including the endless battery polls -
 * are ignored until the profile reconnects (which clears the latch). */
struct ble_profile_settle {
    struct k_work_delayable work;
    struct k_mutex lock;
    /* The connection the stats belong to (compared, never dereferenced). A
     * first pairing only maps to its profile once pairing completes, after
     * the connected callback, so its first read starts the profile afresh. */
    const struct bt_conn *conn;
    uint8_t profile_index;
    bool settled;
    int64_t deadline;
};

static struct ble_profile_settle profile_settle[ZMK_BLE_PROFILE_COUNT];

/* Bluetooth callbacks and settle work run in thread context. The per-profile
 * mutex covers classification/publication too: a disconnect cannot replace a
 * session between its snapshot and the corresponding cache/report update. */

/* Starts a profile afresh. Call with its mutex held. */
static void start_profile_locked(int profile_index, const struct bt_conn *conn, uint16_t interval) {
    /* Park queued work without CANCELING a running callback. A new read may
     * rearm this same work before that old callback returns. */
    k_work_reschedule(&profile_settle[profile_index].work, K_FOREVER);
    profile_stats[profile_index] = (struct ble_fp_stats){.initial_conn_interval = interval};
    profile_settle[profile_index].conn = conn;
    profile_settle[profile_index].settled = false;
    profile_settle[profile_index].deadline = 0;
}

static int profile_index_for_conn(struct bt_conn *conn) {
    if (!conn) {
        return -1;
    }
    return zmk_ble_profile_index(bt_conn_get_dst(conn));
}

static void ble_settle_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct ble_profile_settle *settle = CONTAINER_OF(dwork, struct ble_profile_settle, work);
    uint8_t profile_index = settle->profile_index;

    k_mutex_lock(&settle->lock, K_FOREVER);
    if (!settle->conn || settle->settled || settle->deadline == 0) {
        k_mutex_unlock(&settle->lock);
        return;
    }
    int64_t remaining = settle->deadline - k_uptime_get();
    if (remaining > 0) {
        /* This may be an already-running callback from an earlier session or
         * an earlier read. Only the current quiet deadline may consume stats. */
        k_work_reschedule(dwork, K_MSEC(remaining));
        k_mutex_unlock(&settle->lock);
        return;
    }

    /* Latch first: later reads are ignored until reconnect. Keep ownership
     * through publication so old results cannot land in a replacement session. */
    settle->settled = true;
    settle->deadline = 0;
    struct ble_fp_stats stats = profile_stats[profile_index];

    enum zmk_os detected = zmk_os_classify_ble(&stats);
    LOG_DBG("os detection: BLE fingerprint stats profile=%d report_map=%u hids_info=%u "
            "report_ref=%u pnp_id=%u appearance=%u mtu=%u interval=%u -> os=%d",
            profile_index, stats.report_map_reads, stats.hids_info_reads, stats.report_ref_reads,
            stats.pnp_id_reads, stats.appearance_reads, stats.att_mtu, stats.initial_conn_interval,
            detected);
    zmk_os_detection_report_ble_detected(profile_index, detected);
    k_mutex_unlock(&settle->lock);
}

static bool os_detection_ble_read_authorize(struct bt_conn *conn, const struct bt_gatt_attr *attr) {
    int profile_index = profile_index_for_conn(conn);
    if (profile_index < 0 || profile_index >= ZMK_BLE_PROFILE_COUNT) {
        return true; /* never block access; observation only */
    }

    /* Not a fingerprint signal (e.g. Battery Level, polled indefinitely):
     * crucially don't restart the settle debounce below, otherwise continuous
     * polling would keep detection from finalizing. */
    uint8_t *counter = NULL;
    if (bt_uuid_cmp(attr->uuid, BT_UUID_HIDS_REPORT_MAP) == 0) {
        counter = &profile_stats[profile_index].report_map_reads;
    } else if (bt_uuid_cmp(attr->uuid, BT_UUID_HIDS_INFO) == 0) {
        counter = &profile_stats[profile_index].hids_info_reads;
    } else if (bt_uuid_cmp(attr->uuid, BT_UUID_HIDS_REPORT) == 0) {
        counter = &profile_stats[profile_index].report_ref_reads;
    } else if (bt_uuid_cmp(attr->uuid, BT_UUID_DIS_PNP_ID) == 0) {
        counter = &profile_stats[profile_index].pnp_id_reads;
    } else if (bt_uuid_cmp(attr->uuid, BT_UUID_GAP_APPEARANCE) == 0) {
        counter = &profile_stats[profile_index].appearance_reads;
    } else {
        return true;
    }

    /* Once the fingerprint is finalized for this connection, reads are
     * ignored until the profile reconnects. */
    struct ble_profile_settle *settle = &profile_settle[profile_index];
    k_mutex_lock(&settle->lock, K_FOREVER);
    if (settle->conn != conn) {
        start_profile_locked(profile_index, conn, 0);
    }
    bool counted = !settle->settled;
    if (counted && *counter < UINT8_MAX) {
        (*counter)++;
    }
    /* Classify once the discovery burst goes quiet (see ble_settle_work_handler),
     * rather than re-running it on every read as fingerprint signals trickle in. */
    if (counted) {
        settle->deadline = k_uptime_get() + CONFIG_ZMK_OS_DETECTION_BLE_SETTLE_MS;
        k_work_reschedule(&settle->work, K_MSEC(CONFIG_ZMK_OS_DETECTION_BLE_SETTLE_MS));
    }
    k_mutex_unlock(&settle->lock);
    return true;
}

static struct bt_gatt_authorization_cb os_detection_gatt_authorization_cb = {
    .read_authorize = os_detection_ble_read_authorize,
};

static void os_detection_att_mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx) {
    ARG_UNUSED(tx);
    int profile_index = profile_index_for_conn(conn);
    if (profile_index < 0 || profile_index >= ZMK_BLE_PROFILE_COUNT) {
        return;
    }
    struct ble_profile_settle *settle = &profile_settle[profile_index];
    k_mutex_lock(&settle->lock, K_FOREVER);
    if (settle->conn == conn) {
        profile_stats[profile_index].att_mtu = rx;
    }
    k_mutex_unlock(&settle->lock);
}

static struct bt_gatt_cb os_detection_gatt_cb = {
    .att_mtu_updated = os_detection_att_mtu_updated,
};

static void os_detection_conn_connected(struct bt_conn *conn, uint8_t err) {
    if (err) {
        return;
    }
    int profile_index = profile_index_for_conn(conn);
    if (profile_index < 0 || profile_index >= ZMK_BLE_PROFILE_COUNT) {
        return;
    }
    struct bt_conn_info info;
    bool have_info = bt_conn_get_info(conn, &info) == 0 && info.type == BT_CONN_TYPE_LE;

    struct ble_profile_settle *settle = &profile_settle[profile_index];
    k_mutex_lock(&settle->lock, K_FOREVER);
    start_profile_locked(profile_index, conn, have_info ? info.le.interval : 0);

#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)
    /* Adopt the cached guess immediately; re-detection above will correct
     * it in the background if the fingerprint has changed since last time. */
    enum zmk_os cached = zmk_os_detection_settings_get_detected_cache(profile_index);
    if (cached != ZMK_OS_UNKNOWN) {
        zmk_os_detection_adopt_ble_cached(profile_index, cached);
    }
#endif
    k_mutex_unlock(&settle->lock);
}

static void os_detection_conn_param_updated(struct bt_conn *conn, uint16_t interval,
                                            uint16_t latency, uint16_t timeout) {
    ARG_UNUSED(latency);
    ARG_UNUSED(timeout);
    int profile_index = profile_index_for_conn(conn);
    if (profile_index < 0 || profile_index >= ZMK_BLE_PROFILE_COUNT) {
        return;
    }
    struct ble_profile_settle *settle = &profile_settle[profile_index];
    k_mutex_lock(&settle->lock, K_FOREVER);
    if (settle->conn == conn && profile_stats[profile_index].initial_conn_interval == 0) {
        profile_stats[profile_index].initial_conn_interval = interval;
    }
    k_mutex_unlock(&settle->lock);
}

static void os_detection_conn_disconnected(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(reason);
    /* Matched by connection, not address: a bond cleared meanwhile no longer
     * maps to its profile, and Zephyr reuses connection objects. Drop any
     * pending settle so it can't fire against a disconnected profile. */
    for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
        struct ble_profile_settle *settle = &profile_settle[i];
        k_mutex_lock(&settle->lock, K_FOREVER);
        if (settle->conn == conn) {
            settle->conn = NULL;
            settle->deadline = 0;
            k_work_reschedule(&settle->work, K_FOREVER);
        }
        k_mutex_unlock(&settle->lock);
    }
}

static struct bt_conn_cb os_detection_conn_cb = {
    .connected = os_detection_conn_connected,
    .disconnected = os_detection_conn_disconnected,
    .le_param_updated = os_detection_conn_param_updated,
};

static int os_detection_ble_init(void) {
    for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
        profile_settle[i].profile_index = (uint8_t)i;
        k_mutex_init(&profile_settle[i].lock);
        k_work_init_delayable(&profile_settle[i].work, ble_settle_work_handler);
    }
    /* Zephyr takes a single GATT authorization callback; without it no
     * fingerprint read is seen and every BLE profile stays unknown. */
    int err = bt_gatt_authorization_cb_register(&os_detection_gatt_authorization_cb);
    if (err) {
        LOG_ERR("os detection: GATT authorization callback not registered (%d); "
                "BLE detection is off",
                err);
    }
    bt_gatt_cb_register(&os_detection_gatt_cb);
    err = bt_conn_cb_register(&os_detection_conn_cb);
    if (err) {
        LOG_ERR("os detection: connection callbacks not registered (%d)", err);
    }
    return 0;
}

SYS_INIT(os_detection_ble_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
