/*
 * Linker fragment for the STEP DMA ring.
 *
 * The ring is placed in SRAM2 rather than SRAM1. On the STM32F407 bus
 * matrix SRAM1 and SRAM2 are separate slave ports (RM0090 Rev 22 §2.1),
 * and the Ethernet MAC's DMA is a separate master. Keeping the STEP ring
 * in SRAM2 while the Ethernet descriptors and the LwIP pbuf pool stay in
 * SRAM1 means Ethernet traffic and STEP generation never contend for the
 * same slave port. That is the structural half of the "Ethernet must not
 * affect STEP timing" requirement; the NVIC priority table is the other.
 *
 * HOW TO APPLY
 * ------------
 * The generated STM32F407VGTX_FLASH.ld declares SRAM1+SRAM2 as one
 * contiguous 128 K RAM region, so it cannot express the split. Replace
 * that region with the two below:
 *
 *   MEMORY {
 *     CCMRAM (xrw) : ORIGIN = 0x10000000, LENGTH = 64K
 *     RAM    (xrw) : ORIGIN = 0x20000000, LENGTH = 112K   -- SRAM1
 *     SRAM2  (xrw) : ORIGIN = 0x2001C000, LENGTH = 16K    -- SRAM2
 *     FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
 *   }
 *
 * then insert the section below AFTER .bss and BEFORE ._user_heap_stack.
 *
 * NOLOAD matters: stepgen_core_blank() writes the whole ring before the
 * timebase ever starts, so there is no reason to spend 4 KB of flash and a
 * 4 KB startup copy on it.
 *
 * On-target test HV-03 verifies at runtime that the ring really landed in
 * SRAM2 - if this fragment is forgotten, the self-test fails rather than
 * the isolation silently disappearing.
 */

  .stepgen_ram (NOLOAD) :
  {
    . = ALIGN(32);
    __stepgen_ram_start__ = .;
    *(.stepgen_ram)
    *(.stepgen_ram*)
    . = ALIGN(32);
    __stepgen_ram_end__ = .;
  } >SRAM2

  ASSERT(__stepgen_ram_end__ - __stepgen_ram_start__ <= 16K,
         "STEP DMA ring does not fit in SRAM2")
