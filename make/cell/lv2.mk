BUILD := build/cell-lv2

.PHONY: all run clean

all:
	@echo "ARCH=cell BOOT=lv2 not implemented."
	@exit 1

run: all

clean:
	rm -rf $(BUILD)
