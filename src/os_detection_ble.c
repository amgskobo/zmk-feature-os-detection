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

/* See docs/fingerprints.md for what's real vs. placeholder here. Real
 * captures (2026-07-05: Windows, a real Linux/BlueZ desktop, and real
 * Android) found GAP Appearance is the one signal that actually splits
 * them: Windows and desktop Linux both read it, Android doesn't (even
 * though Android - like desktop Linux - reads Report Map + HIDS Info + DIS
 * PnP ID). PnP ID presence/absence, the original assumed Windows/Linux
 * split, turned out NOT to distinguish anything - all three read it.
 *
 * Unlike over USB (where Android shares desktop Linux's kernel-level USB
 * stack and is deliberately reported as ZMK_OS_LINUX, see
 * os_detection_usb.c), Android's BLE GATT read pattern IS distinguishable
 * from desktop Linux's - so it gets its own ZMK_OS_ANDROID value here
 * instead of folding into ZMK_OS_LINUX. One consequence worth knowing: with
 * only Windows/Android/iPhone real captures backing this logic,
 * `zmk_os_classify_ble()` currently has no path that returns ZMK_OS_LINUX
 * at all - a real desktop Linux capture (which reads Appearance) lands on
 * the Windows rule below instead, per the existing ambiguity decision. */
enum zmk_os zmk_os_classify_ble(const struct ble_fp_stats *stats) {
    if (stats->report_map_reads == 0 && stats->hids_info_reads == 0 && stats->pnp_id_reads == 0 &&
        stats->appearance_reads == 0) {
        return ZMK_OS_UNKNOWN;
    }

    /* Windows (VERIFIED, 2026-07-05 real capture) reads GAP Appearance in
     * addition to DIS PnP ID and the HIDS report map. Checked first because
     * a real Linux/BlueZ desktop capture the same day showed this exact
     * same pattern too (see module comment) - Windows has the larger
     * install base, so this genuinely ambiguous case deliberately resolves
     * to Windows rather than Linux (per module owner, 2026-07-05). */
    if (stats->pnp_id_reads > 0 && stats->appearance_reads > 0) {
        return ZMK_OS_WINDOWS;
    }

    /* Android (VERIFIED, 2026-07-05 real capture): Report Map + HIDS Info +
     * (usually) DIS PnP ID get read, but GAP Appearance never does - the
     * one signal that actually separated it from the real Windows/Linux
     * desktop captures above (both of which DO read Appearance). "No
     * Appearance" is the real Android signal, not "no PnP ID" as originally
     * (wrongly) assumed - see the module comment. Per module owner
     * (2026-07-05): reported as its own ZMK_OS_ANDROID rather than folded
     * into ZMK_OS_LINUX, since this pattern - unlike over USB - actually
     * distinguishes it from real desktop Linux. */
    if (stats->report_map_reads > 0 && stats->hids_info_reads > 0 && stats->appearance_reads == 0) {
        return ZMK_OS_ANDROID;
    }

    /* Fallback: some HIDS access happened but didn't match a clearer
     * pattern above. This is also where real iOS lands today (VERIFIED,
     * 2026-07-05 real iPhone capture: only HIDS Report Map was read, same
     * as macOS): no BLE signal here tells iOS from macOS (an ANCS/AMS GATT
     * client probe was planned but never implemented, see
     * docs/fingerprints.md). Defaulting this ambiguous case
     * to macOS rather than iOS is deliberate (per module owner, 2026-07-05):
     * same reasoning as defaulting the Windows/Linux ambiguity to Windows -
     * pick the platform value more likely to be right/more common when the
     * signals genuinely can't tell them apart. */
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
    /* The connection the stats belong to (compared, never dereferenced). A
     * first pairing only maps to its profile once pairing completes, after
     * the connected callback, so its first read starts the profile afresh. */
    const struct bt_conn *conn;
    uint8_t profile_index;
    bool settled;
};

static struct ble_profile_settle profile_settle[ZMK_BLE_PROFILE_COUNT];

/* profile_stats, `conn` and `settled` are written on the Bluetooth thread
 * (GATT reads, connection callbacks) and read by the settle work on the
 * system work queue. */
static struct k_spinlock stats_lock;

