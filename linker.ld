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

    .bss :
    {
        *(COMMON)
        *(.bss*)
    } :data

    . = ALIGN(0x1000);
    kernel_end = .;
}
