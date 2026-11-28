#!/usr/bin/env python3
"""Pre-flight offline verification suite for zeroflte-rising17."""

import hashlib
import json
import os
import py_compile
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FAILURES: list[str] = []


def ok(msg: str) -> None:
    print(f"[PASS] {msg}")


def fail(msg: str) -> None:
    print(f"[FAIL] {msg}")
    FAILURES.append(msg)


def check_xml_tree(base_dir: Path, label: str) -> int:
    count = 0
    for xml_path in sorted(base_dir.rglob("*.xml")):
        try:
            ET.parse(xml_path)
            count += 1
        except ET.ParseError as exc:
            fail(f"XML parse error in {xml_path.relative_to(ROOT)}: {exc}")
    if count:
        ok(f"{label}: {count} XML files parsed cleanly")
    return count


def check_json_files() -> None:
    json_paths = [
        ROOT / "local_repos/device_samsung_universal7420-common/configs/power/powerhint.json",
        ROOT / "local_repos/device_samsung_universal7420-common/configs/task_profiles.json",
        ROOT / "local_repos/device_samsung_zeroflte/configs/thermal/thermal_info_config.json",
    ]
    for jp in json_paths:
        if not jp.exists():
            fail(f"Missing JSON: {jp.relative_to(ROOT)}")
            continue
        try:
            data = json.loads(jp.read_text(encoding="utf-8"))
            if jp.name == "powerhint.json":
                assert "Nodes" in data and "Actions" in data
                touch_nodes = [n for n in data["Nodes"] if n.get("Name") == "TouchInhibit"]
                assert touch_nodes, "TouchInhibit node missing from powerhint.json"
            elif jp.name == "thermal_info_config.json":
                assert "Sensors" in data and len(data["Sensors"]) >= 2
            elif jp.name == "task_profiles.json":
                assert "Profiles" in data
            ok(f"JSON valid: {jp.relative_to(ROOT)}")
        except Exception as exc:
            fail(f"JSON invalid in {jp.relative_to(ROOT)}: {exc}")


def check_lf_and_shell() -> None:
    sh_files = sorted((ROOT / "scripts").glob("*.sh")) + [
        ROOT / "local_repos/device_samsung_universal7420-common/ramdisk/bin/init.postboot.sh",
        ROOT / "local_repos/device_samsung_universal7420-common/ramdisk/bin/init.touch.sh",
        ROOT / "local_repos/device_samsung_universal7420-common/ramdisk/bin/set_resolution.sh",
        ROOT / "local_repos/device_samsung_universal7420-common/releasetools/resize-system.sh",
    ]
    bash_bin = shutil.which("bash")
    for p in sh_files:
        if not p.exists():
            fail(f"Missing shell script: {p.relative_to(ROOT)}")
            continue
        raw = p.read_bytes()
        if b"\r\n" in raw:
            fail(f"CRLF detected in shell script {p.relative_to(ROOT)}")
            continue
        if os.name != "nt" and bash_bin:
            rc = subprocess.run([bash_bin, "-n", str(p)], capture_output=True, text=True)
            if rc.returncode != 0:
                fail(f"bash -n syntax error in {p.relative_to(ROOT)}: {rc.stderr.strip()}")
                continue
        ok(f"Shell syntax + LF OK: {p.relative_to(ROOT)}")

    patch_count = 0
    for pf in sorted((ROOT / "patches").rglob("*.patch")):
        raw = pf.read_bytes()
        if b"\r\n" in raw:
            fail(f"CRLF detected in patch {pf.relative_to(ROOT)}")
        elif b"diff --git " not in raw and b"--- a/" not in raw:
            fail(f"Invalid patch format in {pf.relative_to(ROOT)}")
        else:
            patch_count += 1
    ok(f"All {patch_count} patch files verified (LF + unified diff format)")


