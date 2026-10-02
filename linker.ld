ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x10000;

    .text :
    {
        *(.text*)
    } :text

    .rodata :
    {
        *(.rodata*)
    } :text

    . = ALIGN(0x1000);
    user_image_start = .;
    .user :
    {
        *(.user_text*)
        *(.user_rodata*)
    } :text
    . = ALIGN(0x1000);
    user_image_end = .;

    . = ALIGN(0x1000);
    .data :
    {
        *(.data*)
    } :data
    ASSERT(. < 0x80000, "Kernel image overlaps reserved initrd memory")

    /* Keep growing runtime state away from the BIOS/VGA aperture. */
    . = 0x100000;
    .bss (NOLOAD) :
    {
        bss_start = .;
        *(COMMON)
        *(.bss*)
        bss_end = .;
    } :data

    . = ALIGN(0x1000);
    kernel_end = .;
    ASSERT(kernel_end < 0x400000, "Kernel BSS exceeds initial identity mapping")
}
