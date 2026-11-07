#ifndef DSO_TIMEBASE_CONTRACT_HPP
#define DSO_TIMEBASE_CONTRACT_HPP

#include <array>
#include <cstdint>

namespace dso::timebase_contract {

// One ordered list drives the classic scope's firmware and Studio timebase UI.
inline constexpr std::uint8_t ClassicCatalogueId{1U};
inline constexpr std::array<std::uint32_t, 18U> ClassicMicrosecondsPerDivision{
    10U, 20U, 50U, 100U, 200U, 500U, 1000U, 2000U, 5000U,
    10000U, 20000U, 50000U, 100000U, 200000U, 500000U,
    1000000U, 2000000U, 5000000U};
inline constexpr std::uint8_t ClassicTimebaseCatalogueEntries{
    static_cast<std::uint8_t>(ClassicMicrosecondsPerDivision.size())};

} // namespace dso::timebase_contract

#endif // DSO_TIMEBASE_CONTRACT_HPP
