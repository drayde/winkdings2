/*
 * Minimal startup for CH32V203 (QingKe V4B, RV32IMAC).
 * Placed at the start of flash; the core begins executing
 * at 0x08000000 (aliased to 0x00000000) after reset.
 */

    .section .init, "ax", @progbits
    .globl _start
    .p2align 2
_start:
    /* The chip boots through the 0x00000000 flash alias while the
     * image is linked for 0x08000000. Jump to the real address so
     * pc-relative absolute addressing (la gp/sp/_data_vma) computes
     * correctly. (No-op if we already booted at 0x08000000.) */
    lui     t0, 0x08000
    jalr    zero, 8(t0)

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

    /* Ensure the SRAM interface clock is on (AHBPCENR.SRAMEN, bit 2).
     * Without it, any store to 0x20000000 traps with a bus fault. */
    lui     t0, 0x40021
    lw      t1, 0x14(t0)
    ori     t1, t1, 0x04
    sw      t1, 0x14(t0)

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
    /* DIAG: make traps visible — light LED1 (PB1) only.
     * All-8 means normal run; LED1-only means an exception fired. */
    lui     t0, 0x40021
    lw      t1, 0x18(t0)
    ori     t1, t1, 0x0d
    sw      t1, 0x18(t0)
    lui     t0, 0x40010
    lw      t1, 4(t0)
    lui     t2, 0xf9000
    addi    t2, t2, -1
    and     t1, t1, t2
    lui     t2, 0x2000
    or      t1, t1, t2
    sw      t1, 4(t0)
    lui     t0, 0x40011
    addi    t0, t0, -1024       /* GPIOB */
    li      t1, 0x3033
    sw      t1, 0(t0)
    li      t1, 0x02
    sw      t1, 0x10(t0)        /* PB1 high = LED1 */
2:  j       2b
