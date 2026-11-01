#ifndef DSO_SCOPE_SCREEN_HPP
#define DSO_SCOPE_SCREEN_HPP

#include "dso_display.hpp"
#include "dso_scaling.hpp"
#include <array>
#include <cstdint>

namespace dso {

inline constexpr std::uint16_t ScopePlotX{28U};
inline constexpr std::uint16_t ScopePlotY{34U};
inline constexpr std::uint16_t ScopePlotWidth{280U};
inline constexpr std::uint16_t ScopePlotHeight{176U};
inline constexpr std::uint16_t ScopeTileWidth{4U};
inline constexpr unsigned ScopeTileCount{(ScopePlotWidth+ScopeTileWidth-1U)/ScopeTileWidth};
static_assert(ScopeTileCount*ScopeTileWidth >= ScopePlotWidth &&
              (ScopeTileCount-1U)*ScopeTileWidth < ScopePlotWidth,
              "Exact vertical tile bounds must cover the complete plot");
inline constexpr std::uint16_t ScopePixelsPerHorizontalDiv{28U};
inline constexpr std::uint16_t ScopePixelsPerVerticalDiv{22U};
static_assert(WaveformColumnCapacity >= ScopePlotWidth,
              "Product waveform capacity must span the full scope plot");

enum class ScreenHighlight : std::uint8_t { None, Timebase, Trigger, Position };
// Bits follow status, quality, channel, timebase, trigger, sampling, record,
// focus, hint. Conservative whole label rectangles erase shortened text.
inline constexpr std::uint16_t ScreenTextAll{0x01ffU};

// Half-open character cells, accumulated while the foreground composes labels.
// An empty range with a dirty field means repaint its complete public band.
struct ScreenTextRange final {
    std::uint8_t first{}, end{};
    void include(std::uint16_t begin, std::uint16_t finish) noexcept
    {
        // Every label has at most 51 visible characters; bound malformed callers
        // before narrowing, so the cell-to-pixel conversion cannot wrap.
        if (finish > 63U) { finish = 63U; }
        if (begin >= finish) { return; }
        if (end == 0U || begin < first) { first = static_cast<std::uint8_t>(begin); }
        if (finish > end) { end = static_cast<std::uint8_t>(finish); }
    }
};
static_assert(sizeof(ScreenTextRange) == 2U, "Keep exact text changes bounded");

// Passive presentation snapshot. Labels/markers are copied on begin, whereas
// waveform storage is borrowed directly to avoid a duplicate projection array.
// epoch is a SCREEN revision, distinct from a frozen acquisition epoch: changing
// a Held view may require repainting the same captured samples with new axes.
// Caller keeps projection alive/immutable until Complete/Cancelled/Fault and
// must cancel BEFORE modifying or releasing it. No hardware identity is implied.
struct ScreenSnapshot final {
    const WaveformProjection* waveform{};
    std::uint32_t epoch{};
    char status[12]{};
    char quality[5]{}; // RAW/NOM/CAL; never silently supplied by the renderer.
    char channel[28]{};
    char timebase[22]{};
    char trigger[31]{};
    char sampling[20]{};
    char record[52]{};
    char focus[52]{};
    char hint[52]{};
    char zero_label[4]{}; // Explicit REF for raw codes or 0V for voltage axes.
    std::int16_t zero_y{-1};    // Plot-relative coordinate; -1 hides marker.
    std::int16_t trigger_y{-1};
    std::int16_t trigger_x{-1};
    ScreenHighlight highlight{ScreenHighlight::None};
    bool warning{};
};

// Full 320x240 instrument screen using one 64-pixel scratch stripe. No target
// framebuffer, formatting library, allocation, exceptions or virtual dispatch.
// Text is original 5x7 ASCII artwork and preserves SI case (mV != MV).
class ScopeScreen final {
public:
    explicit ScopeScreen(Ili9341Display& display) noexcept : display_{display} {}
    ~ScopeScreen() noexcept { cancel(); }
    ScopeScreen(const ScopeScreen&) = delete;
    ScopeScreen& operator=(const ScopeScreen&) = delete;

