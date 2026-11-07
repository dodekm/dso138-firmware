#include "demo_capture.hpp"

#include <cmath>
#include <numbers>

namespace dso
{

Capture demoCapture()
{
    Capture capture;
    auto &m = capture.metadata;
    m.token = 1;
    m.count = 512;
    m.pretrigger = 128;
    m.timerHz = 72000000;
    m.divider = 720;
    m.acquisitionEpoch = 1;
    m.triggerSequence = 128;
    m.physicalGeneration = 1;
    m.microvoltsPerDivision = 1000000;
    m.zeroCodeQ16 = 2048 * 65536;
    m.gainQ16 = 65536000;
    m.calibrationKind = 1;
    m.coupling = 1;
    m.flags = 8;
    capture.raw.reserve(m.count);
    for (std::size_t i = 0; i < m.count; ++i)
    {
        const double radians = 2.0 * std::numbers::pi * 5 * static_cast<double>(i) / m.count;
        capture.raw.push_back(
            static_cast<std::uint16_t>(std::lround(2048 + 900 * std::sin(radians))));
    }
    return capture;
}

} // namespace dso
