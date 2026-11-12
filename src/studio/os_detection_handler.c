#include <errno.h>
#include <stdio.h>

#include <pb_decode.h>
#include <pb_encode.h>
#include <zephyr/sys/util.h>
#include <zmk/studio/core.h>
#include <zmk/studio/custom.h>
#include <cormoran/os-detection/os_detection.pb.h>

#include "../os_detection_internal.h"

#if IS_ENABLED(CONFIG_ZMK_OS_DETECTION_BLE)
#include <zmk/ble.h>
BUILD_ASSERT(ZMK_BLE_PROFILE_COUNT <=
                 ARRAY_SIZE(((cormoran_os_detection_StateResponse *)NULL)->ble_profiles),
             "every BLE profile must fit one StateResponse");
#endif

#include <zephyr/logging/log.h>
#ifndef CONFIG_ZMK_OS_DETECTION_FORK_LOG_LEVEL
#define CONFIG_ZMK_OS_DETECTION_FORK_LOG_LEVEL 3
#endif
LOG_MODULE_REGISTER(zmk_os_detection_fork_os_detection_handler,
                    CONFIG_ZMK_OS_DETECTION_FORK_LOG_LEVEL);

static struct zmk_rpc_custom_subsystem_meta os_detection_meta = {
    ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS("https://cormoran.github.io/zmk-feature-os-detection/"),
    // Unsecured is suggested by default to avoid unlocking in un-reliable
    // environments.
    .security = ZMK_STUDIO_RPC_HANDLER_UNSECURED,
};

ZMK_RPC_CUSTOM_SUBSYSTEM(cormoran__os_detection, &os_detection_meta,
                         os_detection_rpc_handle_request);

ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER(cormoran__os_detection, cormoran_os_detection_Response);

static void set_error(cormoran_os_detection_Response *resp, const char *message) {
    resp->which_response_type = cormoran_os_detection_Response_error_tag;
    snprintf(resp->response_type.error.message, sizeof(resp->response_type.error.message), "%s",
             message);
}

/* enum zmk_os starts at ZMK_OS_UNKNOWN=0; cormoran_os_detection_Os starts at
 * OS_UNSPECIFIED=0 with OS_UNKNOWN=1, so the proto value is always exactly
 * one more than the firmware value. */
static cormoran_os_detection_Os zmk_os_to_proto(enum zmk_os os) {
    return (cormoran_os_detection_Os)(os + 1);
}

/* Returns -1 for a proto value that doesn't map to a valid enum zmk_os. */
static int proto_to_zmk_os(cormoran_os_detection_Os os) {
    if (os == cormoran_os_detection_Os_OS_UNSPECIFIED) {
        return ZMK_OS_UNKNOWN;
    }
    if (os >= cormoran_os_detection_Os_OS_UNKNOWN && os <= cormoran_os_detection_Os_OS_ANDROID) {
        return (int)os - 1;
    }
    return -1;
}

static void fill_ble_profile_state(uint8_t index, cormoran_os_detection_BleProfileState *out) {
    out->index = index;
    out->bonded = zmk_os_detection_ble_profile_bonded(index);
    out->connected = zmk_os_detection_ble_profile_connected(index);
    out->detected = zmk_os_to_proto(zmk_os_detection_ble_profile_detected(index));
    out->override = zmk_os_to_proto(zmk_os_detection_ble_profile_override(index));
    /* Reuse this record's values: a second settings read could see an override
     * changed by another thread and contradict the override we just reported. */
    out->effective =
        out->override == cormoran_os_detection_Os_OS_UNKNOWN ? out->detected : out->override;
}

static void handle_get_state(cormoran_os_detection_StateResponse *out) {
    out->has_usb = true;
    out->usb.connected = zmk_os_detection_usb_connected();
    out->usb.detected = zmk_os_to_proto(zmk_os_detection_usb_detected());

    int profile_count = zmk_os_detection_ble_profile_count();
    int max_profiles = ARRAY_SIZE(out->ble_profiles);
    out->ble_profiles_count = 0;
    for (int i = 0; i < profile_count && i < max_profiles; i++) {
        fill_ble_profile_state(i, &out->ble_profiles[out->ble_profiles_count]);
        out->ble_profiles_count++;
    }

    out->active_profile_index = zmk_os_detection_selected_ble_profile_index();
    out->current_effective = zmk_os_to_proto(zmk_os_detection_current());
}

static int handle_set_ble_override(const cormoran_os_detection_SetBleOverrideRequest *req,
                                   cormoran_os_detection_Response *resp) {
    int profile_count = zmk_os_detection_ble_profile_count();
    if (req->profile_index >= (uint32_t)profile_count) {
        return -EINVAL;
    }

    int os = proto_to_zmk_os(req->os);
    if (os < 0) {
        return -EINVAL;
    }

    int rc = zmk_os_detection_set_ble_override(req->profile_index, (enum zmk_os)os);
    if (rc != 0) {
        return rc;
    }

    resp->which_response_type = cormoran_os_detection_Response_set_ble_override_tag;
    resp->response_type.set_ble_override.has_profile = true;
    fill_ble_profile_state(req->profile_index, &resp->response_type.set_ble_override.profile);
    return 0;
}

static bool os_detection_rpc_handle_request(const zmk_custom_CallRequest *raw_request,
                                            pb_callback_t *encode_response) {
    cormoran_os_detection_Response *resp =
        ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER_ALLOCATE(cormoran__os_detection, encode_response);

    cormoran_os_detection_Request req = cormoran_os_detection_Request_init_zero;

    pb_istream_t req_stream =
        pb_istream_from_buffer(raw_request->payload.bytes, raw_request->payload.size);
    if (!pb_decode(&req_stream, cormoran_os_detection_Request_fields, &req)) {
        LOG_WRN("Failed to decode os_detection request: %s", PB_GET_ERROR(&req_stream));
        set_error(resp, "Failed to decode request");
        return true;
    }

    switch (req.which_request_type) {
    case cormoran_os_detection_Request_get_state_tag:
        resp->which_response_type = cormoran_os_detection_Response_state_tag;
        handle_get_state(&resp->response_type.state);
        return true;
    case cormoran_os_detection_Request_set_ble_override_tag:
        break;
    default:
        LOG_WRN("Unsupported os_detection request type: %d", req.which_request_type);
        set_error(resp, "Unsupported request");
        return true;
    }

    /* The override settings declare SECURE write permission, but this RPC
     * writes them directly, and the subsystem stays UNSECURED so the state
     * can be read while Studio is locked. An override can switch the default
     * layer, so only an unlocked Studio may change it. */
    if (zmk_studio_core_get_lock_state() != ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED) {
        LOG_WRN("OS override change refused: Studio is locked");
        set_error(resp, "Unlock required");
        return true;
    }

    int rc = handle_set_ble_override(&req.request_type.set_ble_override, resp);
    if (rc == -EINVAL) {
        set_error(resp, "Invalid profile or OS");
    } else if (rc == -ENOTSUP) {
        set_error(resp, "Overrides need BLE detection and Custom Settings");
    } else if (rc != 0) {
        /* Custom Settings keeps the new RAM value when saving fails. */
        resp->which_response_type = cormoran_os_detection_Response_error_tag;
        snprintf(resp->response_type.error.message, sizeof(resp->response_type.error.message),
                 "Override applied but not saved: %d", rc);
    }
    return true;
}

#if IS_ENABLED(CONFIG_ZMK_OS_DETECTION_TEST_INJECT)
#include "../../tests/support/os_detection_handler_assertions_0.inc"
#endif /* CONFIG_ZMK_OS_DETECTION_TEST_INJECT */
