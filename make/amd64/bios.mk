# Multiboot2 / legacy — WIP (main.c expects UEFI XeretHandoff*)
BUILD := build/amd64-bios

.PHONY: all efi image run clean

all:
	@echo "BOOT=bios (Multiboot) not ready. Use: make ARCH=amd64 BOOT=uefi"
	@echo "Stubs: source/arch/amd64/boot/exord/bios/"
	@exit 1

efi image run: all

clean:
	rm -rf $(BUILD)
