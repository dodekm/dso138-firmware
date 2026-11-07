#ifndef DSO_USB_PROTOCOL_HPP
#define DSO_USB_PROTOCOL_HPP

#include "dso_instrument_codes.hpp"
#include "dso_usb_protocol_contract.hpp"
// UsbStreamPort was originally declared here; retain that source interface.
#include "dso_usb_transport.hpp"

#include <cstdint>

namespace dso {

inline constexpr std::uint8_t UsbProtocolVersion{usb_protocol_contract::Version};
inline constexpr std::uint8_t UsbProtocolMaximumFrame{usb_protocol_contract::MaximumFrameBytes};
inline constexpr std::uint8_t UsbProtocolMaximumPayload{usb_protocol_contract::MaximumPayloadBytes};
inline constexpr std::uint8_t UsbProtocolMaximumSamplesPerRead{usb_protocol_contract::MaximumSamplesPerRead};

// Keep application-facing names for the shared outer-frame and payload
// contracts; command validation and state transitions remain with handlers.
namespace usb_wire {
inline constexpr std::uint8_t MagicFirstOffset{usb_protocol_contract::MagicFirstOffset};
inline constexpr std::uint8_t MagicSecondOffset{usb_protocol_contract::MagicSecondOffset};
inline constexpr std::uint8_t VersionOffset{usb_protocol_contract::VersionOffset};
inline constexpr std::uint8_t OpcodeOffset{usb_protocol_contract::OpcodeOffset};
inline constexpr std::uint8_t RequestIdOffset{usb_protocol_contract::RequestIdOffset};
inline constexpr std::uint8_t PayloadSizeOffset{usb_protocol_contract::PayloadSizeOffset};
inline constexpr std::uint8_t CrcOffset{usb_protocol_contract::CrcOffset};
inline constexpr std::uint8_t CrcHeaderBytes{usb_protocol_contract::CrcHeaderBytes};
inline constexpr std::uint8_t HeaderBytes{usb_protocol_contract::HeaderBytes};
inline constexpr std::uint8_t MagicBytes{usb_protocol_contract::MagicBytes};
inline constexpr std::uint8_t PayloadOffset{usb_protocol_contract::PayloadOffset};
inline constexpr std::uint8_t StatusBytes{usb_protocol_contract::StatusBytes};
inline constexpr std::uint8_t ResponseBodyOffset{usb_protocol_contract::ResponseBodyOffset};
inline constexpr std::uint8_t MaximumResponseBody{usb_protocol_contract::MaximumResponseBody};
inline constexpr std::uint8_t MagicFirst{usb_protocol_contract::MagicFirst};
inline constexpr std::uint8_t MagicSecond{usb_protocol_contract::MagicSecond};
} // namespace usb_wire

static_assert(usb_wire::HeaderBytes + UsbProtocolMaximumPayload == UsbProtocolMaximumFrame,
              "Keep the established USB maximum frame size");
static_assert(usb_wire::CrcOffset + 4U == usb_wire::HeaderBytes &&
              usb_wire::CrcHeaderBytes == usb_wire::CrcOffset &&
              usb_wire::MagicFirstOffset == 0U &&
              usb_wire::MagicSecondOffset + 1U == usb_wire::MagicBytes &&
              usb_wire::PayloadOffset == usb_wire::HeaderBytes &&
              usb_wire::ResponseBodyOffset == usb_wire::PayloadOffset + usb_wire::StatusBytes &&
              usb_wire::MaximumResponseBody + usb_wire::StatusBytes == UsbProtocolMaximumPayload,
              "Keep the established USB header and response status layout");

inline constexpr std::uint8_t UsbCaptureFlagEdgeTriggered{
    instrument_code_contract::capture_flags::EdgeTriggered};
inline constexpr std::uint8_t UsbCaptureFlagReservedMask{
    instrument_code_contract::capture_flags::ReservedMask};

using UsbOpcode = usb_protocol_contract::Opcode;
using UsbStatus = usb_protocol_contract::Status;

[[nodiscard]] std::uint32_t usb_protocol_crc_byte(std::uint32_t crc,
                                                  std::uint8_t byte) noexcept;
[[nodiscard]] std::uint32_t usb_protocol_crc(const std::uint8_t* header8,
                                             const std::uint8_t* payload,
                                             std::uint16_t payload_size) noexcept;

} // namespace dso
#endif
