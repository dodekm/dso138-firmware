#include "dso_usb_protocol.hpp"
#include "dso_crc32.hpp"

namespace dso {

std::uint32_t usb_protocol_crc_byte(std::uint32_t crc, std::uint8_t byte) noexcept
{ return checksum::crc32_ieee_update_byte(crc, byte); }

std::uint32_t usb_protocol_crc(const std::uint8_t* header8,
                              const std::uint8_t* payload,
                              std::uint16_t payload_size) noexcept
{
    if (header8 == nullptr || (payload_size != 0U && payload == nullptr)) { return 0U; }
    auto crc = checksum::Crc32IeeeInitial;
    for (std::uint8_t i{}; i < usb_wire::CrcHeaderBytes; ++i) {
        crc = usb_protocol_crc_byte(crc, header8[i]);
    }
    for (std::uint16_t i{}; i < payload_size; ++i) { crc = usb_protocol_crc_byte(crc, payload[i]); }
    return checksum::crc32_ieee_finalize(crc);
}

} // namespace dso
