/*
 * Shared linker script for homebrew apps.
 *
 * Apps link with LINK_VA pinned to the upload+exec target address.
 * Default 0x0867f8e4 (the 4 KB freespace at end of OS .text — RX safe,
 * verified by earlier homebrew that executed from here).
 *
 * Override via `make LINK_VA=0xADDR` if a future scratch space appears.
 *
 * payload_entry MUST be the first function in .text — placed via
 * .text.entry section attribute (see HB_APP_ENTRY macro in hb_sdk.h).
 *
 * BSS layout — for LVGL-sized apps:
 *   .bss lives in a NOLOAD section parked at 0x09200000 (safely above
 *   the SDK high-RAM allocations: stub 0x09100000, file cache 0x09118000,
 *   trace 0x09120000, shadow FB 0x09140000-0x09180000). Keeping BSS out
 *   of the loaded binary saves SCSI upload time and avoids overwriting
 *   tens of KB of OS .text with zeros. The HB_APP_ENTRY prologue zeros
 *   __bss_start__..__bss_end__ before any C runtime code runs.
 */

ENTRY(payload_entry)

PROVIDE(LINK_VA = 0x0867f8e4);
PROVIDE(BSS_VA  = 0x09200000);

SECTIONS {
    . = LINK_VA;
    .text : {
        __text_start__ = .;
        *(.text.entry)
        *(.text*)
        *(.rodata*)
        *(.data*)
        . = ALIGN(32);
        __text_end__ = .;
    }

    . = BSS_VA;
    .bss (NOLOAD) : {
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end__ = .;
    }

    /DISCARD/ : {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.note*)
        *(.comment)
    }
}
