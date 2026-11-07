#ifndef DSO_SCOPE_SCREEN_HPP
#define DSO_SCOPE_SCREEN_HPP

#include "dso_render_types.hpp"
#include "dso_screen_scene.hpp"
#include "dso_projection_limits.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace dso {

class Ili9341Display;

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
    // Read-only borrow of the controller-composed scene. The screen owns the
    // storage; later controller composition may replace its contents.
    [[nodiscard]] const ScreenSnapshot& snapshot() const noexcept { return view_; }

private:
    // A controller exclusively attaches to this screen. It composes directly
    // into view_ only after canceling rendering, avoiding a second snapshot.
    // Public begin still copies independent caller snapshots.
    friend class ScopeController;
    // Reset the attached controller's view in place; the renderer owns its
    // placement construction and transfer invalidation.
    void reset_composed_view() noexcept;
    [[nodiscard]] ScreenTextRange& composed_text_range(ScreenTextField field) noexcept
    { return text_ranges_[screen_text_index(field)]; }
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
    std::array<ScreenTextRange,ScreenTextFieldCount> text_ranges_{};
};

} // namespace dso
#endif
