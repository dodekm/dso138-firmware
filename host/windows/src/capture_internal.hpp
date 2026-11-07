#pragma once

#include "dso_adc_contract.hpp"
#include "dso_capture_contract.hpp"

#include <QString>
#include <QtGlobal>
#include <cstdint>
#include <utility>

namespace dso::capture_detail
{

// Shared bounds keep in-memory validation and file parsing in agreement.
inline constexpr std::uint16_t maxCode = adc_contract::MaximumCode;
inline constexpr std::uint16_t maxSamples = capture_contract::MaximumSamples;
inline constexpr qint64 maxFileBytes = 32768;
inline constexpr auto format = "DSO138 capture";

inline bool errorResult(QString *error, QString reason)
{
    if (error)
    {
        *error = std::move(reason);
    }
    return false;
}

} // namespace dso::capture_detail
