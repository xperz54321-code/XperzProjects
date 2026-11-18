#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
UBUNTU_ISO=${UBUNTU_ISO:-/mnt/c/Users/Admin/Downloads/ubuntu-26.04.1-desktop-amd64.iso}
OUTPUT_ISO=${OUTPUT_ISO:-/mnt/c/Users/Admin/Downloads/Xperz.iso}
WORK_DIR=${WORK_DIR:-"$ROOT/build/ubuntu-remaster"}
ISO_SHA256=601e30fbf5d97759367c632e2c33630665039b7e2158fd068403da3ccf1bda1f
MICROSOFT_KEY_FINGERPRINT=BC528686B50D79E339D3721CEB3E94ADBE1229CF

if [ "$(id -u)" -ne 0 ]; then
    printf '%s\n' "Run this build as root (for example: sudo make desktop-iso)." >&2
    exit 1
fi

for tool in xorriso unsquashfs mksquashfs mount umount mountpoint chroot \
    apt-get curl gpg awk sed install sha256sum df mktemp md5sum find sort \
    dpkg-query du nproc realpath grep cp mv rm; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'Required Ubuntu remaster tool not found: %s\n' "$tool" >&2
        exit 1
    fi
done

if [ ! -f "$UBUNTU_ISO" ]; then
    printf 'Ubuntu source ISO not found: %s\nSet UBUNTU_ISO to its Linux/WSL path.\n' "$UBUNTU_ISO" >&2
    exit 1
fi

if [ "$(realpath -m "$UBUNTU_ISO")" = "$(realpath -m "$OUTPUT_ISO")" ]; then
    printf '%s\n' "The output path must not overwrite the Ubuntu source ISO." >&2
    exit 1
fi

SOURCE_SHA256=$(sha256sum "$UBUNTU_ISO" | awk '{ print $1 }')
if [ "$SOURCE_SHA256" != "$ISO_SHA256" ]; then
    printf 'Ubuntu ISO checksum mismatch: %s\nExpected Ubuntu 26.04.1 amd64 Desktop (%s).\n' \
        "$SOURCE_SHA256" "$ISO_SHA256" >&2
    exit 1
fi

mkdir -p "$WORK_DIR" "$(dirname "$OUTPUT_ISO")"
WORK_FREE_KIB=$(df -Pk "$WORK_DIR" | awk 'END { print $4 }')
OUTPUT_FREE_KIB=$(df -Pk "$(dirname "$OUTPUT_ISO")" | awk 'END { print $4 }')
if [ "$WORK_FREE_KIB" -lt 41943040 ]; then
    printf '%s\n' "The remaster work drive needs at least 40 GiB free." >&2
    exit 1
fi
if [ "$OUTPUT_FREE_KIB" -lt 8388608 ]; then
    printf '%s\n' "The output drive needs at least 8 GiB free." >&2
    exit 1
fi

BUILD_DIR=$(mktemp -d "$WORK_DIR/xperz.XXXXXXXX")
ISO_TREE="$BUILD_DIR/iso"
ROOTFS="$BUILD_DIR/rootfs"
NEW_SQUASHFS="$BUILD_DIR/filesystem.squashfs"
TEMP_ISO="$BUILD_DIR/Xperz.iso"
MICROSOFT_KEY="$BUILD_DIR/microsoft.asc"
MD5SUMS="$BUILD_DIR/md5sum.txt"
MANIFEST="$BUILD_DIR/filesystem.manifest"
FILESYSTEM_SIZE="$BUILD_DIR/filesystem.size"

cleanup_mounts() {
    for path in "$ROOTFS/run" "$ROOTFS/sys" "$ROOTFS/dev"; do
        if [ -d "$path" ] && mountpoint -q "$path"; then
            umount -R "$path" || umount -lf "$path" || true
        fi
    done
}
trap cleanup_mounts EXIT HUP INT TERM

xorriso -osirrox on -indev "$UBUNTU_ISO" -extract / "$ISO_TREE" \
    >"$BUILD_DIR/extract.log" 2>&1
SQUASHFS="$ISO_TREE/casper/filesystem.squashfs"
if [ ! -s "$SQUASHFS" ]; then
    printf '%s\n' "The source ISO has no casper/filesystem.squashfs live system." >&2
    exit 1
