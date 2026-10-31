#include "init_result.h"
/* SPDX-License-Identifier: MIT */

/* Exercises the handler's actual business logic (handle_get_state /
 * handle_set_ble_override) directly, rather than round-tripping through the
 * generic nanopb bytes-field encode plumbing shared by every custom
 * subsystem in the template - that plumbing is unmodified template code,
 * not specific to this module. */
static int os_detection_handler_test_init(void) {
    cormoran_os_detection_StateResponse state = cormoran_os_detection_StateResponse_init_zero;
    handle_get_state(&state);
    LOG_INF("os detection: get_state usb_connected=%d usb_detected=%d ble_profiles_count=%d "
            "current_effective=%d",
            state.usb.connected, state.usb.detected, state.ble_profiles_count,
            state.current_effective);

    cormoran_os_detection_SetBleOverrideRequest override_req = {
        .profile_index = 0,
        .os = cormoran_os_detection_Os_OS_WINDOWS,
    };
    cormoran_os_detection_Response override_resp = cormoran_os_detection_Response_init_zero;
    int rc = handle_set_ble_override(&override_req, &override_resp);
    LOG_INF("os detection: set_ble_override(profile=0, WINDOWS) rc=%d", rc);

    return 0;
}

OWNER_CHECKED_SYS_INIT(os_detection_handler_test_init, APPLICATION,
                       CONFIG_APPLICATION_INIT_PRIORITY);
