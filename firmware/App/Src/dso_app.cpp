#include "dso_app.h"
#include "dso_board_bringup.hpp"
#include "dso_bringup_publication.hpp"
#include "dso_acquisition.hpp"
#include "dso_panel_sampler.hpp"
#include "dso_stm32_acquisition.hpp"
#include "dso_trigger_led.hpp"
#include "dso_stm32_panel.hpp"
#include "dso_stm32_display.hpp"
#include "dso_stm32_analog_power.hpp"
#include "dso_stm32_storage.hpp"
#include "dso_timebase.hpp"
#include "dso_ui_controller.hpp"
#include "main.h"
#ifndef DSO_BOARD_QUALIFICATION_COMMANDS
#define DSO_BOARD_QUALIFICATION_COMMANDS 0
#endif
#if DSO_BOARD_QUALIFICATION_COMMANDS
#include "dso_qualification_mailbox.hpp"
#if !DSO_BOARD_BRINGUP || !DSO_BOARD_ACQUISITION || DSO_BOARD_NOMINAL_GAIN != 2
#error "Qualification commands require the UID-bound acquisition gain-2 bench profile"
#endif
#endif
#if DSO_BOARD_NOMINAL_GAIN != 0
#include "dso_nominal_front_end.hpp"
#endif

namespace {

dso::DmaAcquisition acquisition{dso::stm32_acquisition_port()};
dso::RollingAcquisition rolling{dso::stm32_oversampled_rolling_port()};
#if DSO_BOARD_ACQUISITION
dso::PanelSampler panel_sampler{dso::stm32_panel_port(), {3U, 20U, 1U}};
// Guarded nominal classic-board ladder bands, checked against the live tuple
// (SEN2=0, SEN1=4095, coupling=1564). All positions await characterization.
constexpr dso::SenseBands BenchBands{384U, 1100U, 2000U, 3000U};
dso::FrontPanel front_panel{{BenchBands, BenchBands, BenchBands, 20U, 50U, 5U, 20U}};
#else
dso::PanelSampler panel_sampler{dso::stm32_panel_port(), {1U, 4U, 1U}};
// Deliberately invalid electrical bands: a real board profile must supply them.
// Merely constructing the complete application graph never qualifies hardware.
dso::FrontPanel front_panel{{{}, {}, {}, 20U, 50U, 5U, 20U}};
#endif
dso::PanelSuspension panel_suspension{panel_sampler, front_panel};
// The factory constructs RAM callbacks only. Explicit qualification remains
// false; neither this binding nor startup configures or drives LCD pins.
dso::Ili9341Display display{dso::stm32_display_bus()};
dso::ScopeScreen screen{display};
// Nominal selection is an explicit bench topology assumption on the identified
// board. It binds the existing RAM workflow to the board without enabling
// persistence. All default/dormant builds retain identity zero.
#if DSO_BOARD_NOMINAL_GAIN != 0
dso::CalibrationBank calibration_bank{dso::BringupBoardIdentity};
#else
dso::CalibrationBank calibration_bank{0U};
#endif
// The default/dormant profile supplies no Flash callbacks and identity zero,
// which independently inhibits storage. Only the separate DSO_BOARD_STORAGE
// profile below binds the guarded STM32 adapter.
#if DSO_BOARD_STORAGE
// Obtaining the immutable API port does not prepare hardware. The adapter
// denies every operation until the explicit startup quiet-boundary prepare.
dso::StoragePort application_storage_port() noexcept { return dso::stm32_storage_api().port(); }
dso::SnapshotStorage snapshot_storage{application_storage_port()};
dso::ScopeStorage scope_storage{snapshot_storage, dso::BringupBoardIdentity, true};
#else
dso::SnapshotStorage snapshot_storage{dso::StoragePort{}};
dso::ScopeStorage scope_storage{snapshot_storage, 0U, false};
#endif
dso::VoltageCalibration resolve_calibration(void* context, const dso::PhysicalSettings& physical) noexcept
{
#if DSO_BOARD_NOMINAL_GAIN == 2
    static_cast<void>(context);
    auto profile = dso::classic_gain2_nominal_calibration(physical);
    if (profile.kind != dso::CalibrationKind::Unavailable) { profile.zero_code_q16 = DSO_BOARD_ZERO_CODE_Q16; }
    return profile;
#elif DSO_BOARD_NOMINAL_GAIN == 8
    static_cast<void>(context);
    auto profile = dso::classic_gain8_nominal_calibration(physical);
    if (profile.kind != dso::CalibrationKind::Unavailable) { profile.zero_code_q16 = DSO_BOARD_ZERO_CODE_Q16; }
    return profile;
#else
    return static_cast<dso::CalibrationBank*>(context)->resolve(physical);
#endif
}
#if DSO_BOARD_ACQUISITION
constexpr dso::UiControllerConfig DormantUiConfig{true, true, {&calibration_bank, resolve_calibration}, &rolling,
#elif DSO_BOARD_BRINGUP
constexpr dso::UiControllerConfig DormantUiConfig{false, true, {&calibration_bank, resolve_calibration}, &rolling,
#else
constexpr dso::UiControllerConfig DormantUiConfig{false, false, {&calibration_bank, resolve_calibration}, &rolling,
#endif
    dso::RollingProcessing::Filtered25, &scope_storage, &calibration_bank,
    {&panel_suspension, dso::PanelSuspension::set_paused},
    {&panel_suspension, dso::PanelSuspension::set_preparing}, DSO_BOARD_ACQUISITION != 0};
constexpr dso::Stm32DisplayQualification DormantDisplayQualification{false, false, false, false};
constexpr dso::Stm32AnalogPowerQualification DormantAnalogPowerQualification{false, false, false, false};
constexpr dso::Stm32AnalogPowerProfile DormantAnalogPowerProfile{};
constexpr dso::Stm32StorageQualification DormantStorageQualification{false, false, false, false, false};
#if DSO_BOARD_STORAGE
// Explicit profile permissions. These encode operator-authorized bench
// assumptions; they are not evidence of silicon characterization, supply
// integrity, endurance, or power-loss tests.
constexpr dso::Stm32StorageQualification StorageBenchAssumptions{true, true, true, true, true};
#endif
dso::ScopeController scope_controller{acquisition, screen, DormantUiConfig};
#if DSO_BOARD_QUALIFICATION_COMMANDS
// This RAM-only mailbox has no application-storage binding or qualification
// effect. Bring-up's existing startup gate checks the exact UID before idle.
alignas(4) dso::QualificationMailboxWire qualification_mailbox_wire{};
dso::QualificationMailboxConsumer qualification_mailbox_consumer{};

bool route_qualification_action(void* context, dso::QualificationAction action) noexcept
{
    auto* controller = static_cast<dso::ScopeController*>(context);
    if (controller == nullptr) { return false; }
    const auto key = static_cast<std::uint8_t>(action);
    if (key == 0U) { return false; }
    // Public controller button path: ordinary cancellation/rearm ordering is
    // retained, and the next fresh physical release clears synthetic hold.
    controller->buttons({key, 0U, key, false}, HAL_GetTick());
    return true;
}
#endif
static_assert(sizeof(dso::DmaAcquisition) < 6U * 1024U, "Revisit 20KiB SRAM budget");
static_assert(sizeof(acquisition) + sizeof(panel_sampler) + sizeof(front_panel) + sizeof(panel_suspension) +
    sizeof(display) + sizeof(screen) + sizeof(scope_controller) + sizeof(calibration_bank) + sizeof(rolling) +
    sizeof(snapshot_storage) + sizeof(scope_storage) < 16U * 1024U,
    "Leave SRAM for HAL state, the reserved stack and interrupt headroom");
}

// Retained debugger-visible diagnostic state; no USB/UART/display dependency.
DsoBenchStatus dso_bench_status{};
static_assert(sizeof(DsoBenchStatus) == 44U, "Keep diagnostic state bounded");
#if DSO_BOARD_ACQUISITION
volatile dso::BringupAcquisitionStatus dso::dso_bringup_acquisition{};
#endif

extern "C" void dso_app_fault(std::uint32_t reason)
{
    if (dso_bench_status.phase == DSO_BENCH_FAULT) { return; }
    // Publish the terminal state before cleanup. Preserve the initiating reason;
    // later idle/init calls cannot start work or turn a fault into a healthy boot.
    dso_bench_status.fault = reason;
    dso_bench_status.phase = DSO_BENCH_FAULT;
    dso::cancel_stm32_trigger_led();
    // End an outstanding LCD DMA borrow before any foreground owner can be
    // destroyed/reused. With no active transfer this performs no bus access.
    screen.cancel();
    // An early clock/init failure can arrive before peripheral clocks/handles
    // exist. Only a running owner may invoke its MMIO stop callbacks. Ready and
    // Fault already quiesced themselves; construction/Stopped owns no hardware.
    if (acquisition.state() == dso::AcquisitionState::Running) { static_cast<void>(acquisition.stop()); }
    if (rolling.state() == dso::RollingState::Running) { static_cast<void>(rolling.stop()); }
    panel_sampler.stop();
}

extern "C" void dso_app_init(void)
{
    if (dso_bench_status.phase != DSO_BENCH_INITIALIZING) { return; }
    RCC_ClkInitTypeDef clocks{};
    std::uint32_t latency{};
    HAL_RCC_GetClockConfig(&clocks, &latency);
    const auto pclk1_hz = HAL_RCC_GetPCLK1Freq();
    const auto timer_hz = pclk1_hz *
        (clocks.APB1CLKDivider == RCC_HCLK_DIV1 ? 1U : 2U);
    const auto adc_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_ADC);
    dso::TimebasePlan plan{};
    if (HAL_RCC_GetSysClockFreq() != 72000000U || pclk1_hz > 36000000U || timer_hz != 72000000U ||
        adc_hz != 12000000U ||
        !dso::plan_timebase(timer_hz, adc_hz, 15U, 500000U, &plan) ||
        plan.prescaler != 0U || plan.period != 143U) {
        dso_app_fault(1U);
        return;
    }

#if DSO_BOARD_BRINGUP
    if (!dso::activate_bringup_board(display)) { dso_app_fault(7U); return; }
    dso::initialize_stm32_trigger_led();
#if DSO_BOARD_STORAGE
    // UID and flash size have now passed the board activation gate. This is
    // still before panel_sampler.start or acquisition, and the adapter checks
    // TIM1/DMA producer ownership itself. TIM3's optional fixed test-output PWM
    // may continue: its source-checked contract keeps DIER zero with no DMA or
    // interrupt service, like the independent TIM4 carrier. A rejected prepare
    // leaves storage unavailable; ordinary scope startup continues. The
    // unqualified port fails reads and cannot reach program/erase callbacks.
    const auto& storage = dso::stm32_storage_api();
    static_cast<void>(storage.prepare(dso::BringupBoardIdentity, StorageBenchAssumptions));
#endif
#else
    // RAM-only revocation barrier. Retain the complete adapter in the image for
    // inspection/testing without granting pin ownership or touching LCD MMIO.
    if (dso::prepare_stm32_display(DormantDisplayQualification) ||
        dso::stm32_display_state() != dso::Stm32DisplayState::Dormant ||
        dso::stm32_display_fault() != dso::Stm32DisplayFault::Qualification) {
        dso_app_fault(4U); return;
    }
    // This immutable API table retains the shared-TIM4 owner for inspection.
    // False permissions revoke RAM authority only: PB8/PB9 remain untouched.
    const auto& analog_power = dso::stm32_analog_power_api();
    if (analog_power.start(DormantAnalogPowerProfile, DormantAnalogPowerQualification) ||
        analog_power.state() != dso::Stm32AnalogPowerState::Unqualified ||
        analog_power.fault() != dso::Stm32AnalogPowerFault::Qualification ||
        analog_power.carrier_may_be_active()) {
        dso_app_fault(5U); return;
    }
    // Retain the auditable Flash adapter, but grant no access. The journal
    // above still has an empty port and board identity zero independently.
    const auto& storage = dso::stm32_storage_api();
    if (storage.prepare(0U, DormantStorageQualification) ||
        storage.state() != dso::Stm32StorageState::Dormant ||
        storage.fault() != dso::Stm32StorageFault::Qualification) {
        dso_app_fault(6U); return;
    }
#endif
    dso_bench_status.system_clock_hz = HAL_RCC_GetSysClockFreq();
    dso_bench_status.timer_clock_hz = timer_hz;
    dso_bench_status.adc_clock_hz = adc_hz;
    dso_bench_status.sample_divider = plan.divider;
    dso_bench_status.acquisition_bytes = sizeof(acquisition);
    dso_bench_status.panel_sampler_bytes = sizeof(panel_sampler);
    dso_bench_status.ui_bytes = sizeof(front_panel) + sizeof(panel_suspension) + sizeof(display) + sizeof(screen) + sizeof(scope_controller) + sizeof(rolling);
    dso_bench_status.calibration_bank_bytes = sizeof(calibration_bank);
#if DSO_BOARD_ACQUISITION
    if (!panel_sampler.start()) { dso_app_fault(8U); return; }
    dso::dso_bringup_acquisition.ready_ms = HAL_GetTick();
#endif
    dso_bench_status.phase = DSO_BENCH_WAITING_FOR_HARDWARE;
    // Hardware starts/calibration/qualification are deliberately absent here.
    // ADC2 inputs/buttons are configured safely by Cube, but the sampler and
    // DMA port remain dormant until an explicit characterized board profile.
}

extern "C" void dso_app_idle(void)
{
    if (dso_bench_status.phase != DSO_BENCH_WAITING_FOR_HARDWARE) {
        __WFI();
        return;
    }
    const auto now = HAL_GetTick();
#if DSO_BOARD_ACQUISITION
    const auto started = dso::bringup_cycles();
    const bool was_running = acquisition.state() == dso::AcquisitionState::Running;
#elif DSO_BOARD_BRINGUP
    // Buttons only. Electrical switch bands, ADC acquisition and persistence
    // remain unqualified; no ADC conversion is started by this bench profile.
    scope_controller.panel_update(front_panel.update({0U, 0U, 0U,
        dso::bringup_buttons(), now, false}), now);
#endif
#if DSO_BOARD_QUALIFICATION_COMMANDS
    bool qualification_action_boundary{false};
#endif
    // Storage owns the pause boundary. No observation (or old timestamp) can
    // cross it; resumed scans restart their own deadlines from fresh now_ms.
    const auto panel = panel_suspension.paused() ? dso::PanelSamplerUpdate{} : panel_sampler.update(now, 1U);
    if (panel.state == dso::PanelSamplerState::Fault) {
        scope_controller.panel_update(dso::PanelUpdate{{}, {}, false, true, false}, now);
    }
    if (panel.observation_available) {
        const auto update = front_panel.update(panel.observation);
        scope_controller.panel_update(update, now);
#if DSO_BOARD_QUALIFICATION_COMMANDS
        qualification_action_boundary = dso::qualification_action_boundary(
            panel, update, panel_suspension.paused(), scope_controller.storage_busy());
#endif
    }
#if DSO_BOARD_QUALIFICATION_COMMANDS
    if (qualification_action_boundary) {
        static_cast<void>(qualification_mailbox_consumer.poll(
            qualification_mailbox_wire, route_qualification_action, &scope_controller,
            []() noexcept { __DMB(); }));
    }
#endif
    const auto ui_progress = scope_controller.service();
    // Always drain both owner flags; events are discarded while the UI is unhealthy.
    const bool accepted_edge = acquisition.take_accepted_edge() | rolling.take_accepted_edge();
    dso::service_stm32_trigger_led(now, accepted_edge,
        scope_controller.fault() == dso::UiControllerFault::None);
    dso_bench_status.ui_work = static_cast<std::uint32_t>(ui_progress.work);
#if DSO_BOARD_ACQUISITION
    auto& debug = dso::dso_bringup_acquisition;
    const auto elapsed = static_cast<std::uint32_t>(dso::bringup_cycles() - started);
    if (elapsed > debug.max_service_cycles) { debug.max_service_cycles = elapsed; }
    if (was_running && elapsed > debug.max_running_service_cycles) { debug.max_running_service_cycles = elapsed; }
    debug.max_poll_gap_cycles = acquisition.timing_statistics().max_poll_gap_cycles;
    debug.max_snapshot_cycles = acquisition.timing_statistics().max_snapshot_cycles;
    debug.frames_ready = acquisition.statistics().frames_ready;
    debug.acquisition_fault = static_cast<std::uint32_t>(acquisition.fault());
    debug.transport_fault = static_cast<std::uint32_t>(acquisition.transport_fault());
    debug.controller_fault = static_cast<std::uint32_t>(scope_controller.fault());
    debug.panel_fault = static_cast<std::uint32_t>(panel.fault);
    if (ui_progress.render.completed) {
        const auto summary = dso::summarize_bringup_frame(scope_controller.measurements());
        const auto now_ms = HAL_GetTick();
        const auto new_screen_count = debug.screen_count + 1U;
        debug.publication_sequence = new_screen_count * 2U - 1U;
        __DMB();
        debug.screen_count = new_screen_count;
        if (debug.first_screen_ms == 0U) { debug.first_screen_ms = now_ms; }
        debug.last_screen_ms = now_ms;
        debug.frame_code_flags = summary.code_flags;
        debug.frame_frequency_millihz = summary.frequency_millihz;
        debug.physical_generation = summary.physical_generation;
        debug.frame_epoch = summary.acquisition_epoch;
        __DMB();
        debug.publication_sequence = new_screen_count * 2U;
    }
#endif
    // The current transport has TE-only IRQs and foreground HT/TC polling.
    // Never sleep through its sub-millisecond service deadline. Busy panel
    // conversions are also serviced cooperatively rather than waiting on EOC.
    // Slow acquisition's 20ms service bound permits ordinary Sleep between
    // 1ms SysTicks; SysTick/TIM2 must remain live. Fast DWT service stays awake.
    if (acquisition.state() != dso::AcquisitionState::Running &&
        panel.state != dso::PanelSamplerState::Busy && ui_progress.work == dso::UiWork::Idle) { __WFI(); }
}

extern "C" int dso_app_dma_irq(void)
{
    dso::stm32_acquisition_dma_irq();
    return 1;
}
