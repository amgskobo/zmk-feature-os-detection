/* SPDX-License-Identifier: MIT */
/* Complete production observer and real kernel work/mutex/threads. Bluetooth
 * mapping, core report/cache sinks and scheduling gates are controlled. */
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include OS_BLE_SOURCE

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            printk("FAIL line=%d time=%lld: %s\n", __LINE__, k_uptime_get(), #expr);               \
            exit(3);                                                                               \
        }                                                                                          \
    } while (0)

const struct bt_uuid uuid_report_map = {0x2a4b}, uuid_hids_info = {0x2a4a}, uuid_report = {0x2a4d},
                     uuid_pnp_id = {0x2a50}, uuid_appearance = {0x2a01}, uuid_battery = {0x2a19};
static int mapping[8];
static struct bt_conn_cb *callbacks;
static const struct bt_gatt_authorization_cb *authorization;
static enum zmk_os detected[5], cached[5];
static unsigned publications;
static bool publication_gate;
K_SEM_DEFINE(entered, 0, 1);
K_SEM_DEFINE(continue_work, 0, 1);
K_SEM_DEFINE(callback_finished, 0, 1);
K_SEM_DEFINE(reconnect_started, 0, 1);
K_SEM_DEFINE(reconnect_done, 0, 1);

int bt_uuid_cmp(const struct bt_uuid *a, const struct bt_uuid *b) { return a->val != b->val; }
const bt_addr_le_t *bt_conn_get_dst(const struct bt_conn *conn) { return &conn->dst; }
int bt_conn_get_info(const struct bt_conn *conn, struct bt_conn_info *info) {
    ARG_UNUSED(conn);
    *info = (struct bt_conn_info){.type = BT_CONN_TYPE_LE, .le.interval = 24};
    return 0;
}
int zmk_ble_profile_index(const bt_addr_le_t *addr) { return mapping[addr->id]; }
int bt_gatt_authorization_cb_register(const struct bt_gatt_authorization_cb *cb) {
    authorization = cb;
    return 0;
}
void bt_gatt_cb_register(struct bt_gatt_cb *cb) { ARG_UNUSED(cb); }
int bt_conn_cb_register(struct bt_conn_cb *cb) {
    callbacks = cb;
    return 0;
}
enum zmk_os zmk_os_detection_settings_get_detected_cache(uint8_t profile) {
    return cached[profile];
}
void zmk_os_detection_adopt_ble_cached(uint8_t profile, enum zmk_os os) { detected[profile] = os; }
void zmk_os_detection_report_ble_detected(uint8_t profile, enum zmk_os os) {
    if (publication_gate) {
        publication_gate = false;
        k_sem_give(&entered);
        CHECK(k_sem_take(&continue_work, K_SECONDS(2)) == 0);
    }
    detected[profile] = cached[profile] = os;
    ++publications;
}
static void read_uuid(struct bt_conn *conn, const struct bt_uuid *uuid) {
    struct bt_gatt_attr attr = {.uuid = uuid};
    CHECK(authorization->read_authorize(conn, &attr));
}
static void windows_reads(struct bt_conn *conn) {
    read_uuid(conn, BT_UUID_HIDS_REPORT_MAP);
    read_uuid(conn, BT_UUID_DIS_PNP_ID);
    read_uuid(conn, BT_UUID_GAP_APPEARANCE);
}
static void android_reads(struct bt_conn *conn) {
    read_uuid(conn, BT_UUID_HIDS_REPORT_MAP);
    read_uuid(conn, BT_UUID_HIDS_INFO);
}
static void (*original_handler)(struct k_work *);
static void gate_before_handler(struct k_work *work) {
    k_sem_give(&entered);
    CHECK(k_sem_take(&continue_work, K_SECONDS(2)) == 0);
    original_handler(work);
    k_sem_give(&callback_finished);
}
static void reconnect(void *old, void *fresh, void *unused) {
    ARG_UNUSED(unused);
    k_sem_give(&reconnect_started);
    callbacks->disconnected(old, 0);
    callbacks->connected(fresh, 0);
    k_sem_give(&reconnect_done);
}
K_THREAD_STACK_DEFINE(reconnect_stack, 2048);
static struct k_thread reconnect_thread;

