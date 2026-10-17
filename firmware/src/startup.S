/*
 * Minimal startup for CH32V203 (QingKe V4B, RV32IMAC).
 * Placed at the start of flash; the core begins executing
 * at 0x08000000 (aliased to 0x00000000) after reset.
 */

    .section .init, "ax", @progbits
    .globl _start
    .p2align 2
_start:
    /* gp must be loaded with relaxation disabled */
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop

    la      sp, _eusrstack

    /* Point mtvec at a dummy trap handler (direct mode).
     * Interrupts are not used by this example. */
    la      t0, _trap
    csrw    mtvec, t0

    /* Copy .data from flash to RAM */
    la      a0, _data_lma
    la      a1, _data_vma
    la      a2, _edata
1:  bgeu    a1, a2, 2f
    lw      t0, 0(a0)
    sw      t0, 0(a1)
    addi    a0, a0, 4
    addi    a1, a1, 4
    j       1b
2:
    /* Zero .bss */
    la      a0, _sbss
    la      a1, _ebss
1:  bgeu    a0, a1, 2f
    sw      zero, 0(a0)
    addi    a0, a0, 4
    j       1b
2:
    call    main

1:  j       1b          /* main returned: hang */

    .p2align 2
_trap:
    j       _trap       /* exceptions/NMIs land here */
