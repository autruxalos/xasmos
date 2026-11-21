#!/bin/sh
# make-esp.sh EFI_IN IMG_OUT
# Builds a FAT32 EFI System Partition with BOOTX64.EFI. Uses mtools only, so it
# needs no root, no mount and no mkfs.fat: the same script works on Linux and
# OpenBSD. Size: ESP_MB (default 64).
set -eu
EFI="$1"
IMG="$2"
SIZE_MB="${ESP_MB:-64}"

[ -f "$EFI" ] || { echo "make-esp: missing $EFI" >&2; exit 1; }
for t in mformat mmd mcopy mdir; do
    command -v "$t" >/dev/null || { echo "make-esp: needs mtools ($t)" >&2; exit 1; }
done

mkdir -p "$(dirname "$IMG")"
dd if=/dev/zero of="$IMG" bs=1M count="$SIZE_MB" status=none
mformat -i "$IMG" -F -v XASMOS ::
mmd -i "$IMG" ::/EFI ::/EFI/BOOT
mcopy -i "$IMG" "$EFI" ::/EFI/BOOT/BOOTX64.EFI
mdir -i "$IMG" ::/EFI/BOOT
echo "ESP lista: $IMG (BOOTX64.EFI, $(wc -c < "$EFI") bytes)"
