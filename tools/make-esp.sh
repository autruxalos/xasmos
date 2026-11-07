#!/usr/bin/env bash
# XASMOS - creates the FAT32 ESP image that holds BOOTX64.EFI.
# Path: tools/make-esp.sh
#
# Usage: tools/make-esp.sh [path/to/BOOTX64.EFI] [path/to/esp.img]
# Defaults: build/amd64-uefi/esp/EFI/BOOT/BOOTX64.EFI and build/amd64-uefi/esp.img

set -euo pipefail

EFI="${1:-build/amd64-uefi/esp/EFI/BOOT/BOOTX64.EFI}"
IMG="${2:-build/amd64-uefi/esp.img}"
SIZE_MB="${SIZE_MB:-64}"

[ -f "$EFI" ] || { echo "no existe $EFI (compila primero con 'make efi')" >&2; exit 1; }

for tool in mkfs.fat mmd mcopy mdir; do
  command -v "$tool" >/dev/null || { echo "falta $tool (paquetes dosfstools y mtools)" >&2; exit 1; }
done

mkdir -p "$(dirname "$IMG")"
rm -f "$IMG"
dd if=/dev/zero of="$IMG" bs=1M count="$SIZE_MB" status=none
mkfs.fat -F 32 -n XASMOS "$IMG" >/dev/null
mmd -i "$IMG" ::/EFI ::/EFI/BOOT
mcopy -i "$IMG" "$EFI" ::/EFI/BOOT/BOOTX64.EFI

want=$(stat -c%s "$EFI")
got=$(mdir -i "$IMG" ::/EFI/BOOT | awk '/BOOTX64/ {print $3; exit}')
[ "$got" = "$want" ] || { echo "verificación fallida: EFI $want bytes, imagen '${got:-vacía}'" >&2; exit 1; }

echo "ESP lista: $IMG (BOOTX64.EFI, $got bytes)"
