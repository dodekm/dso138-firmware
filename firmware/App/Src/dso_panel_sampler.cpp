#include "dso_panel_sampler.hpp"
#include "dso_adc_codes.hpp"
#include "dso_u32_counter.hpp"

namespace dso {
void PanelSuspension::set_paused(void* context, bool paused) noexcept
{
    static_cast<PanelSuspension*>(context)->pause(paused, true);
}
void PanelSuspension::set_preparing(void* context, bool paused) noexcept
{
    static_cast<PanelSuspension*>(context)->pause(paused, false);
}
void PanelSuspension::pause(bool paused, bool resynchronize) noexcept
{
    if (paused_ == paused) { return; }
    paused_ = paused;
    if (paused) {
        resume_ = sampler_.running();
        sampler_.stop();
        if (resynchronize) { panel_.resynchronize(); }
    } else if (resume_) {
        resume_ = false;
        // A latched fault makes start fail; leave it intact for explicit repair.
        static_cast<void>(sampler_.start());
    }
}
namespace {

bool interval_valid(std::uint32_t interval) noexcept
{
    return interval > 0U && interval < counter_contract::U32HalfRange;
}

PanelSamplerFault permanent_fault(const PanelSamplerPort& port,
                                   const PanelSamplerConfig& config) noexcept
{
    if (port.start == nullptr || port.poll == nullptr || port.abort == nullptr ||
        port.read_gpio_b == nullptr) {
        return PanelSamplerFault::InvalidPort;
    }
    if (!interval_valid(config.conversion_timeout_ms) ||
        !interval_valid(config.maximum_scan_age_ms) ||
        !interval_valid(config.scan_interval_ms)) {
        return PanelSamplerFault::InvalidConfiguration;
    }
    return PanelSamplerFault::None;
}

} // namespace

PanelSampler::PanelSampler(PanelSamplerPort port, PanelSamplerConfig config) noexcept
    : port_{port}, config_{config}, permanent_fault_{permanent_fault(port, config)},
      fault_{permanent_fault_}
{
}

bool PanelSampler::start() noexcept
{
    if (enabled_ || fault_ != PanelSamplerFault::None) {
        return false;
    }
    enabled_ = true;
    observed_time_ = false;
    started_scan_ = false;
    phase_ = Phase::Idle;
    return true;
}

void PanelSampler::discard_channels() noexcept
{
    channels_[0U] = 0U;
    channels_[1U] = 0U;
    channels_[2U] = 0U;
    channel_ = 1U;
}

void PanelSampler::stop() noexcept
{
    if (needs_abort_ && port_.abort != nullptr) {
        port_.abort(port_.context);
    }
    needs_abort_ = false;
    enabled_ = false;
    phase_ = Phase::Idle;
    discard_channels();
}

void PanelSampler::reset() noexcept
{
    stop();
    fault_ = permanent_fault_;
    observed_time_ = false;
    started_scan_ = false;
    epoch_ = 0U;
}

void PanelSampler::fail(PanelSamplerFault reason) noexcept
{
    if (fault_ == PanelSamplerFault::None) {
        fault_ = reason;
    }
    discard_channels();
    phase_ = needs_abort_ ? Phase::FaultAbort : Phase::FaultGpio;
}

PanelSamplerUpdate PanelSampler::result(PanelSamplerState state,
                                        std::uint32_t now_ms) const noexcept
{
    return PanelSamplerUpdate{state, fault_, false,
        PanelObservation{0U, 0U, 0U, 0U, now_ms, false}, epoch_};
}

PanelSamplerUpdate PanelSampler::update(std::uint32_t now_ms,
                                        std::uint32_t scan_epoch) noexcept
{
    if (!enabled_) {
        return result(fault_ == PanelSamplerFault::None ? PanelSamplerState::Idle :
            PanelSamplerState::Fault, now_ms);
    }

    if (fault_ == PanelSamplerFault::None) {
        if (observed_time_ && static_cast<std::uint32_t>(now_ms - last_update_) >=
                                  counter_contract::U32HalfRange) {
            fail(PanelSamplerFault::ClockDiscontinuity);
        }
        observed_time_ = true;
        last_update_ = now_ms;

        if (fault_ == PanelSamplerFault::None && phase_ != Phase::Idle) {
            if (scan_epoch != epoch_) {
                fail(PanelSamplerFault::EpochChanged);
            } else if (static_cast<std::uint32_t>(now_ms - scan_started_) >=
                       config_.maximum_scan_age_ms) {
                fail(PanelSamplerFault::ScanTooOld);
            } else if (phase_ == Phase::Poll &&
                       static_cast<std::uint32_t>(now_ms - conversion_started_) >=
                       config_.conversion_timeout_ms) {
                fail(PanelSamplerFault::ConversionTimeout);
            }
        }
    }

    if (fault_ != PanelSamplerFault::None) {
        if (phase_ == Phase::FaultAbort) {
            port_.abort(port_.context);
            needs_abort_ = false;
            phase_ = Phase::FaultGpio;
            return result(PanelSamplerState::Fault, now_ms);
        }
        auto output = result(PanelSamplerState::Fault, now_ms);
        output.observation.gpio_b_idr = port_.read_gpio_b(port_.context);
        output.observation_available = true;
        return output;
    }

    if (phase_ == Phase::Idle) {
        if (started_scan_ && static_cast<std::uint32_t>(now_ms - scan_started_) <
                            config_.scan_interval_ms) {
            return result(PanelSamplerState::Idle, now_ms);
        }
        discard_channels();
        epoch_ = scan_epoch;
        scan_started_ = now_ms;
        started_scan_ = true;
        phase_ = Phase::StartNext;
    }

    if (phase_ == Phase::StartNext) {
        needs_abort_ = true; // A failed start may still have partially armed hardware.
        conversion_started_ = now_ms;
        if (!port_.start(port_.context, channel_)) {
            fail(PanelSamplerFault::StartFailed);
            return result(PanelSamplerState::Fault, now_ms);
        }
        phase_ = Phase::Poll;
        return result(PanelSamplerState::Busy, now_ms);
    }

    if (phase_ == Phase::Poll) {
        const auto polled = port_.poll(port_.context);
        if (polled.state == PanelPollState::Pending) {
            return result(PanelSamplerState::Busy, now_ms);
        }
        if (polled.state != PanelPollState::Ready) {
            fail(PanelSamplerFault::PollFailed);
            return result(PanelSamplerState::Fault, now_ms);
        }
        needs_abort_ = false;
        if (polled.value > AdcMax) {
            fail(PanelSamplerFault::InvalidSample);
            return result(PanelSamplerState::Fault, now_ms);
        }
        channels_[channel_ - 1U] = polled.value;
        if (channel_ < 3U) {
            ++channel_;
            phase_ = Phase::StartNext;
        } else {
            phase_ = Phase::ReadGpio;
        }
        return result(PanelSamplerState::Busy, now_ms);
    }

    auto output = result(PanelSamplerState::Ready, now_ms);
    output.observation = PanelObservation{channels_[0U], channels_[1U], channels_[2U],
        port_.read_gpio_b(port_.context), now_ms, true};
    output.observation_available = true;
    phase_ = Phase::Idle;
    discard_channels();
    return output;
}

} // namespace dso