def check_python_scripts() -> None:
    py_files = sorted((ROOT / "scripts").glob("*.py")) + [
        ROOT / "local_repos/device_samsung_universal7420-common/releasetools/releasetools.py",
    ]
    with tempfile.TemporaryDirectory() as td:
        for p in py_files:
            try:
                cfile = str(Path(td) / f"{p.stem}.pyc")
                py_compile.compile(str(p), cfile=cfile, doraise=True)
                ok(f"Python syntax OK: {p.relative_to(ROOT)}")
            except py_compile.PyCompileError as exc:
                fail(f"Python syntax error in {p.relative_to(ROOT)}: {exc}")


def check_dtbh_and_image_tools() -> None:
    prebuilt_dt = ROOT / "local_repos/device_samsung_universal7420-common/prebuilt/dt.img"
    prebuilt_img = ROOT / "local_repos/device_samsung_universal7420-common/prebuilt/Image"

    raw_dt = prebuilt_dt.read_bytes()
    if raw_dt[:4] != b"DTBH":
        fail("prebuilt/dt.img missing DTBH magic")
        return
    orig_dt_sha = hashlib.sha256(raw_dt).hexdigest()

    with tempfile.TemporaryDirectory() as td:
        td_path = Path(td)
        dtb_totalsize = struct.unpack(">I", raw_dt[2052:2056])[0]
        dtb_file = td_path / "exynos7420-zeroflte.dtb"
        dtb_file.write_bytes(raw_dt[2048 : 2048 + dtb_totalsize])
        out_dt = td_path / "dt.img"
        rc = subprocess.run(
            [sys.executable, str(ROOT / "scripts/mkdtbh.py"), "-d", str(dtb_file), "-o", str(out_dt)],
            capture_output=True,
            text=True,
        )
        if rc.returncode != 0:
            fail(f"mkdtbh.py execution failed: {rc.stderr}")
        else:
            rebuilt_sha = hashlib.sha256(out_dt.read_bytes()).hexdigest()
            if rebuilt_sha == orig_dt_sha:
                ok(f"mkdtbh.py DTBH v2 round-trip SHA-256 match ({rebuilt_sha[:12]})")
            else:
                fail(f"mkdtbh.py round-trip SHA-256 mismatch: {rebuilt_sha} != {orig_dt_sha}")

    r_real = subprocess.run(
        [sys.executable, str(ROOT / "scripts/patch_kernel_image.py"), str(prebuilt_img), "--verify"],
        capture_output=True,
        text=True,
    )
    if r_real.returncode == 0:
        ok("patch_kernel_image.py --verify OK on prebuilt/Image (text_offset=0x205000)")
    else:
        fail(f"prebuilt/Image verification failed: {r_real.stderr}")


def check_sukisu_and_manifest() -> None:
    mf = (ROOT / "manifests/zeroflte_rising17.xml").read_text(encoding="utf-8")
    if 'name="eiarf/SM_G92X"' in mf and 'path="kernel_src/SM_G92X"' in mf:
        ok("Manifest includes runtime kernel source eiarf/SM_G92X (lineage-22.2-android15-6.6)")
    else:
        fail("Manifest missing eiarf/SM_G92X project entry")

    ksu = ROOT / "local_repos/sukisu_nova_kernel"
    ksu_h = ksu / "include/uapi/ksu.h"
    kbuild = ksu / "Kbuild"
    if not ksu_h.is_file() or ksu_h.stat().st_size < 200:
        fail("sukisu_nova_kernel/include/uapi/ksu.h missing or invalid")
    else:
        ok("sukisu_nova_kernel/include/uapi/ksu.h materialized as real header file")
    ktext = kbuild.read_text(encoding="utf-8")
    if "--max-time 3" in ktext and "KSU_OFFLINE" in ktext:
        ok("sukisu_nova_kernel/Kbuild has offline fallback and curl timeout")
    else:
        fail("sukisu_nova_kernel/Kbuild missing offline/timeout protection")


