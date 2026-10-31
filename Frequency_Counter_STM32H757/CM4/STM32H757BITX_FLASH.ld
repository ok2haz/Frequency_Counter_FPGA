/*
******************************************************************************
**
**  File        : LinkerScript.ld
**
**  Author      : STM32CubeIDE
**
**  Abstract    : Linker script for STM32H7 series
**                1024Kbytes FLASH and 160Kbytes RAM (D2 SRAM2+3; SRAM1 = CM7)
**
**                Set heap size, stack size and stack location according
**                to application requirements.
**
**                Set memory bank area and size if external memory is used.
**
**  Target      : STMicroelectronics STM32
**
**  Distribution: The file is distributed as is, without any warranty
**                of any kind.
**
*****************************************************************************
** @attention
**
** Copyright (c) 2024 STMicroelectronics.
** All rights reserved.
**
** This software is licensed under terms that can be found in the LICENSE file
** in the root directory of this software component.
** If no LICENSE file comes with this software, it is provided AS-IS.
**
****************************************************************************
*/

/* Entry Point */
ENTRY(Reset_Handler)

/* Highest address of the user mode stack */
_estack = ORIGIN(RAM) + LENGTH(RAM); /* end of RAM */
/* Generate a link error if heap and stack don't fit into RAM */
_Min_Heap_Size = 0x200;      /* required amount of heap  */
_Min_Stack_Size = 0x400; /* required amount of stack */

/* Specify the memory areas */
MEMORY
{
FLASH (rx)     : ORIGIN = 0x08100000, LENGTH = 1024K
RAM (xrw)      : ORIGIN = 0x10020000, LENGTH = 128K  /* D2 SPLIT: CM4 = SRAM2 (128K) pres CM4 alias. SRAM1 (0x10000000, 128K) patri CM7 (viz CM7 linker RAM_D2 + NAVRH §11.5). Drive 0x10000000/288K = kolidovalo s CM7 diagnostikou D2.
                                                        ⚠️ ZKRACENO 160K -> 128K (2026-08-22, ETH F3): SRAM3 se vyclenila do ETH_D2 nize. Kdyby RAM dal sahala pres SRAM3, linker by tam umistil .bss/.data a TICHE by to prepsalo ETH DMA deskriptory (stejna fyzicka pamet, jen jina adresa). CM4 dnes zabira ~4 KB, 128K je s velkou rezervou dost. */
/* SRAM4 / domena D3 — sdilena pamet IPC CM7<->CM4. ⚠️ MUSI mit shodnou adresu
   i velikost jako v CM7 linkeru (checklist B) a shodnou s `IPC_BASE`
   v `ipc_shared.h`. Do 2026-09-19 tu tenhle region CHYBEL, takze na strane CM4
   nechranilo IPC nic (audit F-0016). Cely ho rezervuje sekce `.ipc_shared` nize —
   tim kazde dalsi umisteni do RAM_D3 region pretece a link selze. */
RAM_D3 (xrw)   : ORIGIN = 0x38000000, LENGTH = 64K
ETH_D2 (xrw)   : ORIGIN = 0x30040000, LENGTH = 32K   /* SRAM3 — ⚠️ SYSTEMOVA (AHB) adresa, NE CM4 alias 0x10040000!
                                                        ETH DMA je AHB master a vidi D2 SRAM na 0x300xxxxx; ukazatel z CM4 aliasu by DMA nerozlouskla.
                                                        CM4 CPU na tuhle adresu dosahne taky (systemova mapa je spolecna), takze staci jedna adresa pro obe strany.
                                                        Lezi uvnitr casti D2, kterou dle splitu vlastni CM4 (SRAM2+3) -> zadna kolize s CM7. */
}

