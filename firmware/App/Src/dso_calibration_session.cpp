#include "dso_calibration_session.hpp"
#include "dso_front_panel.hpp"
#include "dso_physical_settings.hpp"

namespace dso {

bool CalibrationSession::active() const noexcept
{
    return state_ == CalibrationSessionState::FirstReference || state_ == CalibrationSessionState::SecondReference ||
        state_ == CalibrationSessionState::ValidationReference || state_ == CalibrationSessionState::Verified;
}

bool CalibrationSession::begin(const PhysicalSettings& physical, CalibrationBank& bank) noexcept
{
    // A new attempt always invalidates any formerly verified session first.
    first_ = CalibrationPoint{};
    fit_ = CalibrationFit{};
    board_identity_ = 0U;
    validation_tolerance_ = 0U;
    state_ = CalibrationSessionState::Failed;
    status_ = CalibrationSessionStatus::Disabled;
    if (bank.board_identity() == 0U) { return false; }
    status_ = CalibrationSessionStatus::Physical;
    if (!calibration_physical_valid(physical)) { return false; }
    first_.physical = physical;
    board_identity_ = bank.board_identity();
    state_ = CalibrationSessionState::FirstReference;
    status_ = CalibrationSessionStatus::None;
    return true;
}

bool CalibrationSession::accept(const CalibrationPoint& point, std::uint32_t tolerance_microvolts) noexcept
{
    status_ = CalibrationSessionStatus::State;
    if (!active() || state_ == CalibrationSessionState::Verified) { return false; }
    if (!same_physical_settings(first_.physical, point.physical)) {
        abort(); status_ = CalibrationSessionStatus::Physical; return false;
    }
    status_ = CalibrationSessionStatus::InvalidPoint;
    if (!calibration_point_valid(point) ||
        tolerance_microvolts > MaximumCalibrationToleranceMicrovolts) { return false; }
    if (state_ == CalibrationSessionState::FirstReference) {
        first_ = point;
        state_ = CalibrationSessionState::SecondReference;
    } else {
        bool accepted{};
        if (state_ == CalibrationSessionState::SecondReference) {
            accepted = fit_.begin(first_, point);
            if (accepted) { state_ = CalibrationSessionState::ValidationReference; }
        } else {
            validation_tolerance_ = tolerance_microvolts;
            accepted = fit_.validate(point, tolerance_microvolts);
            if (accepted) { state_ = CalibrationSessionState::Verified; }
        }
        if (!accepted) {
            state_ = CalibrationSessionState::Failed;
            status_ = CalibrationSessionStatus::Fit;
            return false;
        }
    }
    status_ = CalibrationSessionStatus::None;
    return true;
}

bool CalibrationSession::install(const PhysicalSettings& physical, CalibrationBank& bank) noexcept
{
    status_ = CalibrationSessionStatus::State;
    if (state_ != CalibrationSessionState::Verified) { return false; }
    if (!same_physical_settings(first_.physical, physical)) {
        abort(); status_ = CalibrationSessionStatus::Physical; return false;
    }
    if (bank.board_identity() != board_identity_ || bank.board_identity() == 0U || !bank.install(fit_)) {
        state_ = CalibrationSessionState::Failed;
        status_ = CalibrationSessionStatus::Bank;
        return false;
    }
    state_ = CalibrationSessionState::Installed;
    status_ = CalibrationSessionStatus::None;
    return true;
}

void CalibrationSession::abort() noexcept
{
    fit_ = CalibrationFit{};
    first_ = CalibrationPoint{};
    board_identity_ = 0U;
    validation_tolerance_ = 0U;
    state_ = CalibrationSessionState::Aborted;
    status_ = CalibrationSessionStatus::None;
}

void CalibrationSession::physical_update(const PanelUpdate& update) noexcept
{
    if ((active() || state_ == CalibrationSessionState::Installed) &&
        (!update.ready || update.invalidate_acquisition || update.configuration_changed ||
         !same_physical_settings(first_.physical, update.settings))) {
        abort();
        status_ = CalibrationSessionStatus::Physical;
    }
}

} // namespace dso
