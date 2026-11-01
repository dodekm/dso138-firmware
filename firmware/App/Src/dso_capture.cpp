#include "dso_capture.hpp"
#include <cstdint>

namespace dso {

static bool config_valid(const CaptureConfig *cfg) noexcept
{
    return cfg != nullptr && cfg->length > 0U &&
           cfg->length <= CaptureCapacity &&
           cfg->pretrigger < cfg->length && cfg->level <= AdcMax &&
           cfg->hysteresis <= cfg->level &&
           cfg->hysteresis <= AdcMax - cfg->level &&
           (cfg->mode == TriggerMode::Normal || cfg->mode == TriggerMode::Auto ||
            cfg->mode == TriggerMode::Single) &&
           (cfg->edge == TriggerEdge::Rising || cfg->edge == TriggerEdge::Falling) &&
           (cfg->mode != TriggerMode::Auto ||
            cfg->auto_timeout_samples > cfg->pretrigger);
}

bool Capture::arm(const CaptureConfig& new_config, uint32_t first_sequence) noexcept
{
    if (!config_valid(&new_config)) {
        return false;
    }
    /* Copy first: callers may legitimately pass &config_. */
    const CaptureConfig saved = new_config;
    config_ = saved;
    state_ = CaptureState::Armed;
    fault_ = CaptureFault::None;
    next_sequence_ = first_sequence;
    trigger_sequence_ = 0U;
    waited_ = 0U;
    write_index_ = 0U;
    history_ = 0U;
    remaining_ = 0U;
    crossing_.reset();
    edge_triggered_ = false;
    return true;
}

void Capture::stop() noexcept
{
    state_ = CaptureState::Stopped;
}

bool Capture::active() const noexcept
{
    return state_ == CaptureState::Armed ||
           state_ == CaptureState::Posttrigger;
}

void Capture::gap() noexcept
{
    if (active()) {
        fault_ = CaptureFault::SequenceGap;
        state_ = CaptureState::Fault;
    }
}

size_t Capture::push(const uint16_t *samples, size_t count,
                     uint32_t first_sequence) noexcept
{
    if (count == 0U || !active()) {
        return 0U;
    }
    if (samples == nullptr || first_sequence != next_sequence_) {
        gap();
        return 0U;
    }

    // A push has one foreground owner and no callbacks. Keep hot control
    // state local across the immutable, non-aliasing snapshot, then publish
    // it before returning. This avoids per-sample member loads/stores without
    // changing sample chronology or allocating another sample buffer.
    const auto config = config_;
    auto state = state_;
    auto sequence = next_sequence_;
    auto waited = waited_;
    auto write_index = write_index_;
    auto history = history_;
    auto remaining = remaining_;
    auto crossing = crossing_;
    size_t used = 0U;
    while (used < count && (state == CaptureState::Armed || state == CaptureState::Posttrigger)) {
        const uint16_t sample = samples[used];
        if (sample > AdcMax) {
            fault_ = CaptureFault::InvalidSample;
            state = CaptureState::Fault;
            break;
        }
        samples_[write_index] = sample;
        write_index++;
        if (write_index == config.length) {
            write_index = 0U;
        }

        if (state == CaptureState::Posttrigger) {
            remaining--;
            if (remaining == 0U) {
                state = CaptureState::Ready;
            }
        } else {
            const bool hit = crossing.update(sample, config.level, config.hysteresis, config.edge);
            if (waited < UINT32_MAX) {
                waited++;
            }
            const bool timeout = config.mode == TriggerMode::Auto &&
                waited >= config.auto_timeout_samples;
            if (history >= config.pretrigger && (hit || timeout)) {
                trigger_sequence_ = sequence;
                edge_triggered_ = hit;
                remaining = static_cast<uint16_t>(config.length - config.pretrigger - 1U);
                state = remaining == 0U ?
                    CaptureState::Ready : CaptureState::Posttrigger;
            }
            if (history < config.pretrigger) {
                history++;
            }
        }
        sequence++; /* Defined unsigned wrap. */
        used++;
    }
    next_sequence_ = sequence;
    waited_ = waited;
    write_index_ = write_index;
    history_ = history;
    remaining_ = remaining;
    crossing_ = crossing;
    state_ = state;
    return used;
}

bool Capture::read(uint16_t index, uint16_t *sample) const noexcept
{
    if (sample == nullptr || state_ != CaptureState::Ready ||
        index >= config_.length) {
        return false;
    }
    uint16_t position = static_cast<uint16_t>(write_index_ + index);
    if (position >= config_.length) {
        position = static_cast<uint16_t>(position - config_.length);
    }
    *sample = samples_[position];
    return true;
}

} // namespace dso
