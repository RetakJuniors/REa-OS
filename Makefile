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
CURSOR_DIR ?= assets/cursors
ifeq ($(wildcard $(CURSOR_DIR)),)
ifneq ($(wildcard assets/cursor),)
CURSOR_DIR := assets/cursor
endif
endif

CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Werror \
	-ffreestanding -fno-stack-protector -fno-pic -fno-pie \
	-m64 -mno-red-zone -mgeneral-regs-only -mno-mmx -mno-sse -mno-sse2 \
	-mcmodel=kernel -Iinclude $(LAYOUT_DEFINE)
LDFLAGS := -nostdlib -static -no-pie -Wl,--build-id=none \
	-Wl,-T,linker.ld -Wl,-z,max-page-size=0x1000
SOURCES := src/boot.c src/console.c src/cursor.c src/cursor_data.c \
	src/font.c src/gdt.c src/heap.c src/idt.c src/kernel.c src/mouse.c \
	src/keyboard.c src/keyboard_layout.c src/memory.c src/pmm.c src/serial.c \
	src/paging.c src/pic.c src/pit.c src/ramfs.c src/rtc.c src/scheduler.c \
	src/shell.c src/stat.c src/system.c src/task.c src/timer.c src/util.c
OBJECTS := $(SOURCES:src/%.c=$(BUILD_DIR)/%.o)
ASM_SOURCES := src/context.S src/isr.S
ASM_OBJECTS := $(ASM_SOURCES:src/%.S=$(BUILD_DIR)/%.o)
CURSOR_PNGS := $(wildcard $(CURSOR_DIR)/*.png) \
	$(wildcard $(CURSOR_DIR)/*.PNG)

.PHONY: all clean limine iso test

all: $(ISO)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: src/%.S | $(BUILD_DIR)
	$(CC) -m64 -c $< -o $@

src/cursor_data.c: tools/png2c.py $(CURSOR_PNGS)
	python3 tools/png2c.py --input-dir "$(CURSOR_DIR)"

src/cursor_data.h: src/cursor_data.c
	@test -f $@

$(BUILD_DIR)/cursor.o $(BUILD_DIR)/cursor_data.o: src/cursor_data.h

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

test: src/cursor_data.c | $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc \
		tests/shell_test.c src/shell.c src/ramfs.c src/util.c \
		-o $(BUILD_DIR)/shell_test
	$(BUILD_DIR)/shell_test
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc \
		tests/font_test.c src/font.c -o $(BUILD_DIR)/font_test
	$(BUILD_DIR)/font_test
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc \
		tests/console_test.c src/console.c src/font.c src/util.c \
		-o $(BUILD_DIR)/console_test
	$(BUILD_DIR)/console_test
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc \
		tests/cursor_test.c src/cursor.c src/cursor_data.c \
		-o $(BUILD_DIR)/cursor_test
	$(BUILD_DIR)/cursor_test
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
		tests/memory_test.c src/memory.c src/pmm.c -o $(BUILD_DIR)/memory_test
	$(BUILD_DIR)/memory_test
	$(CC) -std=c11 -Wall -Wextra -Werror -DREA_PAGING_TEST -Iinclude \
		tests/vmm_test.c src/paging.c -o $(BUILD_DIR)/vmm_test
	$(BUILD_DIR)/vmm_test
	$(CC) -std=c11 -Wall -Wextra -Werror -mno-red-zone \
		-mgeneral-regs-only -Iinclude tests/scheduler_test.c \
		src/scheduler.c src/task.c src/context.S \
		-o $(BUILD_DIR)/scheduler_test
	$(BUILD_DIR)/scheduler_test
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude \
		tests/heap_test.c -o $(BUILD_DIR)/heap_test
	$(BUILD_DIR)/heap_test
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc \
		tests/ramfs_test.c src/ramfs.c src/util.c \
		-o $(BUILD_DIR)/ramfs_test
	$(BUILD_DIR)/ramfs_test

clean:
	rm -rf build rea-os.iso rea-os-us.iso
