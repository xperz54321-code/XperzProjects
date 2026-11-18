#!/usr/bin/env python3
"""Validate the Ubuntu-based Xperz live ISO remaster configuration."""

from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
PACKAGE_LIST = ROOT / "desktop/packages.txt"
BUILD_SCRIPT = ROOT / "desktop/build.sh"
WALLPAPER = ROOT / "desktop/branding/xperz-wallpaper.svg"
LOGO = ROOT / "desktop/branding/xperz-mark.svg"
DESKTOP_DEFAULTS = ROOT / "desktop/branding/01-xperz"
GDM_DEFAULTS = ROOT / "desktop/branding/01-xperz-gdm"
WORKFLOW = ROOT / ".github/workflows/build-xperz-live.yml"


def main():
    packages = [
        line.strip()
        for line in PACKAGE_LIST.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    invalid = [
        package
        for package in packages
        if not re.fullmatch(r"[a-z0-9][a-z0-9+.-]*", package)
    ]
    if invalid:
        raise ValueError(f"Invalid Ubuntu package names: {invalid}")
    if len(packages) != len(set(packages)):
        raise ValueError("Duplicate Ubuntu package names.")

    required = {
        "firefox",
        "libreoffice",
        "vlc",
        "code",
        "gnome-control-center",
        "gnome-terminal",
        "linux-firmware",
        "ubuntu-drivers-common",
    }
    missing = sorted(required.difference(packages))
    if missing:
        raise ValueError(f"Desktop package list is missing required apps/drivers: {missing}")

    build_script = BUILD_SCRIPT.read_text(encoding="utf-8")
    for marker in (
        "ubuntu-26.04.1-desktop-amd64.iso",
        "601e30fbf5d97759367c632e2c33630665039b7e2158fd068403da3ccf1bda1f",
        "-boot_image any replay",
        "-report_el_torito plain",
        "OUTPUT_ISO",
    ):
        if marker not in build_script:
            raise ValueError(f"Ubuntu remaster script is missing required behavior: {marker}")

    squashfs_staged = build_script.index(
        'mv -f "$NEW_SQUASHFS" "$ISO_TREE/casper/minimal.standard.live.xperz.squashfs"'
    )
    checksums_generated = build_script.index('find . -type f ! -name md5sum.txt')
    if squashfs_staged > checksums_generated:
        raise ValueError("The ISO checksum manifest must be generated after staging the new filesystem.")
    resolver_snapshot = build_script.index('cp -a "$ROOTFS/etc/resolv.conf" "$BUILD_DIR/resolv.conf.original"')
    resolver_restore = build_script.rindex('cp -a "$BUILD_DIR/resolv.conf.original" "$ROOTFS/etc/resolv.conf"')
    mount_cleanup = build_script.index("\ncleanup_mounts\ntrap - EXIT HUP INT TERM")
    if not resolver_snapshot < resolver_restore < mount_cleanup:
        raise ValueError("The build must restore the live filesystem resolver configuration.")
    for marker in (
        "minimal.squashfs",
        "minimal.standard.squashfs",
        "minimal.standard.live.squashfs",
        "mount -t overlay overlay",
        "layerfs-path=minimal.standard.live.xperz.squashfs",
        'mksquashfs "$UPPER"',
        'rm -f "$source_file"',
        "URIs: http://archive.ubuntu.com/ubuntu/",
        "Suites: resolute resolute-updates resolute-backports",
    ):
        if marker not in build_script:
            raise ValueError(f"Layered Ubuntu remaster is missing required behavior: {marker}")

    workflow = WORKFLOW.read_text(encoding="utf-8")
    for marker in (
        "workflow_dispatch:",
        "desktop/build.sh",
        "601e30fbf5d97759367c632e2c33630665039b7e2158fd068403da3ccf1bda1f",
        "qemu-system-x86_64",
        "OVMF_CODE_4M.fd",
        "actions/upload-artifact@v4",
    ):
        if marker not in workflow:
            raise ValueError(f"Hosted build workflow is missing required behavior: {marker}")
    if "mount -t overlay overlay" not in build_script:
        raise ValueError("The live remaster must support Ubuntu's layered Casper filesystem.")

    ET.parse(WALLPAPER)
    ET.parse(LOGO)
    wallpaper_entry = "picture-uri='file:///usr/share/backgrounds/xperz/xperz-wallpaper.svg'"
    for path in (DESKTOP_DEFAULTS, GDM_DEFAULTS):
        if wallpaper_entry not in path.read_text(encoding="utf-8"):
            raise ValueError(f"Xperz wallpaper is not configured in {path.name}.")
    print(
        f"Ubuntu live remaster config OK: {len(packages)} packages, "
        "checksum regeneration, BIOS/UEFI smoke tests, and Xperz artwork."
    )


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, ET.ParseError) as error:
        print(f"Ubuntu remaster config check failed: {error}", file=sys.stderr)
        sys.exit(1)
