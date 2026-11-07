#ifndef DSO_USB_TRANSPORT_HPP
#define DSO_USB_TRANSPORT_HPP

#include <cstdint>

namespace dso {

// Nonblocking byte stream supplied by the CDC or test adapter. session() is
// zero while detached and changes to a fresh nonzero value per connection.
// read() is nonblocking and returns at most capacity bytes. write() copies at
// most 64 bytes before returning true; false means busy and has no side effect.
struct UsbStreamPort final {
    void* context{};
    std::uint32_t (*session)(void*) noexcept{};
    std::uint16_t (*read)(void*, std::uint8_t*, std::uint16_t) noexcept{};
    bool (*write)(void*, const std::uint8_t*, std::uint16_t) noexcept{};
};

} // namespace dso

#endif // DSO_USB_TRANSPORT_HPP
