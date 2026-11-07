#ifndef DSO_APP_H
#define DSO_APP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Narrow C boundary for platform startup and interrupts.
 * Foreground-only, including Error_Handler before it masks interrupts.
 * Do not call C++ owners from HardFault or acquisition interrupt handlers.
 * Initialization is once per reset; a fatal application fault is terminal.
 */
enum DsoBenchPhase {
    DSO_BENCH_INITIALIZING = 0,
    DSO_BENCH_WAITING_FOR_HARDWARE = 1,
    DSO_BENCH_FAULT = 2
};
/* Stable debugger-visible values stored in DsoBenchStatus.fault. Keep the
 * assigned numbers unchanged; value 2 is intentionally unassigned. */
enum DsoAppFaultReason {
    DSO_APP_FAULT_NONE = 0,
    DSO_APP_FAULT_CLOCK_OR_TIMING = 1,
    DSO_APP_FAULT_HAL_ERROR_HANDLER = 3,
    DSO_APP_FAULT_DORMANT_DISPLAY_GUARD = 4,
    DSO_APP_FAULT_DORMANT_ANALOG_POWER_GUARD = 5,
    DSO_APP_FAULT_DORMANT_STORAGE_GUARD = 6,
    DSO_APP_FAULT_BOARD_ACTIVATION = 7,
    DSO_APP_FAULT_PANEL_SAMPLER_START = 8
};
/* Debugger diagnostics, not an atomic multi-field publication or persistence
 * format. Fields use fixed widths; ui_work contains the C++ UiWork value.
 */
typedef struct DsoBenchStatus {
    volatile uint32_t phase;
    volatile uint32_t fault;
    volatile uint32_t system_clock_hz;
    volatile uint32_t timer_clock_hz;
    volatile uint32_t adc_clock_hz;
    volatile uint32_t sample_divider;
    volatile uint32_t acquisition_bytes;
    volatile uint32_t panel_sampler_bytes;
    volatile uint32_t ui_bytes;
    volatile uint32_t calibration_bank_bytes;
    volatile uint32_t ui_work;
} DsoBenchStatus;
extern DsoBenchStatus dso_bench_status;

/* Startup and terminal failure are rare paths; keep size optimization here
 * independent of the acquisition service loop. */
#if defined(__GNUC__)
__attribute__((cold))
#endif
void dso_app_init(void);
void dso_app_idle(void);
#if defined(__GNUC__)
__attribute__((cold))
#endif
void dso_app_fault(uint32_t reason);
/* Establish 8-byte exception-entry stack alignment before HAL_Init enables SysTick. */
uint32_t dso_app_prepare_core(void);
/* This profile exclusively owns DMA1 channel1; nonzero bypasses HAL flag handling. */
int dso_app_dma_irq(void);
/* TIM1-driven LCD bus channels2/3/5; dormant until explicit board activation. */
void dso_app_lcd_dma_irq(void);
/* USB LP handler only masks its own IRQ; foreground owns class/protocol work. */
void dso_app_usb_irq(void);

#ifdef __cplusplus
}
#endif
#endif
