#ifndef DSO_TRIGGER_HPP
#define DSO_TRIGGER_HPP

#include <cstdint>

namespace dso {

enum class TriggerEdge { Rising, Falling };

// Foreground sample-domain Schmitt rearming shared by both acquisition paths.
// A crossing consumes arming even if its caller lacks enough pretrigger history.
// Callers validate ADC codes, level/hysteresis and edge before this operation.
class ThresholdCrossing final {
public:
    void reset() noexcept { armed_ = false; }
    [[nodiscard]] bool update(std::uint16_t sample, std::uint16_t level,
                              std::uint16_t hysteresis, TriggerEdge edge) noexcept
    {
        bool hit{};
        if (edge == TriggerEdge::Rising) {
            hit = armed_ && sample >= level;
            if (sample < level && sample <= level - hysteresis) { armed_ = true; }
        } else {
            hit = armed_ && sample <= level;
            if (sample > level && sample >= level + hysteresis) { armed_ = true; }
        }
        if (hit) { armed_ = false; }
        return hit;
    }
private:
    bool armed_{};
};

} // namespace dso
#endif
