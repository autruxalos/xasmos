B := build/amd64-bios
S := source/arch/amd64/boot/exord/bios
all: $(B)/xasmos-bios.img
$(B)/xasmos-bios.img:
	mkdir -p $(B)
	clang -target i686-unknown-none-elf -c $(S)/boot16.s -o $(B)/b16.o
	ld.lld -m elf_i386 --oformat binary -T $(B)/b16.ld $(B)/b16.o -o $(B)/b16.bin
	clang -target i686-unknown-none-elf -c $(S)/multiboot2.s -o $(B)/mb2.o
	clang -target i686-unknown-none-elf -ffreestanding -O2 -I$(S) -c $(S)/bios_handoff.c -o $(B)/bh.o
	clang -target i686-unknown-none-elf -ffreestanding -O2 -I$(S) -c $(S)/bios_main.c -o $(B)/bm.o
	ld.lld -m elf_i386 --nmagic --oformat binary -T $(S)/stage2.ld $(B)/mb2.o $(B)/bh.o $(B)/bm.o -o $(B)/s2.bin
	cat $(B)/b16.bin $(B)/s2.bin > $@ && truncate -s 65536 $@
qemu-run: $(B)/xasmos-bios.img
	qemu-system-x86_64 -drive format=raw,file=$(B)/xasmos-bios.img -serial stdio