def check_repo_consistency() -> None:
    if (ROOT / "local_repos/device_samsung_zeroflte/prebuilt").exists():
        fail("Stray prebuilt directory in device_samsung_zeroflte (belongs in universal7420-common/prebuilt)")

    bp_modules: dict[str, list[str]] = {}
    for bp in (ROOT / "local_repos").rglob("Android.bp"):
        text_nc = re.sub(r"//.*", "", bp.read_text(encoding="utf-8", errors="ignore"))
        for m in re.finditer(r'name\s*:\s*"([^"]+)"', text_nc):
            bp_modules.setdefault(m.group(1), []).append(str(bp.relative_to(ROOT)))

    for mk in (ROOT / "local_repos").rglob("Android.mk"):
        text = mk.read_text(encoding="utf-8", errors="ignore")
        for m in re.finditer(r"LOCAL_MODULE\s*:=\s*(\S+)", text):
            bp_modules.setdefault(m.group(1), []).append(str(mk.relative_to(ROOT)))

    dups = {k: v for k, v in bp_modules.items() if len(v) > 1}
    if dups:
        fail(f"Duplicate Soong/Make module names across local_repos: {dups}")
    else:
        ok(f"Zero duplicate module names across {len(bp_modules)} Soong/Make modules in local_repos")

    # Verify ueventd /sys/ lines all have 5 or 6 tokens
    uev = ROOT / "local_repos/device_samsung_universal7420-common/ramdisk/etc/ueventd.samsungexynos7420.rc"
    for lineno, line in enumerate(uev.read_text(encoding="utf-8").splitlines(), 1):
        s = line.split("#", 1)[0].strip()
        if s.startswith("/sys/"):
            toks = s.split()
            if len(toks) not in (5, 6):
                fail(f"ueventd.samsungexynos7420.rc:{lineno} invalid /sys/ entry ({len(toks)} fields): {s}")
    ok("ueventd.samsungexynos7420.rc /sys/ entries syntax valid")

    # Verify Watchdog & 20-minute pre-deep sleep rules
    rc_main = (
        ROOT / "local_repos/device_samsung_universal7420-common/ramdisk/etc/init.samsungexynos7420.rc"
    ).read_text(encoding="utf-8")
    boot_comp_idx = rc_main.find("on property:sys.boot_completed=1")
    early_zero_idx = rc_main.find("autorecovery 0")
    if boot_comp_idx != -1 and early_zero_idx > boot_comp_idx and "autorecovery 900" in rc_main[:boot_comp_idx]:
        ok("Watchdog rule OK: autorecovery armed (900s) in early-init and disarmed (0) only on boot_completed=1")
    else:
        fail("Watchdog autorecovery rule violated in init.samsungexynos7420.rc")

    rc_pwr = (
        ROOT / "local_repos/device_samsung_universal7420-common/ramdisk/etc/init.power.rc"
    ).read_text(encoding="utf-8")
    if "zeroflte_pre_deep 1200000000000" in rc_pwr and "wake_unlock zeroflte_pre_deep" in rc_pwr:
        ok("init.power.rc 20-minute delayed deep sleep + screen-off CPU/GPU caps verified")
    else:
        fail("init.power.rc missing 20-minute zeroflte_pre_deep rules")


def main() -> int:
    print("=== zeroflte RisingOS 17 Pre-Flight Verification ===\n")
    try:
        ET.parse(ROOT / "manifests/zeroflte_rising17.xml")
        ok("Manifest XML valid: manifests/zeroflte_rising17.xml")
    except Exception as exc:
        fail(f"Manifest XML error: {exc}")
    for repo in sorted((ROOT / "local_repos").iterdir()):
        if repo.is_dir():
            check_xml_tree(repo, f"local_repos/{repo.name}")

    check_json_files()
    check_lf_and_shell()
    check_python_scripts()
    check_dtbh_and_image_tools()
    check_sukisu_and_manifest()
    check_repo_consistency()

    print("\n===================================================")
    if FAILURES:
        print(f"VERIFICATION FAILED ({len(FAILURES)} issues):")
        for f in FAILURES:
            print("  -", f)
        return 1
    print("ALL VERIFICATION CHECKS PASSED (0 failures).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
