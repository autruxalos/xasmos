# XASMOS

XASMOS is an exokernel for amd64, written in C and NASM, that boots through
UEFI. Its goal is to give applications direct control over hardware resources,
with the kernel enforcing ownership rather than abstracting it away.

Licensed under the GNU General Public License v3.0 or later. See
[LICENSE](LICENSE). The name and logo are covered by [TRADEMARK.md](TRADEMARK.md).

## Status

Early development. The system boots on real emulated hardware (QEMU with
OVMF) and runs an interactive shell. Many subsystems are incomplete; see
[Roadmap](#roadmap).

## Components

| Component | Path | Role |
|---|---|---|
| exord | `source/arch/amd64/boot/exord/` | UEFI bootloader |
| xeret | `source/arch/amd64/kernel/xeret/` | Kernel: memory, paging, interrupts, processes |
| xsh | `source/arch/amd64/apps/xsh/` | Interactive shell and coreutils |
| core | `source/core/` | Portable modules: EXFS, block layer, device registry, ExB loader |
| drivers | `source/arch/amd64/drivers/` | Hardware drivers: PCI, NVMe, storage |

The portable code in `source/core/` does not depend on the architecture and
is compiled and tested on the host.

## What works

- UEFI boot, with the kernel taking over from firmware services.
- Physical and virtual memory management, with per-process address spaces.
- PS/2 keyboard input and a framebuffer console.
- PCI enumeration and NVMe storage (polled driver).
- EXFS, a filesystem with directories, extents, copy-on-write commits, and
  byte-range I/O.
- A shell (`xsh`) with file and disk commands.

## What is not finished

- USB (xHCI) and SATA (AHCI) drivers.
- Persistent storage for the group map and superblock v3 in the kernel build.
- Larger files and volumes: the current limit is 64 GiB per volume.
- Graphical environment.
- Legacy BIOS boot and the PS3 target, which exist as build fragments but are
  not complete.

## Building

Requirements for the default target (amd64, UEFI):

- `clang` with the `x86-64` target
- `lld-link`
- `make`

```sh
make                  # builds build/amd64-uefi/esp/EFI/BOOT/BOOTX64.EFI
make help             # lists the available build fragments
```

## Running in QEMU

Create a FAT image with the bootloader and a test NVMe disk. The NVMe serial
must be `xasmos`: the storage layer refuses to write to other disks.

```sh
qemu-system-x86_64 \
  -machine q35 -cpu qemu64 -m 512M \
  -drive if=pflash,format=raw,readonly=on,file=OVMF_CODE.fd \
  -drive file=esp.img,format=raw \
  -drive file=xasmos.img,format=raw,if=none,id=nvm \
  -device nvme,drive=nvm,serial=xasmos \
  -serial stdio
```

## Tests

Host tests for the portable modules, with sanitizers:

```sh
gcc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror -Wpedantic \
  -Isource/core/blk -Isource/core/exfs tests/core/exfs/exfs_test.c \
  source/core/exfs/exfs.c source/core/exfs/gmap.c source/core/exfs/sb.c \
  source/core/blk/blk.c -o /tmp/exfs_test && /tmp/exfs_test
```

Equivalent commands exist for `tests/core/exfs/gmap_test.c`,
`sb_test.c`, `tests/core/dev/dev_test.c` and `tests/core/blk/blk_test.c`.

## Repository layout
