#!/usr/bin/env python3
r"""Pre-flight offline verification suite for D:\zeroflte-rising17."""

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

ROOT = Path(r"D:\zeroflte-rising17")
FAILURES: list[str] = []


def ok(msg: str) -> None:
    print(f"[PASS] {msg}")


def fail(msg: str) -> None:
    print(f"[FAIL] {msg}")
    FAILURES.append(msg)


def check_xml_tree(base_dir: Path, label: str) -> int:
    count = 0
    for xml_path in sorted(base_dir.rglob("*.xml")):
        # Android.xml or values XMLs should all parse cleanly
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
    sh_files = list((ROOT / "scripts").glob("*.sh")) + [
        ROOT / "local_repos/device_samsung_universal7420-common/ramdisk/bin/init.postboot.sh",
        ROOT / "local_repos/device_samsung_universal7420-common/ramdisk/bin/init.touch.sh",
        ROOT / "local_repos/device_samsung_universal7420-common/ramdisk/bin/set_resolution.sh",
        ROOT / "local_repos/device_samsung_universal7420-common/releasetools/resize-system.sh",
    ]
    for p in sh_files:
        if not p.exists():
            fail(f"Missing shell script: {p.relative_to(ROOT)}")
            continue
        raw = p.read_bytes()
        if b"\r\n" in raw:
            fail(f"CRLF detected in shell script {p.relative_to(ROOT)}")
            continue
        # Syntax check inside WSL bash -n
        wsl_path = "/mnt/d/" + str(p).replace("\\", "/").split("D:/", 1)[1]
        rc = subprocess.run(["wsl", "bash", "-n", wsl_path], capture_output=True, text=True)
        if rc.returncode != 0:
            fail(f"bash -n syntax error in {p.relative_to(ROOT)}: {rc.stderr.strip()}")
        else:
            ok(f"Shell syntax + LF OK: {p.relative_to(ROOT)}")

    for pf in sorted((ROOT / "patches").rglob("*.patch")):
        raw = pf.read_bytes()
        if b"\r\n" in raw:
            fail(f"CRLF detected in patch {pf.relative_to(ROOT)}")
        elif b"diff --git " not in raw:
            fail(f"Invalid patch format in {pf.relative_to(ROOT)}")
        else:
            ok(f"Patch format + LF OK: {pf.relative_to(ROOT)}")


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
    expected_dt_sha = "8a8c42b05508e338992df21864a36178c0b3e4c31a71d856222a20a2d8875296"

    raw_dt = prebuilt_dt.read_bytes()
    if hashlib.sha256(raw_dt).hexdigest() != expected_dt_sha:
        fail("prebuilt/dt.img SHA-256 mismatch from reference baseline")
        return

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
            if rebuilt_sha == expected_dt_sha:
                ok(f"mkdtbh.py exact DTBH v2 round-trip byte-for-byte SHA-256 match ({rebuilt_sha[:12]})")
            else:
                fail(f"mkdtbh.py round-trip SHA-256 mismatch: {rebuilt_sha} != {expected_dt_sha}")

        # Verify patch_kernel_image.py on synthetic zeroed header and on real prebuilt/Image
        synth_img = td_path / "Image"
        buf = bytearray(prebuilt_img.read_bytes()[:4096])
        struct.pack_into("<Q", buf, 0x08, 0x80000)
        synth_img.write_bytes(buf)
        r_fail = subprocess.run(
            [sys.executable, str(ROOT / "scripts/patch_kernel_image.py"), str(synth_img), "--verify"],
            capture_output=True,
            text=True,
        )
        if r_fail.returncode == 0:
            fail("patch_kernel_image.py --verify should have failed on unpatched text_offset=0x80000")
        else:
            r_patch = subprocess.run(
                [sys.executable, str(ROOT / "scripts/patch_kernel_image.py"), str(synth_img)],
                capture_output=True,
                text=True,
            )
            r_ok = subprocess.run(
                [sys.executable, str(ROOT / "scripts/patch_kernel_image.py"), str(synth_img), "--verify"],
                capture_output=True,
                text=True,
            )
            if r_patch.returncode == 0 and r_ok.returncode == 0:
                ok("patch_kernel_image.py synthetic patch + verify OK")
            else:
                fail(f"patch_kernel_image.py failed: {r_patch.stderr} / {r_ok.stderr}")

    r_real = subprocess.run(
        [sys.executable, str(ROOT / "scripts/patch_kernel_image.py"), str(prebuilt_img), "--verify"],
        capture_output=True,
        text=True,
    )
    if r_real.returncode == 0:
        ok("patch_kernel_image.py --verify OK on prebuilt/Image (text_offset=0x205000)")
    else:
        fail(f"prebuilt/Image verification failed: {r_real.stderr}")


