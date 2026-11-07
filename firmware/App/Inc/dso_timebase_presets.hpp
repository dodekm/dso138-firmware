// Offline-generated timer factors, indexed by the shared classic catalogue.
// dso_scaling.cpp checks their count and the catalogue's 10/20/50 progression.
#ifndef DSO_TIMEBASE_PRESETS_HPP
#define DSO_TIMEBASE_PRESETS_HPP

#include <cstddef>
#include <cstdint>

namespace dso::detail {
struct TimerPair final { std::uint16_t prescaler, period; };
inline constexpr TimerPair ScopePresets[]{
    {0U, 143U},
    {0U, 143U},
    {0U, 143U},
    {0U, 143U},
    {0U, 143U},
    {0U, 351U},
    {0U, 703U},
    {0U, 1406U},
    {0U, 3515U},
    {0U, 7031U},
    {0U, 14062U},
    {0U, 35156U},
    {1U, 35156U},
    {2U, 46874U},
    {5U, 58593U},
    {14U, 46874U},
    {24U, 56249U},
    {74U, 46874U}
};
template<std::size_t N> constexpr std::uint32_t preset_fingerprint(const TimerPair (&pairs)[N]) noexcept
{
    std::uint32_t hash{2166136261U};
    for (const auto pair : pairs) {
        hash = (hash ^ ((std::uint32_t{pair.prescaler} << 16U) | pair.period)) * 16777619U;
    }
    return hash;
}
static_assert(preset_fingerprint(ScopePresets) == 0xee5f02c8U,
              "Review the scope timer factors and update their fingerprint");
} // namespace dso::detail
#endif
