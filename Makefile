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