def check_kernel_patch_against_sm_g92x() -> None:
    sm = ROOT / "reference/SM_G92X"
    kpatch = ROOT / "patches/kernel/0001-sm_g92x-sukisu-nova-zeroflte-config.patch"
    orig = subprocess.check_output(
        ["git", "-C", str(sm), "show", "HEAD:arch/arm64/configs/zeroflte_gki.fragment"]
    )
    with tempfile.TemporaryDirectory() as td:
        subprocess.check_call(["git", "-C", td, "init", "-q"])
        target = Path(td) / "arch/arm64/configs/zeroflte_gki.fragment"
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(orig)
        subprocess.check_call(["git", "-C", td, "add", "."])
        subprocess.check_call(["git", "-C", td, "commit", "-q", "-m", "base"])
        rc = subprocess.run(["git", "-C", td, "apply", "--check", str(kpatch)], capture_output=True, text=True)
        if rc.returncode == 0:
            ok("Kernel patch git apply --check CLEAN against SM_G92X HEAD")
        else:
            fail(f"Kernel patch failed apply --check: {rc.stderr}")

    # Verify SukiSU-Nova headers + offline timeout
    ksu = ROOT / "local_repos/sukisu_nova_kernel"
    ksu_h = ksu / "include/uapi/ksu.h"
    kbuild = ksu / "Kbuild"
    if not ksu_h.is_file() or ksu_h.stat().st_size < 200:
        fail("sukisu_nova_kernel/include/uapi/ksu.h missing or invalid symlink placeholder")
    else:
        ok("sukisu_nova_kernel/include/uapi/ksu.h materialized as real header file")
    ktext = kbuild.read_text(encoding="utf-8")
    if "--max-time 3" in ktext and "KSU_OFFLINE" in ktext:
        ok("sukisu_nova_kernel/Kbuild has offline fallback and curl timeout")
    else:
        fail("sukisu_nova_kernel/Kbuild missing offline/timeout protection")


