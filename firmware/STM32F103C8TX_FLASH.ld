/*
 * Copyright 2026 Martin Dodek
 * SPDX-License-Identifier: MIT
 */

ENTRY(Reset_Handler)

_MinHeap = 0;
_MinStack = 0x800;
_estack = ORIGIN(RAM) + LENGTH(RAM);

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 60K
  RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 20K
}

__dso_application_flash_start = ORIGIN(FLASH);
__dso_application_flash_end = ORIGIN(FLASH) + LENGTH(FLASH);
__dso_storage_start = 0x0800F000;
__dso_storage_end = 0x08010000;
__dso_storage_page_bytes = 1024;
__dso_storage_bank_bytes = 2048;

ASSERT(__dso_storage_start == __dso_application_flash_end,
       "storage must begin at the application flash boundary")
ASSERT((__dso_storage_start % __dso_storage_page_bytes) == 0,
       "storage start must be page aligned")
ASSERT((__dso_storage_end % __dso_storage_page_bytes) == 0,
       "storage end must be page aligned")
ASSERT(__dso_storage_end == (__dso_storage_start + 2 * __dso_storage_bank_bytes),
       "storage must span two flash banks")
ASSERT(__dso_storage_end == 0x08010000,
       "storage end must match the device flash boundary")

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
  } > FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text .text.*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } > FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata .rodata.*)
    . = ALIGN(4);
  } > FLASH

  .ARM.extab (READONLY) :
  {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } > FLASH

  .ARM.exidx (READONLY) :
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    __exidx_end = .;
  } > FLASH

  .preinit_array (READONLY) :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(SORT(.preinit_array.*)))
    KEEP(*(.preinit_array))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } > FLASH

  .init_array (READONLY) :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array))
    PROVIDE_HIDDEN(__init_array_end = .);
  } > FLASH

  .fini_array (READONLY) :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } > FLASH

  _sidata = LOADADDR(.data);
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data .data.*)
    . = ALIGN(4);
    __dso_storage_ram_start = .;
    KEEP(*(.RamFunc.dso_storage))
    __dso_storage_ram_end = .;
    *(.RamFunc*)
    . = ALIGN(4);
    _edata = .;
  } > RAM AT> FLASH

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = .;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = .;
  } > RAM

  .noinit (NOLOAD) :
  {
    . = ALIGN(4);
    *(.noinit .noinit.*)
    . = ALIGN(4);
  } > RAM

  ._user_heap_stack (NOLOAD) :
  {
    . = ALIGN(8);
    end = .;
    _end = .;
    . = . + _MinHeap + _MinStack;
    . = ALIGN(8);
  } > RAM

  .ARM.attributes 0 :
  {
    *(.ARM.attributes)
  }
}

ASSERT(LOADADDR(.data) + SIZEOF(.data) <= __dso_application_flash_end,
       "application load image must not enter storage flash")
ASSERT(ADDR(._user_heap_stack) + SIZEOF(._user_heap_stack) <= ORIGIN(RAM) + LENGTH(RAM),
       "RAM heap and stack reservation exceeds RAM")
