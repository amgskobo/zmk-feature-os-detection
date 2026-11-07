#!/usr/bin/env python3
"""Owner kernel boundaries; build once, repeat each scenario in a fresh process."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

from sanitizer_output import sanitizer_diagnostic

ROOT = Path(__file__).resolve().parent.parent
CASES = {
    "usb-session": ["serial", "reset", "debounce", "refresh", "disconnected"],
    "ble-session": ["serial", "latch", "early", "publication", "disconnected"],
    "settings-view": [
        "serial",
        "override-write",
        "override-read",
        "cache-write",
        "cache-read",
    ],
}


def selected_project(name):
    return Path(
        subprocess.check_output(
            ["west", "list", name, "-f", "{abspath}"], text=True
        ).strip()
    ).resolve()


def validate_output_path(requested, source_roots):
    requested = Path(requested)
    if not requested.is_absolute():
        raise ValueError("output must be an absolute path")
    if requested.exists() or requested.is_symlink():
        raise ValueError("output already exists (file, directory or symlink)")
    try:
        parent = requested.parent.resolve(strict=True)
    except OSError as exc:
        raise ValueError("output parent must exist") from exc
    output = parent / requested.name
    if any(output.is_relative_to(Path(root).resolve()) for root in source_roots):
        raise ValueError("output must be outside owner and dependency sources")
    return output


def inputs(family, custom):
    files = list((ROOT / "tests" / family).rglob("*"))
    files += [ROOT / "tests/kernel_boundary.py", ROOT / "src/os_detection_internal.h"]
    files.append(ROOT / "tests/sanitizer_output.py")
    files += list((ROOT / "include").rglob("*.h"))
    source = {
        "usb-session": "os_detection_usb.c",
        "ble-session": "os_detection_ble.c",
        "settings-view": "os_detection_settings.c",
    }[family]
    files.append(ROOT / "src" / source)
    if custom:
        files += list((custom / "src").rglob("*.c"))
        files += list((custom / "include").rglob("*"))
    return {
        str(p): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in sorted(files)
        if p.is_file()
    }


def kernel_result_passed(returncode, raw, marker):
    return (
        returncode == 0
        and marker in raw
        and not sanitizer_diagnostic(raw)
        and not re.search(
            r"ASSERTION FAIL|ZEPHYR FATAL|HARNESS FAIL|^FAIL ", raw, re.MULTILINE
        )
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("family", choices=CASES)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    zephyr = Path(os.environ.get("ZEPHYR_BASE") or selected_project("zephyr")).resolve()
    custom = None
    zmk = None
    if args.family == "settings-view":
        custom = Path(
            os.environ.get("CUSTOM_SETTINGS_MODULE")
            or selected_project("zmk-feature-custom-settings")
        ).resolve()
        zmk = Path(os.environ.get("ZMK_ROOT") or selected_project("zmk")).resolve()
    source_roots = [ROOT, zephyr]
    source_roots += [root for root in (custom, zmk) if root is not None]
    source_roots += subprocess.check_output(
        ["west", "list", "-f", "{abspath}"], text=True, cwd=ROOT
    ).splitlines()
    try:
        output = validate_output_path(args.output, source_roots)
    except ValueError as exc:
        parser.error(str(exc))
    output.mkdir(exist_ok=False)
    command = [
        "cmake",
        "-S",
        str(ROOT / "tests" / args.family),
        "-B",
        str(output / "build"),
        "-G",
        "Ninja",
        "-DBOARD=native_sim/native/64",
        "-DWEST=OFF",
        "-DZEPHYR_MODULES=",
        "-DZEPHYR_BASE=" + str(zephyr),
    ]
    if args.family == "settings-view":
        command += ["-DCUSTOM_SETTINGS_MODULE=" + str(custom), "-DZMK_ROOT=" + str(zmk)]
    before = inputs(args.family, custom)
    (output / "inputs.json").write_text(json.dumps(before, indent=2) + "\n")
    commands = []
    for name, cmd in [
        ("configure", command),
        ("build", ["cmake", "--build", str(output / "build"), "--parallel", "1"]),
    ]:
        with (output / (name + ".log")).open("w") as log:
            result = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, cwd=ROOT)
        commands.append({"command": cmd, "exit": result.returncode})
        (output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if result.returncode:
            raise SystemExit(f"{name} failed: {output / (name + '.log')}")
    binary = output / "build/zephyr/zephyr.exe"
    binary_sha = hashlib.sha256(binary.read_bytes()).hexdigest()
    results = []
    failed = False
    for case in CASES[args.family]:
        for repeat in range(1, 4):
            directory = output / case / str(repeat)
            directory.mkdir(parents=True)
            env = dict(os.environ)
            env[
                {
                    "usb-session": "OS_USB_CASE",
                    "ble-session": "OS_BLE_CASE",
                    "settings-view": "OS_SETTINGS_CASE",
                }[args.family]
            ] = case
            with (directory / "raw.log").open("w") as log:
                result = subprocess.run(
                    [str(binary)],
                    cwd=directory,
                    env=env,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    timeout=20,
                )
            raw = (directory / "raw.log").read_text()
            marker = f"PASS {args.family} case={case}"
            passed = kernel_result_passed(result.returncode, raw, marker)
            record = {
                "case": case,
                "repeat": repeat,
                "command": [str(binary)],
                "cwd": str(directory),
                "exit": result.returncode,
                "passed": passed,
                "binary_sha256": binary_sha,
                "raw_sha256": hashlib.sha256(raw.encode()).hexdigest(),
            }
            (directory / "command.json").write_text(json.dumps(record, indent=2) + "\n")
            results.append(record)
            failed |= not passed
            print(args.family, case, repeat, "PASS" if passed else "FAIL", flush=True)
    after = inputs(args.family, custom)
    (output / "results.json").write_text(
        json.dumps({"runs": results, "inputs_unchanged": before == after}, indent=2)
        + "\n"
    )
    if before != after:
        raise SystemExit("kernel fixture inputs changed during execution")
    raise SystemExit(int(failed))


if __name__ == "__main__":
    main()
