#include "dso_ui_controller.hpp"
#include "dso_front_panel.hpp"
#include "dso_acquisition.hpp"
#include "dso_scope_storage.hpp"

namespace dso
{

bool ScopeController::stored_calibration(void* context, std::uint16_t offset, std::uint8_t* output, std::uint16_t count) noexcept
{ return static_cast<ScopeStorage*>(context)->read_selected_calibration(offset, output, count); }

bool ScopeController::staged_calibration(void* context, std::uint16_t offset, std::uint8_t* output, std::uint16_t count) noexcept
{
    const auto& self = *static_cast<ScopeController*>(context);
    if (output == nullptr || offset > CalibrationSupportedRecordMaximum ||
        count > CalibrationSupportedRecordMaximum - offset) { return false; }
    const auto& acquisition = self.acquisition_;
    if (!acquisition.recall_staging_active()) { return false; }
    const auto* bytes = acquisition.recall_staging_bytes();
    for (std::uint16_t i{}; i < count; ++i) { output[i] = bytes[offset + i]; }
    return true;
}

void ScopeController::finish_storage(UiStorageStatus status) noexcept
{
    const bool boot = storage_.boot_active;
    storage_.boot_active = false;
    acquisition_.end_recall_staging();
    storage_.phase = StoragePhase::Idle;
    storage_.command = StorageCommand::None;
    storage_.status = status;
    ++host_.settings_revision;
    if (config_.storage_input.pause != nullptr) { config_.storage_input.pause(config_.storage_input.context, false); }
    static_cast<void>(ui_.take_actions());
    if (boot && config_.storage_input.pause != nullptr) { storage_.boot_waiting_input = true; }
    reconcile_acquisition_readiness();
    redraw();
}

void ScopeController::start_storage(StorageCommand command) noexcept
{
    invalidate_export();
    if (command == StorageCommand::RestoreSettings || command == StorageCommand::RestoreCalibration) { calibration_session_.abort(); }
    if (config_.storage == nullptr || config_.storage->state() == ScopeStorageState::Disabled) {
        storage_.status = UiStorageStatus::Disabled; redraw(); return;
    }
    if (command == StorageCommand::SaveCalibration || command == StorageCommand::RestoreCalibration) {
        if (config_.calibration_bank == nullptr || config_.calibration_bank->board_identity() == 0U) {
            storage_.status = UiStorageStatus::Disabled; redraw(); return;
        }
        if (config_.calibration_bank->board_identity() != config_.storage->board_identity()) {
            storage_.status = UiStorageStatus::Error; redraw(); return;
        }
    }
    const bool boot_open = storage_.boot_active && command == StorageCommand::None;
    if ((!boot_open && (ui_.wants_run() || ui_.state() != UiRunState::Hold)) ||
        acquisition_running() || processing_.pending_frame_consumption ||
        (boot_open && (fault_ != UiControllerFault::None || ui_.state() == UiRunState::Fault ||
                       retained_frame_.valid || processing_.work != UiWork::Idle)) ||
        acquisition_.state() != AcquisitionState::Stopped ||
        processing_.work == UiWork::Copying || processing_.work == UiWork::Measuring) {
        if (boot_open) { finish_storage(UiStorageStatus::Error); }
        else { storage_.status = UiStorageStatus::HoldRequired; redraw(); }
        return;
    }
    cancel_consumers();
    storage_.command = command;
    if (config_.storage_input.pause != nullptr) {
        config_.storage_input.pause(config_.storage_input.context, true);
        panel_update(PanelUpdate{{}, {0U, 0U, DebouncedButtons::All, true}, false, true, false}, 0U);
    }
    if (!config_.storage->begin_open()) { finish_storage(UiStorageStatus::Error); return; }
    storage_.phase = StoragePhase::Opening;
    storage_.status = UiStorageStatus::Busy;
    // Freeze gestures before borrowing samples or a stopped DMA workspace.
    ui_.buttons({0U, 0U, 0U, true}, 0U);
    static_cast<void>(ui_.take_actions());
}

void ScopeController::service_storage() noexcept
{
    auto& store = *config_.storage;
    static_cast<void>(ui_.take_actions()); // Physical updates cannot arm during storage.
    if (storage_.phase == StoragePhase::Opening || storage_.phase == StoragePhase::Saving)
    {
        const auto progress = store.step(16U);
        if (progress.state == ScopeStorageState::Error)
        {
            finish_storage(UiStorageStatus::Error);
            return;
        }
        if (progress.state != ScopeStorageState::Ready)
        {
            return;
        }
        if (storage_.phase == StoragePhase::Saving)
        {
            finish_storage(UiStorageStatus::Saved);
            return;
        }
        const auto* old = store.record();
        const bool boot = storage_.command == StorageCommand::None && storage_.boot_active;
        const bool boot_bank = storage_.command == StorageCommand::None && old != nullptr &&
                               old->calibration_bytes != 0U && config_.calibration_bank != nullptr &&
                               config_.calibration_bank->board_identity() != 0U;
        if ((storage_.command == StorageCommand::None && !boot_bank) ||
            storage_.command == StorageCommand::RestoreSettings)
        {
            if (old == nullptr)
            {
                finish_storage(UiStorageStatus::Empty);
                return;
            }
            const bool restored = boot ? ui_.restore_boot_settings(old->settings)
                                       : ui_.restore_settings(old->settings);
            finish_storage(restored ? UiStorageStatus::Restored : UiStorageStatus::Error);
            return;
        }
        if (storage_.command == StorageCommand::RestoreCalibration || boot_bank)
        {
            if (old == nullptr || old->calibration_bytes == 0U)
            {
                finish_storage(UiStorageStatus::Empty);
                return;
            }
            if (boot_bank && config_.calibration_bank->board_identity() != store.board_identity())
            {
                finish_storage(UiStorageStatus::Error);
                return;
            }
            if (!acquisition_.begin_recall_staging())
            {
                finish_storage(UiStorageStatus::Error);
                return;
            }
            storage_.copied_bytes = 0U;
            storage_.phase = StoragePhase::Reading;
            return;
        }
        if (!store.writable())
        {
            finish_storage(UiStorageStatus::ReadOnly);
            return;
        }
        SavedRecord record{};
        SavedByteSource calibration_source{};
        if (old != nullptr)
        {
            record.settings = old->settings;
            record.calibration_bytes = old->calibration_bytes;
            if (record.calibration_bytes != 0U)
            {
                calibration_source = {&store, stored_calibration};
            }
        }
        record.board_identity = store.board_identity();
        if (storage_.command == StorageCommand::SaveSettings || old == nullptr)
        {
            record.settings = ui_.settings();
        }
        if (storage_.command == StorageCommand::SaveCalibration)
        {
            if (!acquisition_.begin_recall_staging())
            {
                finish_storage(UiStorageStatus::Error);
                return;
            }
            const auto count = config_.calibration_bank->export_record(
                acquisition_.recall_staging_bytes(), CalibrationSupportedRecordMaximum);
            if (count == 0U)
            {
                finish_storage(UiStorageStatus::Error);
                return;
            }
            record.calibration_bytes = static_cast<std::uint16_t>(count);
            calibration_source = {this, staged_calibration};
        }
        if (!store.begin_save(record, calibration_source))
        {
            finish_storage(UiStorageStatus::Error);
            return;
        }
        storage_.phase = StoragePhase::Saving;
        return;
    }
    const auto *record = store.record();
    if (record == nullptr)
    {
        finish_storage(UiStorageStatus::Error);
        return;
    }
    if (storage_.phase == StoragePhase::Reading)
    {
        const auto remaining = static_cast<std::uint16_t>(record->calibration_bytes - storage_.copied_bytes);
        const auto count = static_cast<std::uint16_t>(remaining > 32U ? 32U : remaining);
        auto* staging = acquisition_.recall_staging_bytes();
        auto* bytes = staging + storage_.copied_bytes;
        if (count != 0U && !store.read_selected_calibration(storage_.copied_bytes, bytes, count))
        {
            finish_storage(UiStorageStatus::Error);
            return;
        }
        storage_.copied_bytes = static_cast<std::uint16_t>(storage_.copied_bytes + count);
        if (storage_.copied_bytes != record->calibration_bytes)
        {
            return;
        }
        const auto* staged = staging;
        const bool boot = storage_.command == StorageCommand::None;
        auto& bank = *config_.calibration_bank;
        if (!calibration_record_valid(staged, record->calibration_bytes, bank.board_identity()) ||
            (boot && !ui_.can_restore_boot_settings(record->settings)))
        {
            finish_storage(UiStorageStatus::Error);
            return;
        }
        if (!bank.import_record(staged, record->calibration_bytes) ||
            (boot && !ui_.restore_boot_settings(record->settings)))
        {
            finish_storage(UiStorageStatus::Error);
            return;
        }
        calibration_status_ = UiCalibrationStatus::Updated;
        finish_storage(UiStorageStatus::Restored);
        return;
    }
}

} // namespace dso
