#include "dso_usb_protocol.hpp"

namespace dso {

std::uint32_t usb_protocol_crc_byte(std::uint32_t crc, std::uint8_t byte) noexcept
{
    crc ^= byte;
    for (unsigned bit{}; bit < 8U; ++bit) {
        crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xedb88320U : 0U);
    }
    return crc;
}

std::uint32_t usb_protocol_crc(const std::uint8_t* header8,
                              const std::uint8_t* payload,
                              std::uint16_t payload_size) noexcept
{
    if (header8 == nullptr || (payload_size != 0U && payload == nullptr)) { return 0U; }
    std::uint32_t crc{0xffffffffU};
    for (std::uint8_t i{}; i < 8U; ++i) { crc = usb_protocol_crc_byte(crc, header8[i]); }
    for (std::uint16_t i{}; i < payload_size; ++i) { crc = usb_protocol_crc_byte(crc, payload[i]); }
    return crc ^ 0xffffffffU;
}

} // namespace dso