    // Requires ready display, nonzero epoch, bounded projection dimensions and
    // valid projected columns. Successful begin replaces a prior job; rejected
    // begin leaves it unchanged. Labels truncate to their array size minus one.
    [[nodiscard]] bool begin(const ScreenSnapshot& snapshot) noexcept;
    // Explicit selective request. Caller marks every changed text field; base
    // Y marker/reference-label changes automatically force full paint; changed
    // X markers and highlights repaint their affected regions selectively.
    // A partial or failed previous job also forces full paint. Projection data
    // must remain immutable through completion, including outstanding DMA.
    [[nodiscard]] bool begin(const ScreenSnapshot& snapshot, std::uint16_t dirty_text) noexcept;
    // Every generated/written pixel counts toward budget, including attempted
    // work in a failed transaction. Work is bounded by pixels and transactions.
    [[nodiscard]] RenderStep step(std::uint32_t current_epoch, RenderBudget budget) noexcept;
    void cancel() noexcept;
    // Required after an external LCD reset/reinitialization that this owner did
    // not observe as a failed transfer. Also ends any outstanding row borrow.
    void invalidate() noexcept { cancel(); cache_valid_ = false; }
    [[nodiscard]] RenderState state() const noexcept { return state_; }

private:
    // A controller exclusively attaches to this screen. It composes directly
    // into view_ only after canceling rendering, avoiding a second snapshot.
    // Public begin still copies independent caller snapshots.
    friend class ScopeController;
    [[nodiscard]] bool begin_composed(std::uint16_t dirty_text) noexcept
    { return begin_job(view_,dirty_text,false,true); }
    [[nodiscard]] bool begin_job(const ScreenSnapshot& snapshot, std::uint16_t dirty_text,
                                 bool full, bool composed = false) noexcept;
    [[nodiscard]] bool row_bounds(std::uint16_t y, std::uint16_t& first, std::uint16_t& end) const noexcept;
    [[nodiscard]] bool seek_row(std::uint8_t& rows_left) noexcept;
    [[nodiscard]] bool step_tile_rects(RenderBudget budget, RenderStep& result) noexcept;
    void prepare_tiles() noexcept;
    void commit_image() noexcept;
    void raster_span(std::uint16_t x, std::uint16_t y, std::uint16_t* output,
                     std::uint16_t count) const noexcept;
    Ili9341Display& display_;
    ScreenSnapshot view_{};
    std::array<std::uint16_t, 64U> row_{};
    // Committed footprints become merged repair bounds during a tile job.
    // Each word packs exact inclusive top/bottom plot rows; 0xffff is empty.
    // Cancel/fault invalidates this worklist; completion rebuilds it from view_.
    std::array<std::uint16_t, ScopeTileCount> old_tiles_{};
    RenderState state_{RenderState::Idle};
    std::uint16_t x_{};
    std::uint16_t y_{};
    std::int16_t old_min_{-1}, old_max_{-1}, next_min_{-1}, next_max_{-1};
    std::int16_t old_zero_{-1}, old_trigger_y_{-1}, old_trigger_x_{-1};
    char old_zero_label_[4]{};
    ScreenHighlight old_highlight_{};
    bool old_warning_{};
    bool cache_valid_{};
    enum class PaintMode : std::uint8_t { Full, Tiles, Rectangle };
    PaintMode paint_mode_{};
    std::uint16_t dirty_text_{};
    std::uint16_t pending_count_{};
    std::uint16_t tile_y_{};
    std::uint8_t tile_index_{};
    bool tile_map_ready_{}, tile_major_active_{};
    std::array<ScreenTextRange,9U> text_ranges_{};
};

} // namespace dso
#endif
