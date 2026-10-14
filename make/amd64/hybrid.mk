BUILD := build/amd64-hybrid

.PHONY: all efi image run clean

all:
	@echo "HYBRID after BOOT=uefi and BOOT=bios both work."
	@exit 1

efi image run: all

clean:
	rm -rf $(BUILD)
