#ifndef DSO_U32_COUNTER_HPP
#define DSO_U32_COUNTER_HPP

#include <cstdint>

namespace dso::counter_contract {

// Half-range bound for unambiguous elapsed-time checks on modulo-2^32 counters.
inline constexpr std::uint32_t U32HalfRange{0x80000000U};

} // namespace dso::counter_contract

#endif // DSO_U32_COUNTER_HPP