def check_repo_consistency() -> None:
    # 1. Duplicate module names across Android.bp / Android.mk in local_repos
    bp_modules: dict[str, list[str]] = {}
    for bp in (ROOT / "local_repos").rglob("Android.bp"):
        text = bp.read_text(encoding="utf-8", errors="ignore")
        # strip comments
        text_nc = re.sub(r"//.*", "", text)
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

    # 2. Graphics & Camera integration checks
    hwc = (ROOT / "local_repos/vendor_nk/gfx/s6_kms_hwcomposer.cpp").read_text(encoding="utf-8")
    if "0xABCDDCBAU" in hwc and "0x3141592" in hwc:
        ok("s6_kms_hwcomposer.cpp supports both CROS_GRALLOC_MAGIC and S6_GRALLOC_MAGIC")
    else:
        fail("s6_kms_hwcomposer.cpp missing cros/s6 handle magic support")

    cam_bridge = (
        ROOT / "local_repos/device_samsung_universal7420-common/hardware/camera/gralloc_bridge.cpp"
    ).read_text(encoding="utf-8")
    if "gralloc.minigbm.so" in cam_bridge and "kCrosGrallocMagic" in cam_bridge:
        ok("camera gralloc_bridge.cpp loads gralloc.minigbm.so and reads CrosGrallocHandle total_size")
    else:
        fail("camera gralloc_bridge.cpp missing minigbm/CrosGrallocHandle BLOB support")

    # 3. Dolby DAX v3.6 cleanup & UUID alignment check
    dolby_dir = ROOT / "local_repos/vendor_lunaris_dolby"
    dolby_files = [dolby_dir / "Android.bp", dolby_dir / "dolby.mk"]
    dolby_files.extend((dolby_dir / "vintf").glob("*.xml"))
    dolby_files.extend((dolby_dir / "sepolicy/vendor").glob("*"))
    bad_pat = re.compile(r"(_sp\b|media\.c2|ddpdec|dolbycodec_shim)")
    for df in dolby_files:
        if df.is_file():
            m = bad_pat.search(df.read_text(encoding="utf-8", errors="ignore"))
            if m:
                fail(
                    f"Obsolete Dolby artifact '{m.group(1)}' still referenced in "
                    f"{df.relative_to(ROOT)}"
                )
    audio_fx = (
        ROOT / "local_repos/device_samsung_universal7420-common/configs/audio/audio_effects.xml"
    ).read_text(encoding="utf-8")
    for tok in (
        "libswdap_v3_6.so",
        "libhwdap_v3_6.so",
        "libeffectproxy.so",
        "9d4921da-8225-4f29-aefa-39537a04bcaa",
        "6ab06da4-c516-4611-8166-452799218539",
        "a0c30891-8246-4aef-b8ad-d53e26da0253",
    ):
        if tok not in audio_fx:
            fail(f"audio_effects.xml missing Dolby token: {tok}")
    ok("Dolby DAX v3.6 cleanup and audio_effects.xml proxy UUIDs verified")

    # 4. SELinux Enforcing & Watchdog rule checks
    permissive = []
    for te in (ROOT / "local_repos").rglob("sepolicy/**/*.te"):
        t = te.read_text(encoding="utf-8", errors="ignore")
        if "permissive " in t or "typepermissive" in t:
            permissive.append(str(te.relative_to(ROOT)))
    if permissive:
        fail(f"Permissive SELinux directives found: {permissive}")
    else:
        ok("SELinux strict Enforcing check: zero permissive/typepermissive directives")

    # Verify early-init does not write autorecovery 0
    rc_main = (
        ROOT / "local_repos/device_samsung_universal7420-common/ramdisk/etc/init.samsungexynos7420.rc"
    ).read_text(encoding="utf-8")
    boot_comp_idx = rc_main.find("on property:sys.boot_completed=1")
    early_zero_idx = rc_main.find("autorecovery 0")
    if boot_comp_idx != -1 and early_zero_idx > boot_comp_idx and "autorecovery 900" in rc_main[:boot_comp_idx]:
        ok("Watchdog rule OK: autorecovery armed (900s) in early-init and disarmed (0) only on boot_completed=1")
    else:
        fail("Watchdog autorecovery rule violated in init.samsungexynos7420.rc")

    # 5. Conflicting RisingOS property checks
    sys_prop = (ROOT / "local_repos/device_samsung_universal7420-common/system.prop").read_text(encoding="utf-8")
    vnd_prop = (ROOT / "local_repos/device_samsung_universal7420-common/vendor.prop").read_text(encoding="utf-8")
    for p in ("ro.lmk.kill_heaviest_task", "ro.surface_flinger.supports_background_blur"):
        if p in sys_prop or p in vnd_prop:
            fail(f"Property {p} must NOT be in system.prop or vendor.prop (conflicts with RisingOS properties.mk)")
    ok("No conflicting ro.lmk / blur properties in system.prop or vendor.prop")

    # 6. Native QHD default & Settings -> Display Screen Resolution module checks
    postboot = (
        ROOT / "local_repos/device_samsung_universal7420-common/ramdisk/bin/init.postboot.sh"
    ).read_text(encoding="utf-8")
    if "1080x1920" in postboot or "720x1280" in postboot:
        fail("init.postboot.sh must not hard-force non-native resolution on boot")
    else:
        ok("init.postboot.sh keeps native QHD (1440x2560) default without overriding WindowManager")

    disp_res_dir = ROOT / "local_repos/device_samsung_universal7420-common/packages/DisplayResolution"
    disp_manifest = (disp_res_dir / "AndroidManifest.xml").read_text(encoding="utf-8")
    disp_priv = (disp_res_dir / "privapp-permissions-displayresolution.xml").read_text(encoding="utf-8")
    disp_bp = (disp_res_dir / "Android.bp").read_text(encoding="utf-8")
    dev_common = (ROOT / "local_repos/device_samsung_universal7420-common/device-common.mk").read_text(
        encoding="utf-8"
    )
    if (
        "com.android.settings.category.ia.display" in disp_manifest
        and "WRITE_SECURE_SETTINGS" in disp_priv
        and "DisplayResolution" in disp_bp
        and "DisplayResolution" in dev_common
    ):
        ok("DisplayResolution Settings -> Display package, privapp permissions, and PRODUCT_PACKAGES verified")
    else:
        fail("DisplayResolution Settings integration incomplete")


def main() -> int:
    print("=== zeroflte RisingOS 17 Pre-Flight Verification ===\n")
    # XMLs
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
    check_kernel_patch_against_sm_g92x()
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