fi
unsquashfs -d "$ROOTFS" "$SQUASHFS" >"$BUILD_DIR/unsquashfs.log" 2>&1

SOURCES="$ROOTFS/etc/apt/sources.list.d/ubuntu.sources"
if [ ! -f "$SOURCES" ]; then
    printf '%s\n' "Ubuntu apt sources file was not found in the live filesystem." >&2
    exit 1
fi
for component in universe multiverse; do
    if ! grep -E '^Components:' "$SOURCES" | grep -qw "$component"; then
        sed -i "/^Components:/ s/\$/ $component/" "$SOURCES"
    fi
done

curl --fail --location --silent --show-error \
    https://packages.microsoft.com/keys/microsoft.asc --output "$MICROSOFT_KEY"
KEY_FINGERPRINT=$(gpg --batch --show-keys --with-colons "$MICROSOFT_KEY" |
    awk -F: '$1 == "fpr" { print $10; exit }')
if [ "$KEY_FINGERPRINT" != "$MICROSOFT_KEY_FINGERPRINT" ]; then
    printf 'Unexpected Microsoft repository key fingerprint: %s\n' "$KEY_FINGERPRINT" >&2
    exit 1
fi
mkdir -p "$ROOTFS/usr/share/keyrings"
gpg --batch --yes --dearmor --output "$ROOTFS/usr/share/keyrings/microsoft.gpg" "$MICROSOFT_KEY"
printf '%s\n' \
    'deb [arch=amd64 signed-by=/usr/share/keyrings/microsoft.gpg] https://packages.microsoft.com/repos/code stable main' \
    >"$ROOTFS/etc/apt/sources.list.d/vscode.list"

install -D -m 0644 "$ROOT/desktop/branding/xperz-wallpaper.svg" \
    "$ROOTFS/usr/share/backgrounds/xperz/xperz-wallpaper.svg"
install -D -m 0644 "$ROOT/desktop/branding/xperz-mark.svg" \
    "$ROOTFS/usr/share/pixmaps/xperz-mark.svg"
install -D -m 0644 "$ROOT/desktop/branding/xperz-mark.svg" \
    "$ROOTFS/usr/share/icons/hicolor/scalable/apps/xperz.svg"
install -D -m 0644 "$ROOT/desktop/branding/01-xperz" \
    "$ROOTFS/etc/dconf/db/local.d/01-xperz"
install -D -m 0644 "$ROOT/desktop/branding/gdm-profile" "$ROOTFS/etc/dconf/profile/gdm"
install -D -m 0644 "$ROOT/desktop/branding/01-xperz-gdm" \
    "$ROOTFS/etc/dconf/db/gdm.d/01-xperz"

if [ -f "$ROOTFS/usr/lib/os-release" ]; then
    sed -i 's/^NAME=.*/NAME="Xperz OS"/' "$ROOTFS/usr/lib/os-release"
    sed -i 's/^PRETTY_NAME=.*/PRETTY_NAME="Xperz OS 26.04.1 LTS (based on Ubuntu)"/' \
        "$ROOTFS/usr/lib/os-release"
fi
printf '%s\n' "Xperz OS 26.04.1 LTS (based on Ubuntu) \\n \\l" >"$ROOTFS/etc/issue"
printf '%s\n' "Xperz OS 26.04.1 LTS (based on Ubuntu)" >"$ROOTFS/etc/issue.net"
printf '%s\n' "Xperz OS 26.04.1 LTS amd64" >"$ISO_TREE/.disk/info"

if [ -f "$ISO_TREE/boot/grub/grub.cfg" ]; then
    sed -i 's/Ubuntu/Xperz OS/g' "$ISO_TREE/boot/grub/grub.cfg"
fi
rm -f "$ISO_TREE/casper/filesystem.squashfs.gpg"

POLICY_RC_CREATED=0
if [ ! -e "$ROOTFS/usr/sbin/policy-rc.d" ]; then
    printf '%s\n' '#!/bin/sh' 'exit 101' >"$ROOTFS/usr/sbin/policy-rc.d"
    chmod 0755 "$ROOTFS/usr/sbin/policy-rc.d"
    POLICY_RC_CREATED=1
fi

