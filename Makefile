CC ?= gcc
LD ?= ld
LIMINE_VERSION ?= v8.7.0
KEYBOARD_LAYOUT ?= tr
LIMINE_DIR := limine

ifeq ($(KEYBOARD_LAYOUT),tr)
LAYOUT_DEFINE := -DREA_KEYBOARD_LAYOUT_TR
ISO := rea-os.iso
else ifeq ($(KEYBOARD_LAYOUT),us)
LAYOUT_DEFINE := -DREA_KEYBOARD_LAYOUT_US
ISO := rea-os-us.iso
else
$(error KEYBOARD_LAYOUT must be tr or us)
endif

BUILD_DIR := build/$(KEYBOARD_LAYOUT)
KERNEL := $(BUILD_DIR)/kernel.elf

CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Werror \
	-ffreestanding -fno-stack-protector -fno-pic -fno-pie \
	-m64 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
	-mcmodel=kernel -Iinclude $(LAYOUT_DEFINE)
LDFLAGS := -nostdlib -static -no-pie -Wl,--build-id=none \
	-Wl,-T,linker.ld -Wl,-z,max-page-size=0x1000
SOURCES := src/boot.c src/console.c src/font.c src/kernel.c \
	src/keyboard.c src/keyboard_layout.c src/memory.c src/serial.c \
	src/shell.c src/stat.c src/timer.c src/util.c
OBJECTS := $(SOURCES:src/%.c=$(BUILD_DIR)/%.o)
ASM_SOURCES := src/interrupts.S
ASM_OBJECTS := $(ASM_SOURCES:src/%.S=$(BUILD_DIR)/%.o)

.PHONY: all clean limine iso test

all: $(ISO)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: src/%.S | $(BUILD_DIR)
	$(CC) -m64 -c $< -o $@

$(KERNEL): $(OBJECTS) $(ASM_OBJECTS) linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS) $(ASM_OBJECTS) -lgcc

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
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc \
		tests/shell_test.c src/shell.c -o $(BUILD_DIR)/shell_test
	$(BUILD_DIR)/shell_test
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc \
		tests/font_test.c src/font.c -o $(BUILD_DIR)/font_test
	$(BUILD_DIR)/font_test
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc \
		tests/console_test.c src/console.c src/font.c src/util.c \
		-o $(BUILD_DIR)/console_test
	$(BUILD_DIR)/console_test
	$(CC) -std=c11 -Wall -Wextra -Werror -DREA_KEYBOARD_LAYOUT_TR -Iinclude \
		tests/keyboard_layout_test.c src/keyboard_layout.c \
		-o $(BUILD_DIR)/keyboard_layout_tr_test
	$(BUILD_DIR)/keyboard_layout_tr_test
	$(CC) -std=c11 -Wall -Wextra -Werror -DREA_KEYBOARD_LAYOUT_US -Iinclude \
		tests/keyboard_layout_test.c src/keyboard_layout.c \
		-o $(BUILD_DIR)/keyboard_layout_us_test
	$(BUILD_DIR)/keyboard_layout_us_test
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude \
		tests/stat_test.c src/stat.c -o $(BUILD_DIR)/stat_test
	$(BUILD_DIR)/stat_test
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude \
		tests/memory_test.c src/memory.c -o $(BUILD_DIR)/memory_test
	$(BUILD_DIR)/memory_test

clean:
	rm -rf build rea-os.iso rea-os-us.iso
