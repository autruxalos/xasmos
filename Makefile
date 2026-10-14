# XASMOS — Root dispatcher
ARCH ?= amd64
BOOT ?= uefi

ARCH_MK := make/$(ARCH)/$(BOOT).mk

ifeq ($(wildcard $(ARCH_MK)),)
$(error No fragment $(ARCH_MK). Try: make help)
endif

include $(ARCH_MK)

.PHONY: help
help:
	@echo "XASMOS  ARCH=$(ARCH)  BOOT=$(BOOT)"
	@echo "  fragment: $(ARCH_MK)"
	@echo ""
	@ls -1 make/*/*.mk 2>/dev/null | sed 's|^|  |'
	@echo ""
	@echo "  make ARCH=amd64 BOOT=uefi [efi|image|run|clean]"
	@echo "  make ARCH=amd64 BOOT=bios"
	@echo "  make ARCH=cell  BOOT=lv2"
