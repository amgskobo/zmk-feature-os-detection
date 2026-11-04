#pragma once
#include <zephyr/bluetooth/bluetooth.h>
struct bt_conn {
    bt_addr_le_t dst;
};
enum { BT_CONN_TYPE_LE = 1 };
struct bt_conn_info {
    int type;
    struct {
        uint16_t interval;
    } le;
};
const bt_addr_le_t *bt_conn_get_dst(const struct bt_conn *conn);
int bt_conn_get_info(const struct bt_conn *conn, struct bt_conn_info *info);
struct bt_conn_cb {
    void (*connected)(struct bt_conn *conn, uint8_t err);
    void (*disconnected)(struct bt_conn *conn, uint8_t reason);
    void (*le_param_updated)(struct bt_conn *conn, uint16_t interval, uint16_t latency,
                             uint16_t timeout);
};
int bt_conn_cb_register(struct bt_conn_cb *cb);