int main(void) {
    const char *scenario = getenv("OS_BLE_CASE");
    CHECK(scenario && callbacks && authorization);
    bool serial = !strcmp(scenario, "serial");
    bool latch = !strcmp(scenario, "latch");
    bool early = !strcmp(scenario, "early");
    bool publish = !strcmp(scenario, "publication");
    bool disconnect = !strcmp(scenario, "disconnected");
    CHECK(serial || latch || early || publish || disconnect);
    for (int i = 0; i < 8; ++i)
        mapping[i] = -ENODEV;
    mapping[1] = mapping[2] = 0;
    cached[0] = ZMK_OS_MACOS;
    cached[1] = detected[1] = ZMK_OS_IOS;
    struct bt_conn old = {.dst.id = 1}, fresh = {.dst.id = 1};
    callbacks->connected(&old, 0);
    windows_reads(&old);
    struct k_work_delayable *work = &profile_settle[0].work;
    original_handler = work->work.handler;
    if (latch || early || disconnect) {
        work->work.handler = gate_before_handler;
        CHECK(k_sem_take(&entered, K_SECONDS(2)) == 0);
        CHECK(k_work_busy_get(&work->work) & K_WORK_RUNNING);
        callbacks->disconnected(&old, 0);
        if (!disconnect)
            callbacks->connected(&fresh, 0);
        if (early)
            android_reads(&fresh);
        k_sem_give(&continue_work);
        /* Wait for this invocation without flushing its delayable work:
         * a fresh delayed reservation must retain its real future deadline. */
        CHECK(k_sem_take(&callback_finished, K_SECONDS(2)) == 0);
        work->work.handler = original_handler;
        CHECK(!profile_settle[0].settled);
        CHECK(detected[0] == ZMK_OS_MACOS && cached[0] == ZMK_OS_MACOS);
        if (disconnect) {
            CHECK(profile_settle[0].conn == NULL);
            k_sleep(K_MSEC(1100));
            CHECK(publications == 0);
            callbacks->connected(&fresh, 0);
        }
    } else if (publish) {
        publication_gate = true;
        CHECK(k_sem_take(&entered, K_SECONDS(2)) == 0);
        k_tid_t tid = k_thread_create(&reconnect_thread, reconnect_stack,
                                      K_THREAD_STACK_SIZEOF(reconnect_stack), reconnect, &old,
                                      &fresh, NULL, 5, 0, K_NO_WAIT);
        CHECK(k_sem_take(&reconnect_started, K_SECONDS(1)) == 0);
        CHECK(k_sem_take(&reconnect_done, K_MSEC(20)) == -EAGAIN);
        CHECK(profile_settle[0].conn == &old);
        k_sem_give(&continue_work);
        CHECK(k_sem_take(&reconnect_done, K_SECONDS(1)) == 0);
        CHECK(k_thread_join(tid, K_SECONDS(1)) == 0);
        CHECK(profile_settle[0].conn == &fresh && !profile_settle[0].settled);
    } else {
        k_sleep(K_MSEC(1100));
        CHECK(detected[0] == ZMK_OS_WINDOWS && cached[0] == ZMK_OS_WINDOWS);
        callbacks->disconnected(&old, 0);
        callbacks->connected(&fresh, 0);
    }
    if (!early)
        android_reads(&fresh);
    CHECK(profile_stats[0].report_map_reads == 1 && profile_stats[0].hids_info_reads == 1);
    CHECK(!profile_settle[0].settled && k_work_delayable_is_pending(work));
    /* Unrelated polling does not postpone the actual quiet deadline. */
    for (unsigned i = 0; i < 5; ++i) {
        k_sleep(K_MSEC(220));
        read_uuid(&fresh, &uuid_battery);
    }
    CHECK(profile_settle[0].settled);
    CHECK(detected[0] == ZMK_OS_ANDROID && cached[0] == ZMK_OS_ANDROID);
    CHECK(detected[1] == ZMK_OS_IOS && cached[1] == ZMK_OS_IOS);
    unsigned completed = publications;
    windows_reads(&fresh);
    k_sleep(K_MSEC(1100));
    CHECK(publications == completed && detected[0] == ZMK_OS_ANDROID);
    /* A subsequent ordinary reconnect must remain usable. */
    callbacks->disconnected(&fresh, 0);
    callbacks->connected(&fresh, 0);
    windows_reads(&fresh);
    k_sleep(K_MSEC(1100));
    CHECK(detected[0] == ZMK_OS_WINDOWS && cached[0] == ZMK_OS_WINDOWS);
    printk("PASS ble-session case=%s publications=%u independent=%d time=%lld\n", scenario,
           publications, detected[1], k_uptime_get());
    exit(0);
}
