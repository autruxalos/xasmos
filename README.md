# XASMOS - Native Exokernel Operating System

XASMOS is an experimental high-performance exokernel operating system written entirely from scratch in 16-bit x86 Assembly. This master repository brings together all of the ecosystem's independent modules into a single project.

📐 Path Specification: SuperDirs (|)

XASMOS completely rejects the legacy UNIX (/) and DOS (\) path standards. Instead, the EXFS file system introduces the SuperDir concept, using the vertical bar | as a unified path delimiter.

System Syntax Rules
Linear Structure: Paths follow the format |directory|file.asm
Direct Mapping: When typing |apps|xsh, the kernel does not traverse a hierarchical directory tree on disk. Instead, it directly looks up the indexed token |system| in the global XOBJ object table. This reduces lookup time to constant-time complexity, $\mathcal{O}(1)$.
POSIX Independence: Eliminates the logical overhead of traditional file descriptors and complex operating system system calls.
🗂️ Ecosystem Architecture

This master repository links together the six essential peripheral projects through modular submodules:

src/boot ── EXORD: Master Boot Record (MBR) bootloader.
src/kernel ── XKERNEL: Secure multiplexing kernel.
src/kernel/drivers/exfs ── EXFS: Inode-free file system driver.
src/init ── EXIT: User-space initialization process.
src/apps ── XSH: Exokernel shell implementing the | prompt.
src/templates ── XEXE: Executable binary standard with a 16-byte header.
🚀 Installation and Build on Void Linux

To clone the complete repository together with all of its components and set up the development environment, run the following commands in your terminal:

# 1. Clone the master repository and download all submodules in a single step
git clone --recursive https://github.com/autruxalos/xasmos.git
cd xasmos

# 2. Build the complete disk image and install the EXFS sectors
make image

# 3. Run the operating system directly in the hardware emulator
make run
