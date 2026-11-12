/* Linker script - STM32F103C8-class (64 KiB flash, 20 KiB RAM)
 *
 * Layout:
 *   FLASH    0x08000000  58 KiB  code + rodata + data image   (application)
 *   USERDATA 0x0800E800   2 KiB  waveform save slots (E800,EFFF) - preserved
 *   SETTINGS 0x0800F000   4 KiB  storage (four 1 KiB pages):
 *                                F000 settings A, F400 settings B,
 *                                F800 calibration, FC00 vendor device record
 *   RAM      0x20000000  20 KiB  .data, .bss, heap, stacks
 *
 * USERDATA+SETTINGS (E800..FFFF) are reserved so the linker never places code
 * there and the safe flasher preserves them across app image writes.  The
 * application must fit within 58 KiB.  Flash pages are 1 KiB each; storage
 * modules address them by page.
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH    (rx)  : ORIGIN = 0x08000000, LENGTH = 58K
  USERDATA (r)   : ORIGIN = 0x0800E800, LENGTH = 2K
  SETTINGS (r)   : ORIGIN = 0x0800F000, LENGTH = 4K
  RAM     (xrw) : ORIGIN = 0x20000000, LENGTH = 20K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_min_heap_size  = 0x400;   /* 1 KiB heap (kept small: 20 KiB total) */
_min_stack_size = 0x800;   /* 2 KiB main stack                        */

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .linker_symbols : { KEEP(*(.linker_symbols)) } >FLASH

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM : {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)); PROVIDE_HIDDEN(__preinit_array_end = .); } >FLASH
  .init_array    : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))); KEEP(*(.init_array*)); PROVIDE_HIDDEN(__init_array_end = .); } >FLASH
  .fini_array    : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))); KEEP(*(.fini_array*)); PROVIDE_HIDDEN(__fini_array_end = .); } >FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> FLASH

  . = ALIGN(4);
  .bss :
  {
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM

  /* .noinit: RAM that the startup code must NOT touch.
   *
   * The startup copies .data and zeroes .bss, so a counter stored there is
   * reset on every boot and cannot show a watchdog-reset loop.  Variables in
   * this section keep their RAM contents across a warm reset, which is what
   * the boot counter needs.  It is placed AFTER .bss, so the zeroing loop
   * ([_sbss,_ebss)) cannot reach it, and NOLOAD, so nothing is copied from
   * flash into it. */
  .noinit (NOLOAD) :
  {
    . = ALIGN(4);
    _snoinit = .;
    *(.noinit)
    *(.noinit.*)
    . = ALIGN(4);
    _enoinit = .;
  } >RAM

  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _min_heap_size;
    . = . + _min_stack_size;
    . = ALIGN(8);
  } >RAM

  /DISCARD/ : { libc.a(*) libm.a(*) libgcc.a(*) }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