mount --rbind /dev "$ROOTFS/dev"
mount --make-rslave "$ROOTFS/dev"
mount --rbind /sys "$ROOTFS/sys"
mount --make-rslave "$ROOTFS/sys"
mount --rbind /run "$ROOTFS/run"
mount --make-rslave "$ROOTFS/run"
cp -L /etc/resolv.conf "$ROOTFS/etc/resolv.conf"

chroot "$ROOTFS" apt-get update
set --
while IFS= read -r package; do
    case "$package" in
        ''|'#'*) continue ;;
        *[!a-z0-9+.-]*)
            printf 'Invalid package name in desktop/packages.txt: %s\n' "$package" >&2
            exit 1
            ;;
        *) set -- "$@" "$package" ;;
    esac
done <"$ROOT/desktop/packages.txt"
chroot "$ROOTFS" env DEBIAN_FRONTEND=noninteractive apt-get install -y "$@"
chroot "$ROOTFS" dconf update
chroot "$ROOTFS" dpkg-query -W --showformat='${binary:Package} ${Version}\n' >"$MANIFEST"
chroot "$ROOTFS" apt-get clean
if [ "$POLICY_RC_CREATED" -eq 1 ]; then
    rm -f "$ROOTFS/usr/sbin/policy-rc.d"
fi
cleanup_mounts
trap - EXIT HUP INT TERM

sort -o "$MANIFEST" "$MANIFEST"
du -sx --block-size=1 "$ROOTFS" | awk '{ print $1 }' >"$FILESYSTEM_SIZE"
mksquashfs "$ROOTFS" "$NEW_SQUASHFS" -comp xz -b 1M -noappend -processors "$(nproc)"
mv -f "$NEW_SQUASHFS" "$SQUASHFS"
cp "$MANIFEST" "$ISO_TREE/casper/filesystem.manifest"
cp "$FILESYSTEM_SIZE" "$ISO_TREE/casper/filesystem.size"
(
    cd "$ISO_TREE"
    find . -type f ! -name md5sum.txt -print0 | sort -z | xargs -0 md5sum >"$MD5SUMS"
)

set -- \
    -indev "$UBUNTU_ISO" \
    -outdev "$TEMP_ISO" \
    -overwrite on \
    -map "$SQUASHFS" /casper/filesystem.squashfs \
    -map "$ISO_TREE/casper/filesystem.manifest" /casper/filesystem.manifest \
    -map "$ISO_TREE/casper/filesystem.size" /casper/filesystem.size \
    -map "$MD5SUMS" /md5sum.txt \
    -map "$ISO_TREE/.disk/info" /.disk/info
if [ -f "$ISO_TREE/boot/grub/grub.cfg" ]; then
    set -- "$@" -map "$ISO_TREE/boot/grub/grub.cfg" /boot/grub/grub.cfg
fi
if [ -f "$ISO_TREE/casper/filesystem.squashfs.gpg" ]; then
    set -- "$@" -rm /casper/filesystem.squashfs.gpg
fi
set -- "$@" -volid XPERZ_26_04_1 -boot_image any replay -commit
xorriso "$@" >"$BUILD_DIR/repack.log" 2>&1
xorriso -indev "$TEMP_ISO" -report_el_torito plain >"$BUILD_DIR/boot-report.log" 2>&1
if ! grep -q BIOS "$BUILD_DIR/boot-report.log" ||
   ! grep -q UEFI "$BUILD_DIR/boot-report.log"; then
    printf '%s\n' "The remastered ISO is missing its BIOS or UEFI boot entry." >&2
    exit 1
fi

OUTPUT_TEMP="${OUTPUT_ISO}.partial"
TEMP_DEVICE=$(df -P "$TEMP_ISO" | awk 'END { print $1 }')
OUTPUT_DEVICE=$(df -P "$(dirname "$OUTPUT_ISO")" | awk 'END { print $1 }')
if [ "$TEMP_DEVICE" = "$OUTPUT_DEVICE" ]; then
    mv -f "$TEMP_ISO" "$OUTPUT_ISO"
else
    cp "$TEMP_ISO" "$OUTPUT_TEMP"
    mv -f "$OUTPUT_TEMP" "$OUTPUT_ISO"
fi
rm -rf "$BUILD_DIR"
printf 'Built and verified %s\n' "$OUTPUT_ISO"
