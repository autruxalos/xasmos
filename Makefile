ARCH ?= amd64
BOOT ?= uefi
ARCH_MK := make/$(ARCH)/$(BOOT).mk
ifeq ($(wildcard $(ARCH_MK)),)
$(error No fragment $(ARCH_MK))
endif
include $(ARCH_MK)

.PHONY: help
help:
	@echo "ARCH=$(ARCH) BOOT=$(BOOT) -> $(ARCH_MK)"
	@ls -1 make/*/*.mk

efi:
	@make ARCH=$(ARCH) BOOT=uefi all
	@mkdir -p build && echo uefi > build/.bootmode
bios:
	@make ARCH=$(ARCH) BOOT=bios all
	@mkdir -p build && echo bios > build/.bootmode
qemu:
	@make ARCH=$(ARCH) BOOT=$$(cat build/.bootmode 2>/dev/null || echo uefi) qemu-run
