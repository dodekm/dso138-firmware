#ifndef DSO_DISPLAY_HPP
#define DSO_DISPLAY_HPP

#include <cstdint>

namespace dso {

inline constexpr std::uint16_t DisplayWidth{320U};
inline constexpr std::uint16_t DisplayHeight{240U};
inline constexpr std::uint16_t DisplayMaxSamples{1024U};
inline constexpr std::uint16_t DisplayBurstPixels{64U};
#if defined(DSO_DISPLAY_DMA_PIXELS)
inline constexpr std::uint16_t DisplayDmaPixels{DSO_DISPLAY_DMA_PIXELS};
#else
inline constexpr std::uint16_t DisplayDmaPixels{16U};
#endif
static_assert(DisplayDmaPixels > 0U && DisplayDmaPixels <= DisplayBurstPixels,
              "DMA chunks must fit the bounded 64-pixel display burst");

enum class ControllerId : std::uint16_t { Unknown = 0U, Ili9341 = 0x9341U };
enum class ColorOrder : std::uint8_t { Rgb, Bgr };
enum class DisplayOrientation : std::uint8_t { Landscape, Landscape180 };
enum class DisplayStatus : std::uint8_t {
    Uninitialized, Ready, UnsupportedController, InvalidArgument, BusFault
};
enum class SpanDirection : std::uint8_t { Horizontal, Vertical };
enum class TransferState : std::uint8_t { Idle, Pending, Complete, Fault };

// Board adapter owns GPIO timing, masks and bus exclusion.
// Logical select/reset true means asserted; write(data=true) selects D/CX data.
// Callbacks must be bounded and return false on error. Context outlives display.
// No callback or GPIO backend is enabled automatically by this portable driver.
struct DisplayBus final {
    void* context;
    bool (*select)(void*, bool asserted) noexcept;
    bool (*reset)(void*, bool asserted) noexcept;
    bool (*write)(void*, bool data, std::uint8_t value) noexcept;
    bool (*delay_ms)(void*, std::uint32_t duration) noexcept;
    // Optional, selected-bus RGB565 stream, MSB first. Source remains readable
    // for the synchronous call: count words, or one word when repeat=true.
    // Each call is bounded to DisplayBurstPixels; nullptr preserves byte mode.
    // No callback may reenter or change peripheral ownership during the burst.
    bool (*pixels)(void*, const std::uint16_t* source, std::uint16_t count,
                   bool repeat) noexcept{};
    // Optional asynchronous transport; supply all three or none. start copies
    // <=DisplayDmaPixels RGB565 words before returning: no DMA source borrow.
    // poll Complete certifies final data hold/WR high; abort synchronously ends
    // all reads/strobes. No other bus operation may overlap Pending.
    bool (*start_pixels)(void*, const std::uint16_t* source, std::uint16_t count) noexcept{};
    TransferState (*poll_pixels)(void*) noexcept{};
    void (*abort_pixels)(void*) noexcept{};
    // Optional synchronous selected-bus sequence of 1..11 bytes. Bit i of
    // data_bits selects D/CX data for bytes[i]; bits outside count are invalid.
    // Source stays readable/immutable for this call only. No callback may
    // reenter or change GPIO/clock ownership. Boundary guards need not detect
    // transient configuration changes at every byte. Null retains byte mode.
    bool (*write_sequence)(void*, const std::uint8_t* bytes, std::uint16_t count,
                           std::uint16_t data_bits) noexcept{};
};

struct Rect final {
    std::int32_t x;
    std::int32_t y;
    std::int32_t width;
    std::int32_t height;
};
struct ClippedRect final {
    std::uint16_t x;
    std::uint16_t y;
    std::uint16_t width;
    std::uint16_t height;
};

// Uses wide endpoint arithmetic. Empty/nonpositive/offscreen rectangles return
// false without changing out. Width/height are extents, not inclusive endpoints.
[[nodiscard]] bool clip_rect(Rect input, ClippedRect& out) noexcept;
// The portable driver accepts only an independently identified ILI9341.

class Ili9341Display final {
public:
    explicit Ili9341Display(DisplayBus bus) noexcept : bus_{bus} {}
    Ili9341Display(const Ili9341Display&) = delete;
    Ili9341Display& operator=(const Ili9341Display&) = delete;

