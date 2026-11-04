#pragma once
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
struct bt_gatt_attr {
    const struct bt_uuid *uuid;
};
struct bt_gatt_authorization_cb {
    bool (*read_authorize)(struct bt_conn *conn, const struct bt_gatt_attr *attr);
};
struct bt_gatt_cb {
    void (*att_mtu_updated)(struct bt_conn *conn, uint16_t tx, uint16_t rx);
};
int bt_gatt_authorization_cb_register(const struct bt_gatt_authorization_cb *cb);
void bt_gatt_cb_register(struct bt_gatt_cb *cb);