/* Define output sections */
SECTIONS
{
  /* The startup code goes first into FLASH */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector)) /* Startup code */
    . = ALIGN(4);
  } >FLASH

  /* The program code and other data goes into FLASH */
  .text :
  {
    . = ALIGN(4);
    *(.text)           /* .text sections (code) */
    *(.text*)          /* .text* sections (code) */
    *(.glue_7)         /* glue arm to thumb code */
    *(.glue_7t)        /* glue thumb to arm code */
    *(.eh_frame)

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    _etext = .;        /* define a global symbols at end of code */
  } >FLASH

  /* Constant data goes into FLASH */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);
  } >FLASH

  .ARM.extab (READONLY) : /* The READONLY keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } >FLASH
  .ARM (READONLY) : /* The READONLY keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array (READONLY) : /* The READONLY keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >FLASH
  .init_array (READONLY) : /* The READONLY keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >FLASH
  .fini_array (READONLY) : /* The READONLY keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } >FLASH

  /* used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  /* Initialized data sections goes into RAM, load LMA copy after code */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;        /* create a global symbol at data start */
    *(.data)           /* .data sections */
    *(.data*)          /* .data* sections */
    *(.RamFunc)        /* .RamFunc sections */
    *(.RamFunc*)       /* .RamFunc* sections */

    . = ALIGN(4);
    _edata = .;        /* define a global symbol at data end */
  } >RAM AT> FLASH


  /* Uninitialized data section */
  . = ALIGN(4);
  .bss :
  {
    /* This is used by the startup in order to initialize the .bss section */
    _sbss = .;         /* define a global symbol at bss start */
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)

    . = ALIGN(4);
    _ebss = .;         /* define a global symbol at bss end */
    __bss_end__ = _ebss;
  } >RAM

  /* User_heap_stack section, used to check that there is enough RAM left */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /* ── ETH DMA deskriptory + bufery (SRAM3 @ 0x30040000, systemova adresa) ──────
   * Bez teto sekce byly `.RxDescripSection`/`.TxDescripSection` z generovaneho
   * `eth.c` ORPHAN sekce a ld je slozil do `.data` na 0x10020010 — tedy do CM4
   * ALIASU D2, na ktery ETH DMA nedosahne (a jeste je tahal z flash, protoze
   * `.data` je LOADED). Overeno `nm`: DMARxDscrTab @ 0x10020010 typ 'D'.
   * NOLOAD = deskriptory se needitujou z flash (inicializuje je HAL_ETH_Init).
   * ⚠️ `.RxDecripSection` (bez 's') je ST preklep ze starsich verzi HAL/prikladu —
   * uvedeno taky, aby to prezilo vymenu HAL nebo import ST lwIP prikladu. */
  .eth_dma (NOLOAD) :
  {
    . = ALIGN(4);
    _seth_dma = .;
    *(.RxDescripSection)
    *(.RxDecripSection)
    . = ALIGN(4);
    *(.TxDescripSection)
    *(.TxDecripSection)
    . = ALIGN(4);
    *(.RxArraySection)      /* lwIP RX pool (az F5) */
    *(.Rx_PoolSection)
    . = ALIGN(4);
    _eeth_dma = .;
  } >ETH_D2

  /* IPC sdilena pamet CM7<->CM4 (SRAM4 / domena D3, 0x38000000, 64 KB) — ZRCADLO
   * teze sekce v CM7 linkeru (audit F-0016).
   * 🔴 PROC to tady musi byt: `g_ipc` je makro nad PEVNOU ADRESOU
   * (`ipc_shared.h: IPC_BASE`), takze linker o te pameti nevi. Do 2026-09-19
   * CM4 linker `RAM_D3` neznal VUBEC, takze na strane CM4 nechranilo IPC nic —
   * checklist B pritom zada „shodna adresa i velikost v OBOU linker skriptech".
   * Rezervace zabira cely region, takze kazde dalsi umisteni do RAM_D3 ho PRETECE
   * a link SELZE. Bez toho by prvni takovy objekt pristal na `0x38000000`, tedy
   * na hlavicce IPC, a projevilo by se to jako „IPC obcas nenabehne".
   * ⚠️ NOLOAD — obsah se plni za behu, nic se odsud nenacita z FLASH.
   * ⚠️ Nesmi se do ni nic umistovat: obe jadra na tu pamet sahaji pres `g_ipc`,
   * tedy pres pevnou adresu. Sekce je tu VYHRADNE jako rezervace. */
  .ipc_shared (NOLOAD) :
  {
    . = ALIGN(32);
    _sipc_shared = .;
    KEEP(*(.ipc_shared))
    KEEP(*(.ipc_shared*))
    . = _sipc_shared + 64K;   /* absolutne, viz komentar v CM7 linkeru */
    _eipc_shared = .;
  } >RAM_D3

  /* Adresa je zdvojena (`IPC_BASE` v hlavicce vs. `RAM_D3` tady) — tenhle ASSERT
   * je jedine misto, kde se ty dve pravdy potkaji (trida L-0018). */
  ASSERT(_sipc_shared == 0x38000000, "IPC: .ipc_shared nezacina na IPC_BASE (0x38000000) — srovnej linker a ipc_shared.h")

  /* Remove information from the standard libraries */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}


