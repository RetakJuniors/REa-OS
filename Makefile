CC ?= gcc
LD ?= ld
LIMINE_VERSION ?= v8.7.0
BUILD_DIR := build
LIMINE_DIR := limine
KERNEL := $(BUILD_DIR)/kernel.elf
ISO := rea-os.iso

CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Werror \
	-ffreestanding -fno-stack-protector -fno-pic -fno-pie \
	-m64 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
	-mcmodel=kernel -Iinclude
LDFLAGS := -nostdlib -static -no-pie -Wl,--build-id=none \
	-Wl,-T,linker.ld -Wl,-z,max-page-size=0x1000
SOURCES := src/boot.c src/console.c src/font.c src/kernel.c \
	src/keyboard.c src/keyboard_layout.c src/serial.c src/shell.c src/util.c
OBJECTS := $(SOURCES:src/%.c=$(BUILD_DIR)/%.o)

.PHONY: all clean limine iso test

all: $(ISO)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(OBJECTS) linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS) -lgcc

limine: $(LIMINE_DIR)/limine

$(LIMINE_DIR)/limine:
	git clone --depth=1 --branch=$(LIMINE_VERSION)-binary \
		https://github.com/limine-bootloader/limine.git $(LIMINE_DIR)
	$(MAKE) -C $(LIMINE_DIR)

$(ISO): $(KERNEL) $(LIMINE_DIR)/limine limine.conf
	mkdir -p $(BUILD_DIR)/iso_root/boot
	mkdir -p $(BUILD_DIR)/iso_root/EFI/BOOT
	cp $(KERNEL) $(BUILD_DIR)/iso_root/boot/kernel.elf
	cp limine.conf $(BUILD_DIR)/iso_root/limine.conf
	cp $(LIMINE_DIR)/limine-bios.sys $(BUILD_DIR)/iso_root/limine-bios.sys
	cp $(LIMINE_DIR)/limine-bios-cd.bin $(BUILD_DIR)/iso_root/limine-bios-cd.bin
	cp $(LIMINE_DIR)/limine-uefi-cd.bin $(BUILD_DIR)/iso_root/limine-uefi-cd.bin
	cp $(LIMINE_DIR)/BOOTX64.EFI $(BUILD_DIR)/iso_root/EFI/BOOT/BOOTX64.EFI
	xorriso -as mkisofs -b limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot limine-uefi-cd.bin -efi-boot-part \
		--efi-boot-image --protective-msdos-label \
		$(BUILD_DIR)/iso_root -o $(ISO)
	$(LIMINE_DIR)/limine bios-install $(ISO)

iso: $(ISO)

test: | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude \
		tests/shell_test.c src/shell.c -o $(BUILD_DIR)/shell_test
	$(BUILD_DIR)/shell_test
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude \
		tests/font_test.c src/font.c -o $(BUILD_DIR)/font_test
	$(BUILD_DIR)/font_test

clean:
	rm -rf $(BUILD_DIR) $(ISO)
