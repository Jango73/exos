PHDRS
{
    text PT_LOAD FLAGS(5);       /* R | X */
    data PT_LOAD FLAGS(6);       /* R | W */
    dynamic PT_DYNAMIC FLAGS(6); /* R | W */
    tls PT_TLS FLAGS(4);         /* R */
    stack PT_GNU_STACK FLAGS(6); /* R | W */
}

SECTIONS
{
    . = SIZEOF_HEADERS;

    .hash : {
        *(.hash)
    } :text

    .gnu.hash : {
        *(.gnu.hash)
    } :text

    .dynsym : {
        *(.dynsym)
    } :text

    .dynstr : {
        *(.dynstr)
    } :text

    .rel.dyn : {
        *(.rel.dyn)
    } :text

    .rela.dyn : {
        *(.rela.dyn)
    } :text

    .rel.plt : {
        *(.rel.plt)
    } :text

    .rela.plt : {
        *(.rela.plt)
    } :text

    .plt : {
        *(.plt .plt.*)
    } :text

    .text : {
        *(.text .text.*)
    } :text

    .rodata : {
        *(.rodata .rodata.*)
    } :text

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .dynamic : {
        *(.dynamic)
    } :data :dynamic

    .got : {
        *(.got .got.*)
    } :data

    .got.plt : {
        *(.got.plt .got.plt.*)
    } :data

    .data : {
        *(.data .data.*)
    } :data

    .tdata : {
        *(.tdata .tdata.*)
    } :data :tls

    .tbss (NOLOAD) : {
        *(.tbss .tbss.*)
    } :data :tls

    .bss (NOLOAD) : {
        *(global)
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /DISCARD/ : {
        *(.comment)
        *(.note.GNU-stack)
    }
}