/* Starts a profile's fingerprint afresh for `conn`. Call with stats_lock held. */
static void start_profile_locked(int profile_index, const struct bt_conn *conn, uint16_t interval) {
    profile_stats[profile_index] = (struct ble_fp_stats){.initial_conn_interval = interval};
    profile_settle[profile_index].conn = conn;
    profile_settle[profile_index].settled = false;
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

    /* Latch first: any read arriving after this is ignored until reconnect. */
    k_spinlock_key_t key = k_spin_lock(&stats_lock);
    settle->settled = true;
    struct ble_fp_stats stats = profile_stats[profile_index];
    k_spin_unlock(&stats_lock, key);

    enum zmk_os detected = zmk_os_classify_ble(&stats);
    LOG_DBG("os detection: BLE fingerprint stats profile=%d report_map=%u hids_info=%u "
            "report_ref=%u pnp_id=%u appearance=%u mtu=%u interval=%u -> os=%d",
            profile_index, stats.report_map_reads, stats.hids_info_reads, stats.report_ref_reads,
            stats.pnp_id_reads, stats.appearance_reads, stats.att_mtu, stats.initial_conn_interval,
            detected);
    zmk_os_detection_report_ble_detected(profile_index, detected);
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
    k_spinlock_key_t key = k_spin_lock(&stats_lock);
    if (profile_settle[profile_index].conn != conn) {
        start_profile_locked(profile_index, conn, 0);
    }
    bool counted = !profile_settle[profile_index].settled;
    if (counted && *counter < UINT8_MAX) {
        (*counter)++;
    }
    k_spin_unlock(&stats_lock, key);

    /* Classify once the discovery burst goes quiet (see ble_settle_work_handler),
     * rather than re-running it on every read as fingerprint signals trickle in. */
    if (counted) {
        k_work_reschedule(&profile_settle[profile_index].work,
                          K_MSEC(CONFIG_ZMK_OS_DETECTION_BLE_SETTLE_MS));
    }
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
    k_spinlock_key_t key = k_spin_lock(&stats_lock);
    profile_stats[profile_index].att_mtu = rx;
    k_spin_unlock(&stats_lock, key);
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
    k_work_cancel_delayable(&profile_settle[profile_index].work);
    struct bt_conn_info info;
    bool have_info = bt_conn_get_info(conn, &info) == 0 && info.type == BT_CONN_TYPE_LE;

    k_spinlock_key_t key = k_spin_lock(&stats_lock);
    start_profile_locked(profile_index, conn, have_info ? info.le.interval : 0);
    k_spin_unlock(&stats_lock, key);

#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)
    /* Adopt the cached guess immediately; re-detection above will correct
     * it in the background if the fingerprint has changed since last time. */
    enum zmk_os cached = zmk_os_detection_settings_get_detected_cache(profile_index);
    if (cached != ZMK_OS_UNKNOWN) {
        zmk_os_detection_adopt_ble_cached(profile_index, cached);
    }
#endif
}

static void os_detection_conn_param_updated(struct bt_conn *conn, uint16_t interval,
                                            uint16_t latency, uint16_t timeout) {
    ARG_UNUSED(latency);
    ARG_UNUSED(timeout);
    int profile_index = profile_index_for_conn(conn);
    if (profile_index < 0 || profile_index >= ZMK_BLE_PROFILE_COUNT) {
        return;
    }
    k_spinlock_key_t key = k_spin_lock(&stats_lock);
    if (profile_stats[profile_index].initial_conn_interval == 0) {
        profile_stats[profile_index].initial_conn_interval = interval;
    }
    k_spin_unlock(&stats_lock, key);
}

static void os_detection_conn_disconnected(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(reason);
    /* Matched by connection, not address: a bond cleared meanwhile no longer
     * maps to its profile, and Zephyr reuses connection objects. Drop any
     * pending settle so it can't fire against a disconnected profile. */
    for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
        k_spinlock_key_t key = k_spin_lock(&stats_lock);
        bool owned = profile_settle[i].conn == conn;
        if (owned) {
            profile_settle[i].conn = NULL;
        }
        k_spin_unlock(&stats_lock, key);
        if (owned) {
            k_work_cancel_delayable(&profile_settle[i].work);
        }
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
