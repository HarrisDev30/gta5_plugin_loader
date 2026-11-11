/*
 * Linker script for freestanding PIC Orbx Native hook blob (orbx_native.bin).
 * Layout:
 *   0x0000: .orbx_header (OrbxNativeHeader)
 *   0x0010: .text.entry  (11 fixed 16-byte naked trampolines at +0x10..+0xb0)
 *   0x00c0+: .text* and .rodata*
 * Enforces zero .data, .bss, and .got so the flat binary is strictly PIC.
 */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(orbx_entry_table)

SECTIONS
{
    . = 0x0;

    .orbx_header : {
        KEEP(*(.orbx_header))
        . = ALIGN(0x10);
    }

    .text.entry : {
        KEEP(*(.text.entry))
    }

    .text : {
        *(.text .text.*)
    }

    .rodata : ALIGN(16) {
        *(.rodata .rodata.*)
    }

    .got : {
        *(.got .got.*)
    }

    .data : {
        *(.data .data.* .data.rel.ro .data.rel.ro.*)
    }

    .bss : {
        *(.bss .bss.* COMMON)
    }

    /DISCARD/ : {
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.note*)
        *(.comment)
    }
}

ASSERT(SIZEOF(.orbx_header) == 0x10, "Header must be exactly 16 bytes so entry table starts at 0x10")
ASSERT(SIZEOF(.got) == 0, "PIC violation: .got must be empty")
ASSERT(SIZEOF(.data) == 0, "PIC violation: .data must be empty")
ASSERT(SIZEOF(.bss) == 0, "PIC violation: .bss must be empty")

