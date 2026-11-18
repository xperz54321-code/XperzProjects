# Xperz OS

Xperz OS is a rebranded Ubuntu live USB image. It keeps Ubuntu's desktop,
kernel, hardware support, and boot system, while adding Xperz branding and
desktop applications. A separate tiny kernel prototype is kept for development;
it is not the full OS.

## Ubuntu live desktop

The source ISO in this workspace's Windows Downloads folder is
`ubuntu-26.04.1-desktop-amd64.iso`. Its SHA-256 matches Ubuntu's published
checksum for Ubuntu 26.04.1 Desktop amd64:

`601e30fbf5d97759367c632e2c33630665039b7e2158fd068403da3ccf1bda1f`

The remaster script preserves the Ubuntu live desktop and boot files, adds
LibreOffice, VLC, Firefox, Microsoft Visual Studio Code, common developer tools,
and Xperz GNOME wallpaper, login background, system labels, and boot-menu
branding. Ubuntu already supplies the graphical Settings app, terminal,
hardware drivers, networking, audio, Bluetooth, and broad firmware support.
The remaster keeps Ubuntu's original BIOS/UEFI boot components and Secure Boot
files; it does not promise support for every PC or peripheral.

To build on Ubuntu/Linux, install the ISO tools:

```sh
sudo apt update
sudo apt install make xorriso squashfs-tools curl gnupg
```

Set the source, output, and scratch paths and build:

```sh
sudo UBUNTU_ISO=/mnt/c/Users/Admin/Downloads/ubuntu-26.04.1-desktop-amd64.iso \
  OUTPUT_ISO=/mnt/c/Users/Admin/Downloads/Xperz.iso \
  WORK_DIR=/mnt/e/xperz-remaster \
  make desktop-iso
```

Change those paths for your Linux machine. The script refuses to overwrite the
Ubuntu source ISO, checks its official SHA-256, verifies Microsoft's VS Code
repository signing key, and checks that the remastered ISO still has BIOS and
UEFI boot entries before copying it to the output path. Allow at least 40 GiB
free on the scratch drive and 8 GiB on the output drive. Package repositories
change over time, so separate builds are not guaranteed to be byte-identical.

If Linux is unavailable locally, `.github/workflows/build-xperz-live.yml` can
build the image on a GitHub-hosted Linux runner. Pushing changes to the
workflow or remaster script starts a build; once the workflow is on the
default branch, it can also be started from the repository's Actions tab. It
downloads and verifies the Ubuntu source without changing the local original,
remasters it, checks BIOS and UEFI boot entries, and smoke-tests both firmware
paths in QEMU/OVMF. Download the `Xperz-26.04.1-live-amd64` artifact, which is
retained for seven days. The remaster has not been boot-tested on every PC;
QEMU checks only the emulated BIOS and UEFI paths.

The existing `C:\Users\Admin\Downloads\Xperz.iso` from the earlier prototype
is not a full desktop image. Do not write it to USB expecting the graphical
system described above.

## Kernel prototype

The separate `build/xperz-os.iso` is a small freestanding x86_64 Limine kernel.
It draws an Xperz status screen and offers `HELP`, `ABOUT`, `MEM`, `CLEAR`, and
`HALT` commands over a COM1 serial terminal. It is not the graphical OS and has
no USB drivers, filesystem, networking, or desktop.

Build and test it on Linux with clang, lld, xorriso, QEMU, and (for UEFI tests)
OVMF:

```sh
make
make test
```

The kernel ISO previously passed QEMU BIOS and UEFI raw-disk boots. A repeat
UEFI run on this Windows host did not reach the shell, so UEFI boot for this
prototype needs a further reproducible check.

## Write the desktop ISO to a USB drive

Once `Xperz.iso` has been built, use a raw-image writer such as balenaEtcher or
Rufus in **DD image mode**. Writing the image erases the entire selected USB
drive. Verify the drive model and capacity, disconnect other removable drives,
and never select a drive containing data you need.

On Linux, identify the whole device first:

```sh
lsblk -o NAME,SIZE,MODEL,TRAN
```

Then replace `/dev/sdX` with the verified whole-device path, not a partition
such as `/dev/sdX1`:

```sh
sudo dd if=/mnt/c/Users/Admin/Downloads/Xperz.iso of=/dev/sdX \
  bs=4M status=progress conv=fsync
sync
```

This command irreversibly overwrites its output device. Verify the device path
again before running it, and never write to a system disk. Firmware settings,
USB controllers, and hardware vary; successful boot on one PC does not
guarantee compatibility on another.
