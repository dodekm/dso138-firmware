/* Terminal Cortex-M3 exception containment. No stack, calls, HAL or return.
 * See docs/fault-review.md for PM0056/RM0008 evidence and physical limits.
 */
#include "../Inc/dso_cpu_fault.h"
.syntax unified
.cpu cortex-m3
.thumb
.section .text.dso_fault_entry,"ax",%progbits
.align 2
.global dso_fault_entry
.type dso_fault_entry,%function
.thumb_func
dso_fault_entry:
    cpsid i
    ldr r8, =DSO_CPU_FAULT_MAGIC
    b dso_fault_common

.global dso_software_fault_entry
.type dso_software_fault_entry,%function
.thumb_func
dso_software_fault_entry:
    cpsid i
    ldr r8, =DSO_CPU_SOFTWARE_TERMINAL_MAGIC
    b dso_fault_common
.size dso_software_fault_entry, .-dso_software_fault_entry

.global dso_fault_common
.type dso_fault_common,%function
.thumb_func
dso_fault_common:
    ldr r0, =0x40021000
    ldr r4, [r0, #0x08]          /* Original RCC_CIR, before CSS acknowledgement. */
    ldr r5, [r0, #0x14]
    ldr r6, [r0, #0x18]
    ldr r7, [r0, #0x1c]
    movs r3, #0

    /* Stop the trigger source first; never enable a missing peripheral clock. */
    tst r7, #1
    beq .Ltim1
    orr r3, r3, #1
    ldr r1, =0x40000000
    ldr r2, [r1]
    bic r2, r2, #1
    str r2, [r1]
    ldr r2, [r1]
    tst r2, #1
    beq .Ltim1
    orr r3, r3, #0x10000
.Ltim1:
    /* Remove LCD DMA requests before stopping its timer or DMA readers. */
    tst r6, #0x800
    beq .Ladc1
    orr r3, r3, #0x20
    ldr r1, =0x40012c00
    ldr r2, [r1, #0x0c]
    bic r2, r2, #0x700          /* UDE, CC1DE, CC2DE; preserve other sources. */
    str r2, [r1, #0x0c]
    ldr r2, [r1, #0x0c]
    tst r2, #0x700
    beq .Ltim1_counter
    orr r3, r3, #0x200000
.Ltim1_counter:
    ldr r2, [r1]
    bic r2, r2, #1
    str r2, [r1]
    ldr r2, [r1]
    tst r2, #1
    beq .Ladc1
    orr r3, r3, #0x200000       /* Either TIM1 stop readback can fail. */
.Ladc1:
    tst r6, #0x200
    beq .Ladc2
    orr r3, r3, #2
    ldr r1, =0x40012408
    ldr r12, =0x00108101         /* ADON, DMA, EXTTRIG, JEXTTRIG. */
    ldr r2, [r1]
    bic r2, r2, r12
    str r2, [r1]
    ldr r2, [r1]
    tst r2, r12
    beq .Ladc2
    orr r3, r3, #0x20000
.Ladc2:
    tst r6, #0x400
    beq .Ldma
    orr r3, r3, #4
    ldr r1, =0x40012808
    ldr r12, =0x00108001         /* ADC2 has no DMA request. */
    ldr r2, [r1]
    bic r2, r2, r12
    str r2, [r1]
    ldr r2, [r1]
    tst r2, r12
    beq .Ldma
    orr r3, r3, #0x40000
.Ldma:
    tst r5, #1
    beq .Lcss
    orr r3, r3, #8
    ldr r1, =0x40020008
    ldr r2, [r1]
    bic r2, r2, #0x0f
    str r2, [r1]
    ldr r2, [r1]
    tst r2, #0x0f
    beq .Ldma2
    orr r3, r3, #0x80000
.Ldma2:
    orr r3, r3, #0x40
    ldr r2, [r1, #20]           /* DMA1 channel 2 CCR, LCD WR-low phase. */
    bic r2, r2, #0x0f
    str r2, [r1, #20]
    ldr r2, [r1, #20]
    tst r2, #0x0f
    beq .Ldma3
    orr r3, r3, #0x400000
.Ldma3:
    orr r3, r3, #0x80
    ldr r2, [r1, #40]           /* DMA1 channel 3 CCR, LCD WR-high phase. */
    bic r2, r2, #0x0f
    str r2, [r1, #40]
    ldr r2, [r1, #40]
    tst r2, #0x0f
    beq .Ldma5
    orr r3, r3, #0x800000
.Ldma5:
    orr r3, r3, #0x100
    ldr r2, [r1, #80]           /* DMA1 channel 5 CCR, LCD data reader. */
    bic r2, r2, #0x0f
    str r2, [r1, #80]
    ldr r2, [r1, #80]
    tst r2, #0x0f
    beq .Lcss
    orr r3, r3, #0x1000000
.Lcss:
    /* Release the active-low PA15 indicator without a stack or C++ callback.
     * A clock-off port is never accessed; mode and SWD pins stay untouched. */
    tst r6, #4                  /* RCC_APB2ENR.IOPAEN. */
    beq .Lcss_flags
    ldr r1, =0x40010810         /* GPIOA_BSRR: set only the PA15 latch HIGH. */
    movw r2, #0x8000
    str r2, [r1]
.Lcss_flags:
    tst r4, #0x80
    beq .Lrecord
    orr r3, r3, #0x10
    and r2, r4, #0x1f00         /* Preserve only classic F103 ready IRQ enables. */
    orr r2, r2, #0x800000      /* CSSC W1C; other ready flags remain evidence. */
    str r2, [r0, #0x08]
    ldr r2, [r0, #0x08]
    tst r2, #0x80
    beq .Lrecord
    orr r3, r3, #0x100000
.Lrecord:
    dsb sy
    /* Optional diagnostics follow containment attempts: bad SRAM must not
     * prevent the initial peripheral-stop sequence. NMI can replace this
     * record; completion is not a chronological-first-fault guarantee.
     */
    ldr r0, =dso_cpu_fault_record
    movs r1, #0
    str r1, [r0]
    mrs r1, ipsr
    str r1, [r0, #DSO_FAULT_IPSR]
    str lr, [r0, #DSO_FAULT_EXC_RETURN]
    mrs r1, msp
    str r1, [r0, #DSO_FAULT_MSP]
    mrs r1, psp
    str r1, [r0, #DSO_FAULT_PSP]
    ldr r2, =0xe000ed00
    ldr r1, [r2, #0x28]
    str r1, [r0, #DSO_FAULT_CFSR]
    ldr r1, [r2, #0x2c]
    str r1, [r0, #DSO_FAULT_HFSR]
    ldr r1, [r2, #0x04]
    str r1, [r0, #DSO_FAULT_ICSR]
    ldr r1, [r2, #0x24]
    str r1, [r0, #DSO_FAULT_SHCSR]
    ldr r1, [r2, #0x34]
    str r1, [r0, #DSO_FAULT_MMFAR]
    ldr r1, [r2, #0x38]
    str r1, [r0, #DSO_FAULT_BFAR]
    str r4, [r0, #DSO_FAULT_RCC_CIR]
    str r5, [r0, #DSO_FAULT_AHBENR]
    str r6, [r0, #DSO_FAULT_APB2ENR]
    str r7, [r0, #DSO_FAULT_APB1ENR]
    str r3, [r0, #DSO_FAULT_ACTIONS]
    dsb sy
    str r8, [r0]
    dsb sy
.global dso_fault_halt
.type dso_fault_halt,%function
.thumb_func
dso_fault_halt:
    b dso_fault_halt
.global dso_fault_code_end
dso_fault_code_end:
    .ltorg
.size dso_fault_entry, .-dso_fault_entry
.size dso_fault_common, .-dso_fault_common

.global NMI_Handler
.thumb_set NMI_Handler,dso_fault_entry
.global HardFault_Handler
.thumb_set HardFault_Handler,dso_fault_entry
.global MemManage_Handler
.thumb_set MemManage_Handler,dso_fault_entry
.global BusFault_Handler
.thumb_set BusFault_Handler,dso_fault_entry
.global UsageFault_Handler
.thumb_set UsageFault_Handler,dso_fault_entry
