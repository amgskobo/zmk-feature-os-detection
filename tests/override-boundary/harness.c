/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <pb_encode.h>
#include "../../src/os_detection_core.c"
#include "../../src/os_detection_settings.c"
#include "../../src/os_detection_ble.c"
#include "../../src/studio/os_detection_handler.c"

static struct zmk_endpoint_instance endpoint;
static bool usb_ready;
static int persist_error;
static int32_t values[2][ZMK_BLE_PROFILE_COUNT];
static struct zmk_custom_setting settings[2][ZMK_BLE_PROFILE_COUNT];
static unsigned writes;
static unsigned event_count;
static enum zmk_os event_os;
static unsigned failures;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            printf("FAIL line=%u expression=%s\n", __LINE__, #x);                                  \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static enum zmk_studio_core_lock_state lock_state = ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED;
enum zmk_studio_core_lock_state zmk_studio_core_get_lock_state(void) { return lock_state; }
struct zmk_endpoint_instance zmk_endpoint_get_selected(void) { return endpoint; }
bool zmk_usb_is_hid_ready(void) { return usb_ready; }
bool zmk_ble_profile_is_open(uint8_t index) { return index >= ZMK_BLE_PROFILE_COUNT; }
bool zmk_ble_profile_is_connected(uint8_t index) { return index < ZMK_BLE_PROFILE_COUNT; }
int zmk_ble_active_profile_index(void) { return endpoint.ble.profile_index; }
/* The system work queue, run inline: zmk_os_changed must come from there,
 * never from the thread that changed an input (Studio RPC, Bluetooth). */
static bool in_work;
int k_work_submit(struct k_work *work) {
    in_work = true;
    work->handler(work);
    in_work = false;
    return 0;
}
int raise_zmk_os_changed(struct zmk_os_changed event) {
    CHECK(in_work);
    event_count++;
    event_os = event.os;
    return 0;
}
const struct zmk_custom_setting *
zmk_custom_setting_find_array_element(const char *subsystem, const char *key, uint32_t index) {
    if (index >= ZMK_BLE_PROFILE_COUNT) {
        return NULL;
    }
    unsigned row = strcmp(key, "ble_override") == 0 ? 0 : 1;
    settings[row][index] =
        (struct zmk_custom_setting){.subsystem = subsystem, .key = key, .index = (uint8_t)index};
    return &settings[row][index];
}
/* A custom settings change event, from this subsystem or another one. */
static const zmk_event_t setting_changed_event;
static struct zmk_custom_setting_changed setting_changed;
const struct zmk_custom_setting_changed *as_zmk_custom_setting_changed(const zmk_event_t *eh) {
    return eh == &setting_changed_event ? &setting_changed : NULL;
}
bool zmk_custom_setting_matches(const struct zmk_custom_setting *setting, const char *subsystem,
                                const char *key, const char *prefix) {
    (void)key, (void)prefix;
    return strcmp(setting->subsystem, subsystem) == 0;
}

/* Bluetooth: a peer maps to a profile once ZMK has recorded its address, which
 * for a first pairing is only at pairing_complete, after `connected`. */
const struct bt_uuid uuid_report_map = {0x2a4b}, uuid_hids_info = {0x2a4a}, uuid_report = {0x2a4d},
                     uuid_pnp_id = {0x2a50}, uuid_appearance = {0x2a01}, uuid_battery = {0x2a19};
static int profile_of_peer[8];
static const struct bt_gatt_authorization_cb *authorization;
static struct bt_conn_cb *conn_callbacks;
int bt_uuid_cmp(const struct bt_uuid *a, const struct bt_uuid *b) { return a->val != b->val; }
const bt_addr_le_t *bt_conn_get_dst(const struct bt_conn *conn) { return &conn->dst; }
int bt_conn_get_info(const struct bt_conn *conn, struct bt_conn_info *info) {
    (void)conn;
    *info = (struct bt_conn_info){.type = BT_CONN_TYPE_LE, .le.interval = 24};
    return 0;
}
int zmk_ble_profile_index(const bt_addr_le_t *addr) { return profile_of_peer[addr->id]; }
int bt_gatt_authorization_cb_register(const struct bt_gatt_authorization_cb *cb) {
    authorization = cb;
    return 0;
}
void bt_gatt_cb_register(struct bt_gatt_cb *cb) { (void)cb; }
int bt_conn_cb_register(struct bt_conn_cb *cb) {
    conn_callbacks = cb;
    return 0;
}
k_spinlock_key_t k_spin_lock(struct k_spinlock *lock) {
    CHECK(lock->depth++ == 0);
    return 0;
}
void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key) {
    (void)key;
    CHECK(--lock->depth == 0);
}
void k_work_init_delayable(struct k_work_delayable *dwork, void (*handler)(struct k_work *)) {
    *dwork = (struct k_work_delayable){.work.handler = handler};
}
int k_work_reschedule(struct k_work_delayable *dwork, int delay_ms) {
    (void)delay_ms;
    dwork->pending = true;
    return 0;
}
int k_work_cancel_delayable(struct k_work_delayable *dwork) {
    dwork->pending = false;
    return 0;
}
struct k_work_delayable *k_work_delayable_from_work(struct k_work *work) {
    return CONTAINER_OF(work, struct k_work_delayable, work);
}
static void gatt_read(struct bt_conn *conn, const struct bt_uuid *uuid) {
    struct bt_gatt_attr attr = {.uuid = uuid};
    CHECK(authorization->read_authorize(conn, &attr));
}
/* Lets every armed BLE settle debounce expire. */
static unsigned settle_due(void) {
    unsigned fired = 0;
    for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
        if (profile_settle[i].work.pending) {
            profile_settle[i].work.pending = false;
            profile_settle[i].work.work.handler(&profile_settle[i].work.work);
            fired++;
        }
    }
    return fired;
}
int zmk_custom_setting_read(const struct zmk_custom_setting *setting,
                            struct zmk_custom_setting_value *out) {
    unsigned row = strcmp(setting->key, "ble_override") == 0 ? 0 : 1;
    *out = ZMK_CUSTOM_SETTING_VALUE_INT32(values[row][setting->index]);
    return 0;
}
int zmk_custom_setting_write(const struct zmk_custom_setting *setting,
                             const struct zmk_custom_setting_value *value,
                             enum zmk_custom_setting_write_mode mode) {
    (void)mode;
    unsigned row = strcmp(setting->key, "ble_override") == 0 ? 0 : 1;
    values[row][setting->index] = value->int32_value;
    writes++;
    /* Matches the selected Custom Settings owner's tested RAM-before-save policy. */
    return persist_error;
}
static void reset(void) {
    memset(values, 0, sizeof(values));
    memset(ble_detected, 0, sizeof(ble_detected));
    usb_detected = ZMK_OS_UNKNOWN;
    last_raised = ZMK_OS_UNKNOWN;
    endpoint = (struct zmk_endpoint_instance){.transport = ZMK_TRANSPORT_BLE};
    persist_error = 0;
    usb_ready = false;
    event_count = 0;
    event_os = ZMK_OS_UNKNOWN;
    lock_state = ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED;
    writes = 0;
    memset(profile_stats, 0, sizeof(profile_stats));
    for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
        profile_settle[i].conn = NULL;
        profile_settle[i].settled = false;
        profile_settle[i].work.pending = false;
    }
    for (size_t i = 0; i < ARRAY_SIZE(profile_of_peer); i++) {
        profile_of_peer[i] = -ENODEV;
    }
}
static cormoran_os_detection_Response request(uint32_t index) {
    cormoran_os_detection_Request request_data = cormoran_os_detection_Request_init_zero;
    request_data.which_request_type = cormoran_os_detection_Request_set_ble_override_tag;
    request_data.request_type.set_ble_override.profile_index = index;
    request_data.request_type.set_ble_override.os = cormoran_os_detection_Os_OS_WINDOWS;
    zmk_custom_CallRequest raw = {0};
    pb_ostream_t stream = pb_ostream_from_buffer(raw.payload.bytes, sizeof(raw.payload.bytes));
    CHECK(pb_encode(&stream, cormoran_os_detection_Request_fields, &request_data));
    raw.payload.size = stream.bytes_written;
    pb_callback_t callback = {0};
    CHECK(os_detection_rpc_handle_request(&raw, &callback));
    uint8_t bytes[512];
    stream = pb_ostream_from_buffer(bytes, sizeof(bytes));
    CHECK(pb_encode(&stream, cormoran_os_detection_Response_fields,
                    &response_cormoran__os_detection));
    pb_istream_t input = pb_istream_from_buffer(bytes, stream.bytes_written);
    cormoran_os_detection_Response decoded = cormoran_os_detection_Response_init_zero;
    CHECK(pb_decode(&input, cormoran_os_detection_Response_fields, &decoded));
    return decoded;
}
int main(void) {
    reset();
    zmk_os_detection_report_ble_detected(0, ZMK_OS_MACOS);
    CHECK(event_count == 1);
    persist_error = -EIO;
    CHECK(zmk_os_detection_set_ble_override(0, ZMK_OS_WINDOWS) == -EIO);
    CHECK(zmk_os_detection_current() == ZMK_OS_WINDOWS);
    CHECK(event_count == 2 && event_os == ZMK_OS_WINDOWS);
    CHECK(zmk_os_detection_set_ble_override(0, ZMK_OS_WINDOWS) == -EIO);
    CHECK(event_count == 2);
    persist_error = 0;
    CHECK(zmk_os_detection_set_ble_override(0, ZMK_OS_WINDOWS) == 0);
    CHECK(event_count == 2);
    CHECK(zmk_os_detection_set_ble_override(5, ZMK_OS_LINUX) == -EINVAL);
    CHECK(event_count == 2);
    CHECK(zmk_os_detection_set_ble_override(0, (enum zmk_os)999) == -EINVAL);
    CHECK(event_count == 2);
    persist_error = -EIO;
    CHECK(zmk_os_detection_set_ble_override(0, ZMK_OS_UNKNOWN) == -EIO);
    CHECK(event_count == 3 && event_os == ZMK_OS_MACOS);
    endpoint.transport = ZMK_TRANSPORT_USB;
    usb_ready = true;
    zmk_os_detection_report_usb(ZMK_OS_LINUX);
    unsigned before = event_count;
    CHECK(zmk_os_detection_set_ble_override(1, ZMK_OS_WINDOWS) == -EIO);
    CHECK(event_count == before);
    printf("notification checks events=%u failures=%u\n", event_count, failures);

    const uint32_t invalid[] = {
        5, 256, UINT32_C(0x80000000), UINT32_C(0x80000001), UINT32_C(0xffffff00), UINT32_MAX};
    for (size_t i = 0; i < ARRAY_SIZE(invalid); i++) {
        reset();
        cormoran_os_detection_Response response = request(invalid[i]);
        CHECK(response.which_response_type == cormoran_os_detection_Response_error_tag);
        CHECK(strcmp(response.response_type.error.message, "Invalid profile or OS") == 0);
        for (size_t profile = 0; profile < ZMK_BLE_PROFILE_COUNT; profile++) {
            CHECK(values[0][profile] == ZMK_OS_UNKNOWN);
        }
        printf("RPC index=%u tag=%u failures=%u\n", invalid[i], response.which_response_type,
               failures);
    }
    /* A locked Studio cannot change an override; nothing is written or raised. */
    reset();
    lock_state = ZMK_STUDIO_CORE_LOCK_STATE_LOCKED;
    cormoran_os_detection_Response response = request(4);
    CHECK(response.which_response_type == cormoran_os_detection_Response_error_tag);
    CHECK(strcmp(response.response_type.error.message, "Unlock required") == 0);
    CHECK(values[0][4] == ZMK_OS_UNKNOWN && event_count == 0);
    printf("RPC locked index=4 tag=%u failures=%u\n", response.which_response_type, failures);

    /* A failed save still applied the override in RAM; the error says so. */
    reset();
    persist_error = -EIO;
    response = request(2);
    CHECK(response.which_response_type == cormoran_os_detection_Response_error_tag);
    CHECK(strcmp(response.response_type.error.message, "Override applied but not saved: -5") == 0);
    CHECK(values[0][2] == ZMK_OS_WINDOWS);
    printf("RPC unsaved index=2 failures=%u\n", failures);

    reset();
    response = request(4);
    CHECK(response.which_response_type == cormoran_os_detection_Response_set_ble_override_tag);
    CHECK(response.response_type.set_ble_override.has_profile);
    CHECK(response.response_type.set_ble_override.profile.index == 4);
    CHECK(values[0][4] == ZMK_OS_WINDOWS);
    /* Generic Custom Settings imports and persisted values bypass the OS RPC
     * setter. Invalid classifications must stay unavailable to core/API/wire. */
    const int32_t invalid_os[] = {-1, 6, INT32_MIN, INT32_MAX};
    for (size_t i = 0; i < ARRAY_SIZE(invalid_os); i++) {
        reset();
        values[0][0] = values[1][0] = invalid_os[i];
        values[0][1] = ZMK_OS_WINDOWS;
        ble_detected[0] = ZMK_OS_MACOS;
        CHECK(zmk_os_detection_settings_get_override(0) == ZMK_OS_UNKNOWN);
        CHECK(zmk_os_detection_settings_get_detected_cache(0) == ZMK_OS_UNKNOWN);
        CHECK(zmk_os_detection_current() == ZMK_OS_MACOS);
        CHECK(zmk_os_detection_settings_get_override(1) == ZMK_OS_WINDOWS);
        CHECK(values[0][0] == invalid_os[i] && values[1][0] == invalid_os[i]);
        response = (cormoran_os_detection_Response)cormoran_os_detection_Response_init_zero;
        response.which_response_type = cormoran_os_detection_Response_state_tag;
        handle_get_state(&response.response_type.state);
        uint8_t wire[512];
        pb_ostream_t encoded = pb_ostream_from_buffer(wire, sizeof(wire));
        CHECK(pb_encode(&encoded, cormoran_os_detection_Response_fields, &response));
        cormoran_os_detection_Response decoded = cormoran_os_detection_Response_init_zero;
        pb_istream_t input = pb_istream_from_buffer(wire, encoded.bytes_written);
        CHECK(pb_decode(&input, cormoran_os_detection_Response_fields, &decoded));
        CHECK(decoded.response_type.state.ble_profiles[0].override ==
              cormoran_os_detection_Os_OS_UNKNOWN);
        CHECK(decoded.response_type.state.current_effective == cormoran_os_detection_Os_OS_MACOS);
        CHECK(event_count == 0);
        printf("stored OS=%d failures=%u\n", invalid_os[i], failures);
    }
    for (int32_t os = ZMK_OS_UNKNOWN; os <= ZMK_OS_ANDROID; os++) {
        reset();
        values[0][0] = values[1][0] = os;
        CHECK(zmk_os_detection_settings_get_override(0) == (enum zmk_os)os);
        CHECK(zmk_os_detection_settings_get_detected_cache(0) == (enum zmk_os)os);
    }

    /* An override written past this module's RPC (the generic custom settings
     * RPC) changes the effective OS; another subsystem's change does not. */
    reset();
    values[0][0] = ZMK_OS_LINUX;
    setting_changed.setting = &(const struct zmk_custom_setting){.subsystem = "other"};
    os_detection_listener(&setting_changed_event);
    CHECK(event_count == 0);
    setting_changed.setting =
        &(const struct zmk_custom_setting){.subsystem = OS_DETECTION_SUBSYSTEM_ID};
    os_detection_listener(&setting_changed_event);
    CHECK(event_count == 1 && event_os == ZMK_OS_LINUX);
    printf("external override events=%u failures=%u\n", event_count, failures);

    /* The boot settings load applies a persisted override without a change
     * event; settings-initialized must still raise it. The subscription
     * symbols exist only when the listener subscribes to these events. */
    reset();
    values[0][0] = ZMK_OS_MACOS;
    const zmk_event_t initialized_event = 0;
    os_detection_listener(&initialized_event);
    CHECK(event_count == 1 && event_os == ZMK_OS_MACOS);
    CHECK(strcmp(os_detection_core_zmk_custom_settings_initialized,
                 "zmk_custom_settings_initialized") == 0);
    CHECK(strcmp(os_detection_core_zmk_custom_setting_changed, "zmk_custom_setting_changed") == 0);
    CHECK(strcmp(os_detection_core_zmk_endpoint_changed, "zmk_endpoint_changed") == 0);
    CHECK(strcmp(os_detection_core_zmk_ble_active_profile_changed,
                 "zmk_ble_active_profile_changed") == 0);
    printf("boot override events=%u failures=%u\n", event_count, failures);

    /* BLE fingerprints, with the real os_detection_ble.c on controlled
     * Bluetooth callbacks. A bonded host is fingerprinted on its profile. */
    reset();
    CHECK(os_detection_ble_init() == 0 && authorization && conn_callbacks);
    struct bt_conn host_a = {.dst.id = 1}, host_b = {.dst.id = 2};
    profile_of_peer[1] = 0;
    conn_callbacks->connected(&host_a, 0);
    gatt_read(&host_a, BT_UUID_HIDS_REPORT_MAP);
    gatt_read(&host_a, BT_UUID_DIS_PNP_ID);
    gatt_read(&host_a, BT_UUID_GAP_APPEARANCE);
    gatt_read(&host_a, &uuid_battery);
    CHECK(settle_due() == 1);
    CHECK(ble_detected[0] == ZMK_OS_WINDOWS && values[1][0] == ZMK_OS_WINDOWS);
    gatt_read(&host_a, BT_UUID_HIDS_INFO);
    CHECK(settle_due() == 0);
    conn_callbacks->disconnected(&host_a, 0);

    /* A new host paired into the same profile: `connected` cannot map it yet,
     * but its fingerprint is still taken once pairing completes. */
    profile_of_peer[1] = -ENODEV;
    conn_callbacks->connected(&host_b, 0);
    profile_of_peer[2] = 0;
    gatt_read(&host_b, BT_UUID_HIDS_REPORT_MAP);
    gatt_read(&host_b, BT_UUID_HIDS_INFO);
    CHECK(settle_due() == 1);
    CHECK(ble_detected[0] == ZMK_OS_ANDROID && values[1][0] == ZMK_OS_ANDROID);

    /* Its bond is cleared while connected, and Zephyr hands the same
     * connection object to the next host paired there. */
    profile_of_peer[2] = -ENODEV;
    conn_callbacks->disconnected(&host_b, 0);
    host_b.dst.id = 3;
    conn_callbacks->connected(&host_b, 0);
    profile_of_peer[3] = 0;
    gatt_read(&host_b, BT_UUID_HIDS_REPORT_MAP);
    gatt_read(&host_b, BT_UUID_DIS_PNP_ID);
    gatt_read(&host_b, BT_UUID_GAP_APPEARANCE);
    CHECK(settle_due() == 1);
    CHECK(ble_detected[0] == ZMK_OS_WINDOWS);
    printf("BLE re-pairing failures=%u\n", failures);

    /* On reconnect the cached guess applies at once, and is not written
     * back (this runs on the Bluetooth thread). */
    reset();
    endpoint.ble.profile_index = 2;
    values[1][2] = ZMK_OS_MACOS;
    profile_of_peer[1] = 2;
    conn_callbacks->connected(&host_a, 0);
    CHECK(ble_detected[2] == ZMK_OS_MACOS && writes == 0);
    CHECK(event_count == 1 && event_os == ZMK_OS_MACOS);
    printf("BLE cached guess writes=%u failures=%u\n", writes, failures);

    if (failures) {
        printf("OS override boundary: FAIL checks=%u\n", failures);
        return 1;
    }
    puts("OS override boundary: PASS notification/failure/retry/invalid/independent/real-wire");
    return 0;
}
