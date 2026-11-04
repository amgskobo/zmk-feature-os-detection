"""Restore one OS ownership defect in an isolated container source copy."""

import argparse
from pathlib import Path
import re


FAULTS = {
    "quiet-deadline": (
        "src/os_detection_ble.c",
        "if (remaining > 0) {",
        "if (false && remaining > 0) {",
    ),
    "publication-lock": (
        "src/os_detection_ble.c",
        "zmk_os_detection_report_ble_detected(profile_index, detected);",
        "k_mutex_unlock(&settle->lock); zmk_os_detection_report_ble_detected(profile_index, detected); k_mutex_lock(&settle->lock, K_FOREVER);",
    ),
    "borrowed-read": (
        "src/os_detection_settings.c",
        "if (zmk_custom_setting_read_array_by_key(OS_DETECTION_SUBSYSTEM_ID, key, profile_index, &value) != 0) {",
        "const struct zmk_custom_setting *borrowed = zmk_custom_setting_find_array_element(OS_DETECTION_SUBSYSTEM_ID, key, profile_index); if (!borrowed || zmk_custom_setting_read(borrowed, &value) != 0) {",
    ),
    "borrowed-write": (
        "src/os_detection_settings.c",
        "int ret = zmk_custom_setting_write_array_by_key(OS_DETECTION_SUBSYSTEM_ID, key, profile_index, &ZMK_CUSTOM_SETTING_VALUE_INT32(os), mode);",
        "const struct zmk_custom_setting *borrowed = zmk_custom_setting_find_array_element(OS_DETECTION_SUBSYSTEM_ID, key, profile_index); int ret = borrowed ? zmk_custom_setting_write(borrowed, &ZMK_CUSTOM_SETTING_VALUE_INT32(os), mode) : -ENOENT;",
    ),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fault", choices=FAULTS)
    parser.add_argument("isolated_owner", type=Path)
    args = parser.parse_args()
    owner = Path(__file__).resolve().parents[1]
    isolated = args.isolated_owner.resolve()
    if isolated == owner or isolated.is_relative_to(owner):
        parser.error("Use an isolated container source copy")
    relative, old, new = FAULTS[args.fault]
    path = isolated / relative
    content = path.read_text()
    tokens = re.findall(r"[A-Za-z_]\w*|\d+|==|!=|<=|>=|>>|<<|\|\||&&|->|\S", old)
    pattern = r"\s*".join(re.escape(token) for token in tokens)
    if len(list(re.finditer(pattern, content))) != 1:
        parser.error("Expected exactly one control site")
    path.write_text(re.sub(pattern, lambda _: new, content, count=1))
    print("Applied isolated negative control: " + args.fault)


if __name__ == "__main__":
    main()
