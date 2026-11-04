#pragma once
#include <stdint.h>
struct bt_uuid {
    uint16_t val;
};
int bt_uuid_cmp(const struct bt_uuid *a, const struct bt_uuid *b);
extern const struct bt_uuid uuid_report_map, uuid_hids_info, uuid_report, uuid_pnp_id,
    uuid_appearance, uuid_battery;
#define BT_UUID_HIDS_REPORT_MAP (&uuid_report_map)
#define BT_UUID_HIDS_INFO (&uuid_hids_info)
#define BT_UUID_HIDS_REPORT (&uuid_report)
#define BT_UUID_DIS_PNP_ID (&uuid_pnp_id)
#define BT_UUID_GAP_APPEARANCE (&uuid_appearance)