    // Explicit board identification is required. Unknown/legacy IDs cause no
    // bus traffic. Blocking reset/sleep delays belong before live acquisition.
    // Minimal IC-default initialization is not panel power/gamma calibration.
    [[nodiscard]] bool initialize(ControllerId verified_id, ColorOrder order,
                                  DisplayOrientation orientation = DisplayOrientation::Landscape) noexcept;
    [[nodiscard]] DisplayStatus status() const noexcept { return status_; }
    [[nodiscard]] bool ready() const noexcept { return status_ == DisplayStatus::Ready; }

    // Strict row/column bounds, nonzero count; never crosses the selected span.
    // One transaction has 11 command/parameter bytes plus 2 bytes/pixel and two
    // select calls (one extra deassert attempt can occur on a bus fault).
    // RGB565 bytes are sent most-significant byte first.
    // Failure latches BusFault; explicit initialize is required before retry.
    [[nodiscard]] bool write_span(std::uint16_t x, std::uint16_t y,
                                  const std::uint16_t* pixels,
                                  std::uint16_t count) noexcept;
    [[nodiscard]] bool fill_span(std::uint16_t x, std::uint16_t y,
                                 std::uint16_t count, std::uint16_t color,
                                 SpanDirection direction = SpanDirection::Horizontal) noexcept;
    // Selected row window and source remain owned until Complete/Fault/abort.
    // All <=64 source pixels must stay immutable/alive across chained chunks.
    // A synchronous-only bus completes in begin_span; poll remains idempotent.
    [[nodiscard]] bool begin_span(std::uint16_t x, std::uint16_t y,
                                  const std::uint16_t* pixels, std::uint16_t count) noexcept;
    // Selected row-major rectangle window, bounded to the same 64-word source
    // borrow. Width*height must equal count; source stays immutable through
    // Complete/Fault/abort. GRAM advances across rows inside the window.
    [[nodiscard]] bool begin_rect(std::uint16_t x, std::uint16_t y,
                                  std::uint16_t width, std::uint16_t height,
                                  const std::uint16_t* pixels, std::uint16_t count) noexcept;
    [[nodiscard]] TransferState poll_span() noexcept;
    void abort_span() noexcept;
    [[nodiscard]] TransferState transfer_state() const noexcept { return transfer_; }

private:
    [[nodiscard]] bool fail_bus() noexcept;
    [[nodiscard]] bool command(std::uint8_t value, const std::uint8_t* parameters,
                                std::uint16_t count) noexcept;
    [[nodiscard]] bool window(std::uint16_t x0, std::uint16_t y0,
                               std::uint16_t x1, std::uint16_t y1) noexcept;
    [[nodiscard]] bool put(bool data, std::uint8_t value) noexcept;
    [[nodiscard]] bool pixels(const std::uint16_t* source, std::uint16_t count,
                               bool repeat) noexcept;
    [[nodiscard]] bool finish() noexcept;
    [[nodiscard]] bool start_chunk() noexcept;
    [[nodiscard]] bool begin_window_stream(std::uint16_t x0, std::uint16_t y0,
        std::uint16_t x1, std::uint16_t y1, const std::uint16_t* source,
        std::uint16_t count) noexcept;
    DisplayBus bus_;
    const std::uint16_t* pending_source_{};
    std::uint16_t pending_count_{};
    DisplayStatus status_{DisplayStatus::Uninitialized};
    TransferState transfer_{TransferState::Idle};
};

enum class RenderState : std::uint8_t { Idle, Drawing, Complete, Cancelled, Fault };
struct RenderBudget final {
    std::uint16_t pixels;
    std::uint16_t transactions;
};
struct RenderStep final {
    RenderState state;
    std::uint16_t pixels;       // Requested pixel work; a fault may write only part.
    std::uint16_t transactions; // Attempted transactions, including a failed one.
    bool completed{};           // One completion event, including zero-pixel polls.
};
} // namespace dso
#endif
