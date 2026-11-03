#ifndef DSO_USB_PROTOCOL_HPP
#define DSO_USB_PROTOCOL_HPP

#include <cstdint>

namespace dso {

inline constexpr std::uint8_t UsbProtocolVersion{1U};
inline constexpr std::uint8_t UsbProtocolMaximumFrame{64U};
inline constexpr std::uint8_t UsbProtocolMaximumPayload{52U};
inline constexpr std::uint8_t UsbProtocolMaximumSamplesPerRead{22U};

// The USB backend is a transport only. session() is 0 when detached and must
// change to a fresh nonzero value on every configured connection. read() is
// nonblocking and returns at most capacity bytes. write() copies <=64 bytes
// synchronously before returning true; false means busy and has no side effect.
struct UsbStreamPort final {
    void* context{};
    std::uint32_t (*session)(void*) noexcept{};
    std::uint16_t (*read)(void*, std::uint8_t*, std::uint16_t) noexcept{};
    bool (*write)(void*, const std::uint8_t*, std::uint16_t) noexcept{};
};

enum class UsbOpcode : std::uint8_t {
    Info = 1U,
    Status = 2U,
    BeginFrame = 3U,
    ReadSamples = 4U,
    EndFrame = 5U,
    CaptureSingle = 6U,
    CancelCapture = 7U
};

enum class UsbStatus : std::uint8_t {
    Ok = 0U, Invalid = 1U, Busy = 2U, Stale = 3U,
    Unsupported = 4U, Timeout = 5U, Cancelled = 6U, Fault = 7U
};

[[nodiscard]] std::uint32_t usb_protocol_crc_byte(std::uint32_t crc,
                                                  std::uint8_t byte) noexcept;
[[nodiscard]] std::uint32_t usb_protocol_crc(const std::uint8_t* header8,
                                             const std::uint8_t* payload,
                                             std::uint16_t payload_size) noexcept;

} // namespace dso
#endif
