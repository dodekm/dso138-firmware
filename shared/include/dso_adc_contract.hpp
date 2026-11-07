#ifndef DSO_ADC_CONTRACT_HPP
#define DSO_ADC_CONTRACT_HPP

#include <cstdint>

namespace dso::adc_contract {

// ADC codes are serialized in captures and shared by firmware and Studio.
inline constexpr std::uint8_t ResolutionBits{12U};
inline constexpr std::uint16_t MaximumCode{
    static_cast<std::uint16_t>((1U << ResolutionBits) - 1U)};

static_assert(ResolutionBits == 12U && MaximumCode == 4095U,
              "The DSO138 capture contract uses unsigned 12-bit ADC codes");

} // namespace dso::adc_contract

#endif // DSO_ADC_CONTRACT_HPP
