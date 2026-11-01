#include "dso_display.hpp"

#include <cstddef>

namespace dso {
namespace {
constexpr std::uint16_t smaller(std::uint16_t first, std::uint16_t second) noexcept
{
    return first < second ? first : second;
}
}

bool clip_rect(Rect input, ClippedRect& out) noexcept
{
    if (input.width <= 0 || input.height <= 0) {
        return false;
    }
    const auto right = static_cast<std::int64_t>(input.x) + input.width;
    const auto bottom = static_cast<std::int64_t>(input.y) + input.height;
    const auto left = input.x < 0 ? std::int64_t{0} : input.x;
    const auto top = input.y < 0 ? std::int64_t{0} : input.y;
    const auto clipped_right = right > DisplayWidth ? DisplayWidth : right;
    const auto clipped_bottom = bottom > DisplayHeight ? DisplayHeight : bottom;
    if (left >= clipped_right || top >= clipped_bottom) {
        return false;
    }
    out = ClippedRect{static_cast<std::uint16_t>(left), static_cast<std::uint16_t>(top),
                      static_cast<std::uint16_t>(clipped_right - left),
                      static_cast<std::uint16_t>(clipped_bottom - top)};
    return true;
}

bool Ili9341Display::fail_bus() noexcept
{
    if (transfer_ == TransferState::Pending && bus_.abort_pixels != nullptr) { bus_.abort_pixels(bus_.context); }
    pending_source_ = nullptr; pending_count_ = 0U; transfer_ = TransferState::Fault;
    status_ = DisplayStatus::BusFault;
    if (bus_.select != nullptr) {
        static_cast<void>(bus_.select(bus_.context, false));
    }
    return false;
}

bool Ili9341Display::put(bool data, std::uint8_t value) noexcept
{
    return bus_.write(bus_.context, data, value) || fail_bus();
}

bool Ili9341Display::finish() noexcept
{
    return bus_.select(bus_.context, false) || fail_bus();
}

bool Ili9341Display::command(std::uint8_t value, const std::uint8_t* parameters,
                             std::uint16_t count) noexcept
{
    if (!bus_.select(bus_.context, true)) {
        return fail_bus();
    }
    if (!put(false, value)) {
        return false;
    }
    for (std::uint16_t index{0U}; index < count; ++index) {
        if (!put(true, parameters[index])) {
            return false;
        }
    }
    return finish();
}

bool Ili9341Display::initialize(ControllerId verified_id, ColorOrder order,
                               DisplayOrientation orientation) noexcept
{
    if (transfer_ == TransferState::Pending) { return false; }
    if (verified_id != ControllerId::Ili9341) {
        status_ = DisplayStatus::UnsupportedController;
        return false;
    }
    if (bus_.select == nullptr || bus_.reset == nullptr || bus_.write == nullptr ||
        bus_.delay_ms == nullptr || (order != ColorOrder::Rgb && order != ColorOrder::Bgr) ||
        (orientation != DisplayOrientation::Landscape && orientation != DisplayOrientation::Landscape180) ||
        ((bus_.start_pixels != nullptr || bus_.poll_pixels != nullptr || bus_.abort_pixels != nullptr) &&
         (bus_.start_pixels == nullptr || bus_.poll_pixels == nullptr || bus_.abort_pixels == nullptr))) {
        status_ = DisplayStatus::InvalidArgument;
        return false;
    }
    status_ = DisplayStatus::Uninitialized;
    transfer_ = TransferState::Idle;
    // Conservative reset/sleep waits, independently selected from documented
    // requirements; no panel-specific power/gamma register values are assumed.
    if (!bus_.select(bus_.context, false) || !bus_.reset(bus_.context, true) ||
        !bus_.delay_ms(bus_.context, 10U) || !bus_.reset(bus_.context, false) ||
        !bus_.delay_ms(bus_.context, 120U)) {
        return fail_bus();
    }
    if (!command(0x11U, nullptr, 0U) || !bus_.delay_ms(bus_.context, 120U)) {
        return fail_bus();
    }
    constexpr std::uint8_t rgb565{0x55U};
    // ILI9341 MADCTL: retain MV for 320x240 landscape, reverse both address
    // axes for a physical 180-degree rotation; RGB/BGR remains independent.
    const auto axes = orientation == DisplayOrientation::Landscape180 ? 0xe0U : 0x20U;
    const auto madctl = static_cast<std::uint8_t>(axes | (order == ColorOrder::Bgr ? 0x08U : 0U));
    if (!command(0x3aU, &rgb565, 1U) || !command(0x36U, &madctl, 1U) ||
        !command(0x13U, nullptr, 0U) || !command(0x29U, nullptr, 0U)) {
        return false;
    }
    status_ = DisplayStatus::Ready;
    return true;
}

bool Ili9341Display::window(std::uint16_t x0, std::uint16_t y0,
                            std::uint16_t x1, std::uint16_t y1) noexcept
{
    if (!bus_.select(bus_.context, true)) {
        return fail_bus();
    }
    if (bus_.write_sequence != nullptr) {
        const std::uint8_t bytes[]{0x2aU,
            static_cast<std::uint8_t>(x0 >> 8U),static_cast<std::uint8_t>(x0),
            static_cast<std::uint8_t>(x1 >> 8U),static_cast<std::uint8_t>(x1),0x2bU,
            static_cast<std::uint8_t>(y0 >> 8U),static_cast<std::uint8_t>(y0),
            static_cast<std::uint8_t>(y1 >> 8U),static_cast<std::uint8_t>(y1),0x2cU};
        return bus_.write_sequence(bus_.context,bytes,11U,0x03deU) || fail_bus();
    }
    const std::uint8_t columns[]{static_cast<std::uint8_t>(x0 >> 8U),
        static_cast<std::uint8_t>(x0), static_cast<std::uint8_t>(x1 >> 8U),
        static_cast<std::uint8_t>(x1)};
    const std::uint8_t rows[]{static_cast<std::uint8_t>(y0 >> 8U),
        static_cast<std::uint8_t>(y0), static_cast<std::uint8_t>(y1 >> 8U),
        static_cast<std::uint8_t>(y1)};
    if (!put(false, 0x2aU)) { return false; }
    for (const auto value : columns) {
        if (!put(true, value)) { return false; }
    }
    if (!put(false, 0x2bU)) { return false; }
    for (const auto value : rows) {
        if (!put(true, value)) { return false; }
    }
    return put(false, 0x2cU);
}

bool Ili9341Display::write_span(std::uint16_t x, std::uint16_t y,
                               const std::uint16_t* pixels, std::uint16_t count) noexcept
{
    if (!ready() || transfer_ == TransferState::Pending || pixels == nullptr || count == 0U || x >= DisplayWidth ||
        y >= DisplayHeight || count > DisplayWidth - x) {
        return false;
    }
    if (!window(x, y, static_cast<std::uint16_t>(x + count - 1U), y)) {
        return false;
    }
    if (!this->pixels(pixels, count, false)) { return false; }
    return finish();
}

bool Ili9341Display::pixels(const std::uint16_t* source, std::uint16_t count,
                            bool repeat) noexcept
{
    if (bus_.pixels != nullptr) {
        while (count != 0U) {
            const auto chunk = smaller(count, DisplayBurstPixels);
            if (!bus_.pixels(bus_.context, source, chunk, repeat)) { return fail_bus(); }
            if (!repeat) { source += chunk; }
            count = static_cast<std::uint16_t>(count - chunk);
        }
        return true;
    }
    for (std::uint16_t index{}; index < count; ++index) {
        const auto value = source[repeat ? 0U : index];
        if (!put(true, static_cast<std::uint8_t>(value >> 8U)) ||
            !put(true, static_cast<std::uint8_t>(value))) { return false; }
    }
    return true;
}

bool Ili9341Display::fill_span(std::uint16_t x, std::uint16_t y,
                              std::uint16_t count, std::uint16_t color,
                              SpanDirection direction) noexcept
{
    if (!ready() || transfer_ == TransferState::Pending || count == 0U || x >= DisplayWidth || y >= DisplayHeight ||
        (direction != SpanDirection::Horizontal && direction != SpanDirection::Vertical)) {
        return false;
    }
    const auto limit = direction == SpanDirection::Horizontal ? DisplayWidth - x : DisplayHeight - y;
    if (count > limit) { return false; }
    const auto x1 = direction == SpanDirection::Horizontal ? static_cast<std::uint16_t>(x + count - 1U) : x;
    const auto y1 = direction == SpanDirection::Vertical ? static_cast<std::uint16_t>(y + count - 1U) : y;
    if (!window(x, y, x1, y1)) { return false; }
    if (!pixels(&color, count, true)) { return false; }
    return finish();
}

bool Ili9341Display::start_chunk() noexcept
{
    const auto count = smaller(pending_count_,DisplayDmaPixels);
    if (!bus_.start_pixels(bus_.context,pending_source_,count)) { return fail_bus(); }
    pending_source_ += count;
    pending_count_ = static_cast<std::uint16_t>(pending_count_-count);
    return true;
}

bool Ili9341Display::begin_window_stream(std::uint16_t x0, std::uint16_t y0,
                                        std::uint16_t x1, std::uint16_t y1,
                                        const std::uint16_t* source,
                                        std::uint16_t count) noexcept
{
    if (bus_.start_pixels == nullptr) {
        if (!window(x0,y0,x1,y1) || !pixels(source,count,false) || !finish()) { return false; }
        transfer_ = TransferState::Complete;
        return true;
    }
    if (!window(x0,y0,x1,y1)) { return false; }
    pending_source_ = source; pending_count_ = count; transfer_ = TransferState::Pending;
    return start_chunk();
}

bool Ili9341Display::begin_span(std::uint16_t x, std::uint16_t y,
                               const std::uint16_t* source, std::uint16_t count) noexcept
{
    return begin_rect(x,y,count,1U,source,count);
}

bool Ili9341Display::begin_rect(std::uint16_t x, std::uint16_t y,
                                std::uint16_t width, std::uint16_t height,
                                const std::uint16_t* source, std::uint16_t count) noexcept
{
    if (!ready() || transfer_ == TransferState::Pending || source == nullptr || width == 0U || height == 0U ||
        count == 0U || count > DisplayBurstPixels || std::uint32_t{width}*height != count ||
        x >= DisplayWidth || y >= DisplayHeight || width > DisplayWidth-x || height > DisplayHeight-y) { return false; }
    return begin_window_stream(x,y,static_cast<std::uint16_t>(x+width-1U),
                               static_cast<std::uint16_t>(y+height-1U),source,count);
}

TransferState Ili9341Display::poll_span() noexcept
{
    if (transfer_ != TransferState::Pending) { return transfer_; }
    const auto state = bus_.poll_pixels(bus_.context);
    if (state == TransferState::Pending) { return transfer_; }
    if (state != TransferState::Complete) { static_cast<void>(fail_bus()); return transfer_; }
    if (pending_count_ != 0U) { static_cast<void>(start_chunk()); return transfer_; }
    pending_source_ = nullptr;
    if (finish()) { transfer_ = TransferState::Complete; }
    return transfer_;
}

void Ili9341Display::abort_span() noexcept
{
    if (transfer_ != TransferState::Pending) { return; }
    bus_.abort_pixels(bus_.context);
    pending_source_ = nullptr; pending_count_ = 0U; transfer_ = TransferState::Idle;
    if (!finish()) { transfer_ = TransferState::Fault; }
}

} // namespace dso
