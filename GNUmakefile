.SUFFIXES:
.DELETE_ON_ERROR:

CLANG ?= clang
MAKE ?= make
PYTHON ?= python3
QEMU ?= qemu-system-x86_64
XORRISO ?= xorriso
OVMF_CODE ?= /usr/share/OVMF/OVMF_CODE_4M.fd
OVMF_VARS ?= /usr/share/OVMF/OVMF_VARS_4M.fd

BUILD := build
ISO := $(BUILD)/xperz-os.iso
KERNEL := $(BUILD)/xperz.elf
BIOS_PARTITION := $(BUILD)/bios-boot.img
BIOS_PARTITION_GUID := 21686148-6449-6e6f-744e-656564454649
LIMINE_VERSION := 12.10.0
LIMINE_DIR := $(BUILD)/limine
LIMINE_ARCHIVE := $(BUILD)/limine-binary.tar.gz
LIMINE_SHA256 := 365ba8785d4e4b55c3368738a642681b31f606afec9f77179e07fc26ebe841e2
LIMINE_STAMP := $(LIMINE_DIR)/.ready

KERNEL_CFLAGS := --target=x86_64-unknown-none-elf -std=c11 -O2 -Wall -Wextra -Werror \
	-ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
	-fno-asynchronous-unwind-tables -fno-unwind-tables -mno-red-zone -mno-mmx -mno-sse \
	-mcmodel=kernel
KERNEL_LDFLAGS := -fuse-ld=lld -nostdlib -static -Wl,-T,kernel/linker.ld \
	-Wl,-z,max-page-size=0x1000 -Wl,-z,noexecstack -Wl,--build-id=none

.PHONY: all kernel test desktop-iso test-desktop-config qemu qemu-uefi clean

all: $(ISO)

kernel: $(KERNEL)

$(KERNEL): kernel/main.c kernel/limine.h kernel/linker.ld
	mkdir -p $(BUILD)
	$(CLANG) $(KERNEL_CFLAGS) kernel/main.c $(KERNEL_LDFLAGS) -o $@

$(BIOS_PARTITION):
	mkdir -p $(BUILD)
	dd if=/dev/zero of=$@ bs=1M count=1 status=none

$(LIMINE_STAMP):
	mkdir -p $(LIMINE_DIR)
	curl --fail --location --silent --show-error \
		https://github.com/Limine-Bootloader/Limine/releases/download/v$(LIMINE_VERSION)/limine-binary.tar.gz \
		--output $(LIMINE_ARCHIVE)
	echo "$(LIMINE_SHA256)  $(LIMINE_ARCHIVE)" | sha256sum --check
	tar --extract --gzip --file $(LIMINE_ARCHIVE) --directory $(LIMINE_DIR) --strip-components=1
	$(MAKE) -C $(LIMINE_DIR)
	test -x $(LIMINE_DIR)/limine
	touch $@

$(ISO): $(KERNEL) $(LIMINE_STAMP) $(BIOS_PARTITION) limine.conf
	rm -rf $(BUILD)/iso
	mkdir -p $(BUILD)/iso/boot/limine $(BUILD)/iso/EFI/BOOT
	cp $(KERNEL) $(BUILD)/iso/boot/xperz.elf
	cp limine.conf $(BUILD)/iso/boot/limine/limine.conf
	cp $(LIMINE_DIR)/limine-bios.sys $(BUILD)/iso/boot/limine/
	cp $(LIMINE_DIR)/limine-bios-cd.bin $(BUILD)/iso/boot/limine/
	cp $(LIMINE_DIR)/limine-uefi-cd.bin $(BUILD)/iso/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI $(BUILD)/iso/EFI/BOOT/
	$(XORRISO) -as mkisofs -R -r -J \
		-b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		-append_partition 4 $(BIOS_PARTITION_GUID) $(BIOS_PARTITION) \
		$(BUILD)/iso -o $@ -- -boot_image any appended_part_as=gpt
	$(LIMINE_DIR)/limine bios-install --no-gpt-to-mbr-isohybrid-conversion $@

test: $(ISO)
	$(PYTHON) scripts/test_boot.py --iso $(ISO) --qemu $(QEMU) $(if $(REQUIRE_UEFI),--require-uefi)

desktop-iso:
	./desktop/build.sh

test-desktop-config:
	$(PYTHON) scripts/test_desktop_config.py

qemu: $(ISO)
	$(QEMU) -machine q35 -m 512M -cdrom $(ISO) -boot d -serial stdio -monitor none

qemu-uefi: $(ISO)
	@test -n "$(OVMF_CODE)" -a -n "$(OVMF_VARS)" || \
		(echo "Set OVMF_CODE and OVMF_VARS to your OVMF firmware files." >&2; exit 1)
	cp "$(OVMF_VARS)" $(BUILD)/OVMF_VARS.fd
	$(QEMU) -machine q35 -m 512M -cdrom $(ISO) -serial stdio -monitor none \
		-drive if=pflash,format=raw,unit=0,file="$(OVMF_CODE)",readonly=on \
		-drive if=pflash,format=raw,unit=1,file=$(BUILD)/OVMF_VARS.fd

clean:
	rm -rf $(BUILD)
