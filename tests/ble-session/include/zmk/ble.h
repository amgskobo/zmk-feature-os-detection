#pragma once
#include <zephyr/bluetooth/bluetooth.h>
#define ZMK_BLE_PROFILE_COUNT 5
int zmk_ble_profile_index(const bt_addr_le_t *addr);
bool zmk_ble_profile_is_open(uint8_t index);
bool zmk_ble_profile_is_connected(uint8_t index);
int zmk_ble_active_profile_index(void);
