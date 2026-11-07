#ifndef DSO_RENDER_TYPES_HPP
#define DSO_RENDER_TYPES_HPP

#include <cstdint>

namespace dso {

enum class RenderState : std::uint8_t { Idle, Drawing, Complete, Cancelled, Fault };
struct RenderBudget final {
    std::uint16_t pixels;
    std::uint16_t transactions;
};
struct RenderStep final {
    RenderState state;
    std::uint16_t pixels;       // Requested pixel work; a fault may write only part.
    std::uint16_t transactions; // Attempted transactions, including a failed one.
    bool completed{};           // One completion event, including zero-pixel polls.
};

} // namespace dso

#endif // DSO_RENDER_TYPES_HPP
