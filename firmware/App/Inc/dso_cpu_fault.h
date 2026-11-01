#ifndef DSO_CPU_FAULT_H
#define DSO_CPU_FAULT_H

/* Shared C/assembler layout. Do not add integer suffixes to assembly constants. */
#define DSO_CPU_FAULT_MAGIC 0x44534631
#define DSO_CPU_SOFTWARE_TERMINAL_MAGIC 0x44534632
#define DSO_CPU_FAULT_RECORD_BYTES 64
#define DSO_FAULT_IPSR 4
#define DSO_FAULT_EXC_RETURN 8
#define DSO_FAULT_MSP 12
#define DSO_FAULT_PSP 16
#define DSO_FAULT_CFSR 20
#define DSO_FAULT_HFSR 24
#define DSO_FAULT_ICSR 28
#define DSO_FAULT_SHCSR 32
#define DSO_FAULT_MMFAR 36
#define DSO_FAULT_BFAR 40
#define DSO_FAULT_RCC_CIR 44
#define DSO_FAULT_AHBENR 48
#define DSO_FAULT_APB2ENR 52
#define DSO_FAULT_APB1ENR 56
#define DSO_FAULT_ACTIONS 60
/* Attempted action bits; a failed single readback uses the same bit + 16.
 * TIM1 combines DIER request clearing and CR1 counter stop into one action;
 * either failed readback sets its failure bit. Record layout stays unchanged.
 */
#define DSO_FAULT_ACTION_TIM2 0x1
#define DSO_FAULT_ACTION_ADC1 0x2
#define DSO_FAULT_ACTION_ADC2 0x4
#define DSO_FAULT_ACTION_DMA1 0x8
#define DSO_FAULT_ACTION_CSS 0x10
#define DSO_FAULT_ACTION_TIM1 0x20
#define DSO_FAULT_ACTION_DMA2 0x40
#define DSO_FAULT_ACTION_DMA3 0x80
#define DSO_FAULT_ACTION_DMA5 0x100

#ifndef __ASSEMBLER__
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DsoCpuFaultRecord {
    uint32_t magic;
    uint32_t ipsr;
    uint32_t exc_return; /* EXC_RETURN for CPU_FAULT_MAGIC; raw caller LR for SOFTWARE_TERMINAL_MAGIC. */
    uint32_t msp;
    uint32_t psp;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t icsr;
    uint32_t shcsr;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t rcc_cir;
    uint32_t ahbenr;
    uint32_t apb2enr;
    uint32_t apb1enr;
    uint32_t actions;
} DsoCpuFaultRecord;
/* Written only by the terminal assembly entries. The completion magic selects
 * the interpretation of exc_return. Read while halted; BSS does not survive
 * reset and is not an atomic live snapshot.
 */
extern volatile DsoCpuFaultRecord dso_cpu_fault_record;
#ifdef __cplusplus
}
#endif
#endif
#endif
