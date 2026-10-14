ENTRY(_start)

SECTIONS
{
    /* Load the kernel cleanly at 1MB (1M) */
    . = 1M;

    .multiboot : {
        *(.multiboot)
    }

    .text : {
        *(.text)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(.bss COMMON)
    }
}
