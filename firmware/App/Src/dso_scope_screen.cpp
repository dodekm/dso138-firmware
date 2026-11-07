#include "dso_scope_screen.hpp"
#include "dso_display.hpp"

#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>

namespace dso {

void ScopeScreen::reset_composed_view() noexcept
{
    invalidate();
    // Reuse the existing composition storage; no 296-byte stack temporary.
    static_assert(std::is_trivially_destructible<ScreenSnapshot>::value,
                  "Review composition lifetime reset");
    ::new (static_cast<void*>(&view_)) ScreenSnapshot{};
}

bool ScopeScreen::begin(const ScreenSnapshot& snapshot) noexcept
{
    return begin_job(snapshot, ScreenTextAll, true);
}

bool ScopeScreen::begin(const ScreenSnapshot& snapshot, std::uint16_t dirty_text) noexcept
{
    return begin_job(snapshot, dirty_text, false);
}

RenderStep ScopeScreen::step(std::uint32_t current_epoch, RenderBudget budget) noexcept
{
    if (state_ == RenderState::Drawing && current_epoch != view_.epoch) { cancel(); }
    RenderStep result{state_, 0U, 0U};
    if (state_ != RenderState::Drawing || budget.pixels == 0U || budget.transactions == 0U) { return result; }
    if (!tile_major_active_ && paint_mode_ == PaintMode::Tiles && budget.pixels < ScopeTileWidth) {
        // Restart with one complete plot repair when the tile path is unsuitable.
        // This also handles a budget drop after prior rectangles committed.
        paint_mode_ = PaintMode::Rectangle; tile_map_ready_ = false;
        tile_index_ = 0U; tile_y_ = 0U;
        if (y_ >= ScopePlotY && y_ < ScopePlotY+ScopePlotHeight) { x_ = 0U; y_ = ScopePlotY; }
    }
    if (pending_count_ != 0U) {
        const auto transfer = display_.poll_span();
        if (transfer == TransferState::Pending) { return result; }
        if (transfer != TransferState::Complete) {
            display_.abort_span(); pending_count_ = 0U; cache_valid_ = false;
            state_ = RenderState::Fault; result.state = state_; return result;
        }
        pending_count_ = 0U;
    }
    if (tile_major_active_ && budget.pixels < ScopeTileWidth) {
        // Some rectangles may already be committed, so replay the complete plot.
        tile_major_active_ = false; tile_map_ready_ = false;
        paint_mode_ = PaintMode::Rectangle;
        tile_index_ = 0U; tile_y_ = 0U; x_ = 0U; y_ = ScopePlotY;
    }
    std::uint8_t rows_left{8U};
    if (!tile_major_active_) {
        if (!seek_row(rows_left)) {
            result.state = state_; result.completed = state_ == RenderState::Complete; return result;
        }
        if (tile_map_ready_ && paint_mode_ == PaintMode::Tiles) {
            tile_major_active_ = true; tile_index_ = 0U; tile_y_ = 0U;
        }
    }
    if (tile_major_active_) {
        static_cast<void>(step_tile_rects(budget,result));
        return result;
    }
    while (state_ == RenderState::Drawing && result.pixels < budget.pixels &&
           result.transactions < budget.transactions) {
        const auto remaining = static_cast<std::uint16_t>(budget.pixels - result.pixels);
        std::uint16_t first{}, end{};
        static_cast<void>(row_bounds(y_,first,end));
        const auto room = static_cast<std::uint16_t>(end - x_);
        const auto span = remaining < room ? remaining : room;
        const auto count = static_cast<std::uint16_t>(span < row_.size() ? span : row_.size());
        raster_span(x_, y_, row_.data(), count);
        result.pixels = static_cast<std::uint16_t>(result.pixels + count);
        ++result.transactions;
        if (!display_.begin_span(x_, y_, row_.data(), count)) {
            display_.abort_span(); cache_valid_ = false;
            state_ = RenderState::Fault;
            break;
        }
        pending_count_ = count; x_ = static_cast<std::uint16_t>(x_+count);
        if (display_.transfer_state() == TransferState::Pending) { break; }
        pending_count_ = 0U;
        if (!seek_row(rows_left)) { result.completed = state_ == RenderState::Complete; break; }
    }
    result.state = state_;
    return result;
}

void ScopeScreen::cancel() noexcept
{
    if (state_ == RenderState::Drawing) {
        if (pending_count_ != 0U) { display_.abort_span(); pending_count_ = 0U; }
        cache_valid_ = false; state_ = RenderState::Cancelled;
        tile_major_active_ = false;
    }
}


} // namespace dso
