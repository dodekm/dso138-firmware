#ifndef DSO_SCREEN_SCENE_HPP
#define DSO_SCREEN_SCENE_HPP

#include <cstddef>
#include <cstdint>

namespace dso {

struct WaveformProjection;

enum class ScreenHighlight : std::uint8_t { None, Timebase, Trigger, Position };
// This order is shared by the snapshot, dirty-field mask and text-range array.
enum class ScreenTextField : std::uint8_t {
    Status, Quality, Channel, Timebase, Trigger, Sampling, Record, Focus, Hint, Count
};
inline constexpr std::size_t ScreenTextFieldCount{
    static_cast<std::size_t>(ScreenTextField::Count)};
constexpr std::size_t screen_text_index(ScreenTextField field) noexcept
{
    return static_cast<std::size_t>(field);
}
constexpr std::uint16_t screen_text_mask(ScreenTextField field) noexcept
{
    const auto index = screen_text_index(field);
    return index < ScreenTextFieldCount ? static_cast<std::uint16_t>(1U << index) : 0U;
}
// Conservative whole label rectangles erase shortened text.
inline constexpr std::uint16_t ScreenTextAll{
    static_cast<std::uint16_t>((1U << ScreenTextFieldCount) - 1U)};
static_assert(screen_text_mask(ScreenTextField::Status) == 0x0001U &&
              screen_text_mask(ScreenTextField::Quality) == 0x0002U &&
              screen_text_mask(ScreenTextField::Channel) == 0x0004U &&
              screen_text_mask(ScreenTextField::Timebase) == 0x0008U &&
              screen_text_mask(ScreenTextField::Trigger) == 0x0010U &&
              screen_text_mask(ScreenTextField::Sampling) == 0x0020U &&
              screen_text_mask(ScreenTextField::Record) == 0x0040U &&
              screen_text_mask(ScreenTextField::Focus) == 0x0080U &&
              screen_text_mask(ScreenTextField::Hint) == 0x0100U &&
              ScreenTextAll == 0x01ffU, "Preserve the established UI dirty-field mask");

// Half-open character cells accumulated while the foreground composes labels.
// An empty range with a dirty field means repaint its complete public band.
struct ScreenTextRange final {
    std::uint8_t first{}, end{};
    void include(std::uint16_t begin, std::uint16_t finish) noexcept
    {
        // Every label has at most 51 visible characters; bound malformed callers
        // before narrowing, so the cell-to-pixel conversion cannot wrap.
        if (finish > 63U) { finish = 63U; }
        if (begin >= finish) { return; }
        if (end == 0U || begin < first) { first = static_cast<std::uint8_t>(begin); }
        if (finish > end) { end = static_cast<std::uint8_t>(finish); }
    }
};
static_assert(sizeof(ScreenTextRange) == 2U, "Keep exact text changes bounded");

// Passive presentation snapshot. Labels and markers are copied by ScopeScreen
// when a render job begins; waveform storage is borrowed to avoid duplicating
// the projection. epoch is a screen revision, not the acquisition epoch: a held
// frame can be redrawn with new axes. Keep the projection alive and immutable
// until the render completes, is cancelled, or faults; cancel before modifying
// or releasing it. This value does not imply hardware identity.
struct ScreenSnapshot final {
    const WaveformProjection* waveform{};
    std::uint32_t epoch{};
    char status[12]{};
    char quality[5]{}; // RAW/NOM/CAL; never silently supplied by the renderer.
    char channel[28]{};
    char timebase[22]{};
    char trigger[31]{};
    char sampling[20]{};
    char record[52]{};
    char focus[52]{};
    char hint[52]{};
    char zero_label[4]{}; // Explicit REF for raw codes or 0V for voltage axes.
    std::int16_t zero_y{-1}; // Plot-relative coordinate; -1 hides the marker.
    std::int16_t trigger_y{-1};
    std::int16_t trigger_x{-1};
    ScreenHighlight highlight{ScreenHighlight::None};
    bool warning{};
};

} // namespace dso

#endif // DSO_SCREEN_SCENE_HPP
