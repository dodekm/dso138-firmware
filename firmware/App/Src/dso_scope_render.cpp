#include "dso_scope_screen.hpp"
#include "dso_display_limits.hpp"
#include "dso_display.hpp"
#include "dso_scaling.hpp"

#include <cstddef>
#include <cstring>

namespace dso {
namespace {
constexpr std::uint16_t Black{0x0000U};
constexpr std::uint16_t White{0xffffU};
constexpr std::uint16_t Grey{0xbdf7U};
constexpr std::uint16_t Border{0x7befU};
constexpr std::uint16_t Grid{0x2104U};
constexpr std::uint16_t Axis{0x4208U};
constexpr std::uint16_t Cyan{0x07ffU};
constexpr std::uint16_t Yellow{0xffe0U};
constexpr std::uint16_t Green{0x07e0U};
constexpr std::uint16_t Orange{0xfd20U};
constexpr std::uint16_t FocusBackground{0x0841U};

// Original, hand-authored row artwork. Each low five bits is one row, left
// pixel in bit4. Separate lowercase is essential for accurate SI prefixes.
// Index order: digits, uppercase, lowercase. No external font data is used.
constexpr std::uint8_t Glyphs[62][7]{
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {6,8,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
    {14,17,17,15,1,2,12},
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14}, {7,2,2,2,18,18,12},
    {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
    {0,0,14,1,15,17,15}, {16,16,30,17,17,17,30},
    {0,0,14,17,16,17,14}, {1,1,15,17,17,17,15},
    {0,0,14,17,31,16,14}, {6,9,8,28,8,8,8},
    {0,15,17,17,15,1,14}, {16,16,30,17,17,17,17},
    {4,0,12,4,4,4,14}, {2,0,6,2,2,18,12},
    {16,16,18,20,24,20,18}, {12,4,4,4,4,4,14},
    {0,0,26,21,21,21,21}, {0,0,30,17,17,17,17},
    {0,0,14,17,17,17,14}, {0,0,30,17,30,16,16},
    {0,0,15,17,15,1,1}, {0,0,22,25,16,16,16},
    {0,0,15,16,14,1,30}, {8,8,28,8,8,9,6},
    {0,0,17,17,17,19,13}, {0,0,17,17,17,10,4},
    {0,0,17,17,21,21,10}, {0,0,17,10,4,10,17},
    {0,0,17,17,15,1,14}, {0,0,31,2,4,8,31}
};

constexpr std::uint8_t original_glyph_row(char character, std::uint16_t row) noexcept
{
    if (row >= 7U) { return 0U; }
    if (character >= '0' && character <= '9') {
        return Glyphs[static_cast<unsigned int>(character - '0')][row];
    }
    if (character >= 'A' && character <= 'Z') {
        return Glyphs[10U + static_cast<unsigned int>(character - 'A')][row];
    }
    if (character >= 'a' && character <= 'z') {
        return Glyphs[36U + static_cast<unsigned int>(character - 'a')][row];
    }
    // Punctuation is described by rows rather than a copied font table.
    switch (character) {
    case '\0': case ' ': return 0U;
    case '-': return row == 3U ? 31U : 0U;
    case '_': return row == 6U ? 31U : 0U;
    case '+': return row == 3U ? 31U : (row >= 1U && row <= 5U ? 4U : 0U);
    case '=': return row == 2U || row == 4U ? 31U : 0U;
    case '.': return row == 6U ? 4U : 0U;
    case ',': return row == 5U ? 4U : (row == 6U ? 8U : 0U);
    case ':': return row == 2U || row == 5U ? 4U : 0U;
    case ';': return row == 2U || row == 5U ? 4U : (row == 6U ? 8U : 0U);
    case '/': return static_cast<std::uint8_t>(1U << (row < 5U ? row : 4U));
    case '\\': return static_cast<std::uint8_t>(16U >> (row < 5U ? row : 4U));
    case '!': return row <= 3U || row == 6U ? 4U : 0U;
    case '|': return 4U;
    case '<': return row == 3U ? 16U : (row == 2U || row == 4U ? 8U :
                   (row == 1U || row == 5U ? 4U : 0U));
    case '>': return row == 3U ? 1U : (row == 2U || row == 4U ? 2U :
                   (row == 1U || row == 5U ? 4U : 0U));
    case '(': return row == 0U || row == 6U ? 2U :
                     (row == 1U || row == 5U ? 4U : 8U);
    case ')': return row == 0U || row == 6U ? 8U :
                     (row == 1U || row == 5U ? 4U : 2U);
    case '[': return row == 0U || row == 6U ? 14U : 8U;
    case ']': return row == 0U || row == 6U ? 14U : 2U;
    case '%': return row < 2U ? 25U : (row == 2U ? 2U :
                   (row == 3U ? 4U : (row == 4U ? 8U : 19U)));
    case '?': default:
        return row == 0U ? 14U : (row == 1U ? 17U : (row == 2U ? 2U :
                     (row == 3U || row == 6U ? 4U : 0U)));
    }
}

// Pack seven five-bit rows continuously across all 95 printable characters.
// One trailing byte makes the final two-byte extraction safe. No runtime
// decompression buffer or state is needed.
constexpr auto make_font() noexcept
{
    std::array<std::uint8_t, 417U> font{};
    for (std::size_t character{}; character < 95U; ++character) {
        for (std::uint16_t row{}; row < 7U; ++row) {
            const auto bit = static_cast<unsigned>(character) * 35U + static_cast<unsigned>(row) * 5U;
            const auto byte = bit / 8U;
            const auto bits = static_cast<std::uint16_t>(
                std::uint16_t{original_glyph_row(static_cast<char>(character + 32U), row)} << (bit % 8U));
            font[byte] |= static_cast<std::uint8_t>(bits);
            font[byte+1U] |= static_cast<std::uint8_t>(bits >> 8U);
        }
    }
    return font;
}
constexpr auto Font = make_font();
static_assert(sizeof(Font) == 417U, "Tightly packed printable artwork includes a safe-read sentinel");

constexpr std::uint8_t glyph_row(char character, std::uint16_t row) noexcept
{
    if (character == '\0' || row >= 7U) { return 0U; }
    const auto code = static_cast<unsigned char>(character);
    const auto index = code >= 32U && code <= 126U ? static_cast<unsigned>(code) - 32U : 31U; // '?'
    const auto bit = index * 35U + static_cast<unsigned>(row) * 5U;
    const auto byte = bit / 8U;
    const auto bits = std::uint32_t{Font[byte]} | (std::uint32_t{Font[byte+1U]} << 8U);
    return static_cast<std::uint8_t>((bits >> (bit % 8U)) & 31U);
}

constexpr bool font_matches_artwork() noexcept
{
    for (unsigned character{32U}; character <= 126U; ++character) {
        for (std::uint16_t row{}; row < 7U; ++row) {
            if (glyph_row(static_cast<char>(character),row) != original_glyph_row(static_cast<char>(character),row)) { return false; }
        }
    }
    return true;
}
static_assert(font_matches_artwork(), "Every original artwork row must fit five bits and round-trip exactly");

template <std::size_t N>
void terminate(char (&label)[N]) noexcept
{
    bool ended{false};
    for (std::size_t index{0U}; index < N; ++index) {
        ended = ended || label[index] == '\0' || index == N - 1U;
        if (ended) { label[index] = '\0'; }
    }
}

void paint_span(std::uint16_t* output, std::uint16_t x, std::uint16_t count,
                std::uint16_t left, std::uint16_t right, std::uint16_t color) noexcept
{
    const auto first = x > left ? x : left;
    const auto end = static_cast<std::uint16_t>(x + count < right ? x + count : right);
    for (auto column = first; column < end; ++column) { output[column - x] = color; }
}

void text_span(std::uint16_t* output, std::uint16_t x, std::uint16_t count,
               const char* label, std::uint16_t capacity, std::uint16_t left,
               std::uint16_t right, std::uint16_t row, std::uint16_t color) noexcept
{
    auto column = x > left ? x : left;
    const auto end = static_cast<std::uint16_t>(x + count < right ? x + count : right);
    if (column >= end) { return; }
    const auto dx = static_cast<std::uint16_t>(column - left);
    auto character = static_cast<std::uint16_t>(dx / 6U);
    auto part = static_cast<std::uint16_t>(dx % 6U);
    while (column < end && character < capacity && label[character] != '\0') {
        const auto artwork = glyph_row(label[character], row);
        do {
            if (part < 5U && (artwork & (16U >> part)) != 0U) { output[column - x] = color; }
            ++column; ++part;
        } while (column < end && part < 6U);
        ++character; part = 0U;
    }
}

bool inside_marker(std::int16_t value, std::uint16_t extent) noexcept
{
    return value >= 0 && static_cast<std::uint16_t>(value) < extent;
}

std::uint16_t marker_top(std::int16_t value) noexcept
{
    const auto center = static_cast<std::int32_t>(ScopePlotY) + value;
    const auto top = center - 3;
    if (top < ScopePlotY) { return ScopePlotY; }
    if (top > static_cast<std::int32_t>(ScopePlotY + ScopePlotHeight - 7U)) {
        return static_cast<std::uint16_t>(ScopePlotY + ScopePlotHeight - 7U);
    }
    return static_cast<std::uint16_t>(top);
}

struct TextBand final {
    ScreenTextField field;
    std::uint16_t offset, left, right, color;
    std::uint8_t capacity, top;
};
constexpr TextBand TextBands[]{
    {ScreenTextField::Status,offsetof(ScreenSnapshot,status),94U,278U,Green,sizeof(ScreenSnapshot::status),2U},
    {ScreenTextField::Quality,offsetof(ScreenSnapshot,quality),286U,319U,Yellow,sizeof(ScreenSnapshot::quality),2U},
    {ScreenTextField::Channel,offsetof(ScreenSnapshot,channel),2U,178U,White,sizeof(ScreenSnapshot::channel),12U},
    {ScreenTextField::Timebase,offsetof(ScreenSnapshot,timebase),182U,319U,White,sizeof(ScreenSnapshot::timebase),12U},
    {ScreenTextField::Trigger,offsetof(ScreenSnapshot,trigger),2U,190U,Grey,sizeof(ScreenSnapshot::trigger),22U},
    {ScreenTextField::Sampling,offsetof(ScreenSnapshot,sampling),194U,319U,Grey,sizeof(ScreenSnapshot::sampling),22U},
    {ScreenTextField::Record,offsetof(ScreenSnapshot,record),2U,319U,Grey,sizeof(ScreenSnapshot::record),213U},
    {ScreenTextField::Focus,offsetof(ScreenSnapshot,focus),2U,319U,Cyan,sizeof(ScreenSnapshot::focus),222U},
    {ScreenTextField::Hint,offsetof(ScreenSnapshot,hint),2U,319U,White,sizeof(ScreenSnapshot::hint),232U}
};
static_assert(sizeof(TextBands) / sizeof(TextBands[0]) == ScreenTextFieldCount,
              "Every text field needs one display band");
constexpr bool text_fields_match_bands() noexcept
{
    std::uint16_t mapped{};
    for (const auto& band : TextBands) {
        const auto bit = screen_text_mask(band.field);
        if (bit == 0U || (mapped & bit) != 0U) { return false; }
        mapped = static_cast<std::uint16_t>(mapped | bit);
    }
    return mapped == ScreenTextAll;
}
static_assert(text_fields_match_bands(), "Text fields and display bands must map one-to-one");

constexpr std::uint16_t TileWidth{ScopeTileWidth};
constexpr unsigned TileCount{ScopeTileCount};
constexpr std::uint16_t EmptyTile{0xffffU};
static_assert(ScopePlotHeight <= 255U &&
              (TileCount-1U)*TileWidth < ScopePlotWidth &&
              TileCount*TileWidth >= ScopePlotWidth, "Tile coverage");

std::uint16_t footprint(const WaveformProjection* waveform, unsigned tile) noexcept
{
    if (waveform == nullptr || !waveform->available) { return EmptyTile; }
    std::int16_t top{ScopePlotHeight}, bottom{};
    const auto start = tile * TileWidth;
    const auto finish = start + TileWidth < ScopePlotWidth ? start + TileWidth : ScopePlotWidth;
    for (auto x = start; x < finish; ++x) {
        const auto& column = waveform->columns[x];
        if (!column.valid) { continue; }
        if (column.top < top) { top = column.top; }
        if (column.bottom > bottom) { bottom = column.bottom; }
    }
    return top == ScopePlotHeight ? EmptyTile : static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bottom) << 8U) | static_cast<std::uint16_t>(top));
}

} // namespace

bool ScopeScreen::begin_job(const ScreenSnapshot& snapshot, std::uint16_t dirty_text,
                            bool full, bool composed) noexcept
{
    if (!display_.ready() || snapshot.epoch == 0U || (dirty_text & ~ScreenTextAll) != 0U) { return false; }
    std::int16_t minimum{-1}, maximum{-1};
    if (snapshot.waveform != nullptr && snapshot.waveform->available) {
        const auto& waveform = *snapshot.waveform;
        if (waveform.width != ScopePlotWidth || waveform.height != ScopePlotHeight) {
            return false;
        }
        for (std::uint16_t column{0U}; column < ScopePlotWidth; ++column) {
            const auto& part = waveform.columns[column];
            if (part.valid && (part.top < 0 || part.bottom < part.top ||
                              part.bottom >= static_cast<std::int16_t>(ScopePlotHeight))) {
                return false;
            }
            if (part.valid) {
                if (minimum < 0 || part.top < minimum) { minimum = part.top; }
                if (part.bottom > maximum) { maximum = part.bottom; }
            }
        }
    }
    // Validation above leaves a current job and its borrow untouched on reject.
    // A valid replacement ends any outstanding bus read before changing view.
    cancel();
    // Public callers supply a dirty field mask, not proof of changed cells.
    // Keep validation ahead of this reset so a rejected replacement preserves
    // the active job's ranges as well as its borrowed pixels and progress.
    if (!composed) { text_ranges_ = {}; }
    if (&snapshot != &view_) { view_ = snapshot; }
    terminate(view_.status); terminate(view_.quality); terminate(view_.channel);
    terminate(view_.timebase); terminate(view_.trigger); terminate(view_.sampling);
    terminate(view_.record); terminate(view_.focus); terminate(view_.hint);
    terminate(view_.zero_label);
    paint_mode_ = full || !cache_valid_ || view_.zero_y != old_zero_ ||
        view_.trigger_y != old_trigger_y_ ||
        std::memcmp(view_.zero_label,old_zero_label_,sizeof(old_zero_label_)) != 0 ? PaintMode::Full :
        view_.trigger_x != old_trigger_x_ ? PaintMode::Rectangle : PaintMode::Tiles;
    dirty_text_ = static_cast<std::uint16_t>(dirty_text | (view_.warning != old_warning_ ? 1U : 0U));
    // A status color change affects every existing glyph, including unchanged
    // characters outside the formatter's changed substring.
    if (view_.warning != old_warning_) {
        text_ranges_[screen_text_index(ScreenTextField::Status)] = {};
    }
    if (view_.highlight != old_highlight_) {
        // Highlight colors affect the entire old/new label, even where text
        // characters did not change. Public dirty masks cannot express that.
        if (view_.highlight == ScreenHighlight::Timebase || old_highlight_ == ScreenHighlight::Timebase) {
            dirty_text_ |= screen_text_mask(ScreenTextField::Timebase);
            text_ranges_[screen_text_index(ScreenTextField::Timebase)] = {};
        }
        if (view_.highlight == ScreenHighlight::Trigger || old_highlight_ == ScreenHighlight::Trigger) {
            dirty_text_ |= screen_text_mask(ScreenTextField::Trigger);
            text_ranges_[screen_text_index(ScreenTextField::Trigger)] = {};
        }
    }
    next_min_ = minimum; next_max_ = maximum;
    pending_count_ = 0U;
    tile_map_ready_ = false; tile_major_active_ = false;
    tile_index_ = 0U; tile_y_ = 0U;
    x_ = 0U; y_ = 0U; state_ = RenderState::Drawing;
    return true;
}

bool ScopeScreen::row_bounds(std::uint16_t y, std::uint16_t& first, std::uint16_t& end) const noexcept
{
    first = DisplayWidth; end = 0U;
    if (paint_mode_ == PaintMode::Full) { first = 0U; end = DisplayWidth; return x_ < end; }
    if (view_.highlight != old_highlight_) {
        if (y == 20U && (old_highlight_ == ScreenHighlight::Timebase || view_.highlight == ScreenHighlight::Timebase)) {
            first = 182U; end = 319U;
        }
        if (y == 30U && (old_highlight_ == ScreenHighlight::Trigger || view_.highlight == ScreenHighlight::Trigger)) {
            first = 2U; end = 188U;
        }
        if (y == 210U && (old_highlight_ == ScreenHighlight::Position || view_.highlight == ScreenHighlight::Position)) {
            first = ScopePlotX; end = ScopePlotX + ScopePlotWidth;
        }
    }
    if (view_.trigger_x != old_trigger_x_ && y >= ScopePlotY && y < ScopePlotY+ScopePlotHeight) {
        // A clipped seven-column stripe covers each triangle and dashed stem.
        // Merge the old and new stripes so both are restored, including gaps.
        for (const auto marker : {old_trigger_x_,view_.trigger_x}) {
            if (!inside_marker(marker,ScopePlotWidth)) { continue; }
            const auto left = marker > 3 ? static_cast<std::uint16_t>(marker-3) : 0U;
            const auto right = static_cast<std::uint16_t>(marker+4 < ScopePlotWidth ? marker+4 : ScopePlotWidth);
            if (x_ < ScopePlotX+right) {
                if (ScopePlotX+left < first) { first = static_cast<std::uint16_t>(ScopePlotX+left); }
                if (ScopePlotX+right > end) { end = static_cast<std::uint16_t>(ScopePlotX+right); }
            }
        }
    }
    const auto low = old_min_ < 0 ? next_min_ : next_min_ < 0 ? old_min_ : old_min_ < next_min_ ? old_min_ : next_min_;
    const auto high = old_max_ > next_max_ ? old_max_ : next_max_;
    if (low >= 0 && y >= ScopePlotY + low && y <= ScopePlotY + high) {
        if (x_ >= ScopePlotX + ScopePlotWidth) { return false; }
        if (paint_mode_ == PaintMode::Rectangle) {
            first = ScopePlotX; end = ScopePlotX+ScopePlotWidth; return true;
        }
        first = ScopePlotX; end = ScopePlotX + ScopePlotWidth;
        return x_ < end;
    }
    for (const auto& band : TextBands) {
        if ((dirty_text_ & screen_text_mask(band.field)) == 0U ||
            y < band.top || y >= band.top + 7U) { continue; }
        const auto& range = text_ranges_[screen_text_index(band.field)];
        const auto left = static_cast<std::uint16_t>(band.left + std::uint16_t{range.first} * 6U);
        const auto right = static_cast<std::uint16_t>(band.left + std::uint16_t{range.end} * 6U);
        const auto begin = range.end == 0U ? band.left : left < band.right ? left : band.right;
        const auto finish = range.end == 0U ? band.right : right < band.right ? right : band.right;
        if (begin < first) { first = begin; }
        if (finish > end) { end = finish; }
    }
    return first < end && x_ < end;
}

void ScopeScreen::prepare_tiles() noexcept
{
    // Merge the two footprints in place. A continuous interval may repaint
    // gaps, but every emitted pixel is still rasterized from the current view.
    const auto low = old_min_ < 0 ? next_min_ : next_min_ < 0 ? old_min_ : old_min_ < next_min_ ? old_min_ : next_min_;
    const auto high = old_max_ > next_max_ ? old_max_ : next_max_;
    if (low < 0 || high-low+1 < 14) { paint_mode_ = PaintMode::Rectangle; return; }
    for (unsigned tile{}; tile < TileCount; ++tile) {
        const auto next = footprint(view_.waveform,tile);
        const auto old = old_tiles_[tile];
        if (old == EmptyTile) { old_tiles_[tile] = next; }
        else if (next != EmptyTile) {
            const auto top = (old & 0xffU) < (next & 0xffU) ? (old & 0xffU) : (next & 0xffU);
            const auto old_bottom = old >> 8U, next_bottom = next >> 8U;
            const auto bottom = old_bottom > next_bottom ? old_bottom : next_bottom;
            old_tiles_[tile] = static_cast<std::uint16_t>((static_cast<std::uint32_t>(bottom) << 8U) |
                static_cast<std::uint32_t>(top));
        }
    }
}

void ScopeScreen::commit_image() noexcept
{
    // All pending writes have completed. Replace the repair worklist with
    // committed footprints from the immutable projection.
    for (unsigned tile{}; tile < TileCount; ++tile) { old_tiles_[tile] = footprint(view_.waveform,tile); }
    old_min_ = next_min_; old_max_ = next_max_;
    old_zero_ = view_.zero_y; old_trigger_y_ = view_.trigger_y; old_trigger_x_ = view_.trigger_x;
    old_highlight_ = view_.highlight; old_warning_ = view_.warning;
    std::memcpy(old_zero_label_,view_.zero_label,sizeof(old_zero_label_));
    text_ranges_ = {};
    cache_valid_ = true; state_ = RenderState::Complete;
}

bool ScopeScreen::seek_row(std::uint8_t& rows_left) noexcept
{
    // Share the allowance across every seek in this service, including after
    // synchronous spans. Empty rows yield cooperatively to acquisition.
    while (y_ < DisplayHeight && rows_left != 0U) {
        --rows_left;
        if (!tile_map_ready_ && paint_mode_ == PaintMode::Tiles && y_ == ScopePlotY && x_ == 0U) {
            prepare_tiles(); tile_map_ready_ = true;
        }
        if (paint_mode_ == PaintMode::Tiles && y_ == ScopePlotY) {
            x_ = ScopePlotX;
            return true;
        }
        std::uint16_t first{}, end{};
        if (row_bounds(y_,first,end)) {
            if (x_ < first) { x_ = first; }
            return true;
        }
        x_ = 0U; ++y_;
    }
    if (y_ == DisplayHeight) { commit_image(); }
    return false;
}

bool ScopeScreen::step_tile_rects(RenderBudget budget, RenderStep& result) noexcept
{
    if (budget.pixels < TileWidth || budget.transactions == 0U) { return true; }
    const auto low = old_min_ < 0 ? next_min_ : next_min_ < 0 ? old_min_ : old_min_ < next_min_ ? old_min_ : next_min_;
    const auto high = old_max_ > next_max_ ? old_max_ : next_max_;
    while (tile_index_ < TileCount && result.pixels < budget.pixels &&
           result.transactions < budget.transactions) {
        const auto tile = static_cast<unsigned>(tile_index_);
        const auto packed = old_tiles_[tile];
        if (packed == EmptyTile) { ++tile_index_; tile_y_ = 0U; continue; }
        const auto first_x = static_cast<std::uint16_t>(ScopePlotX + tile*TileWidth);
        const auto width = static_cast<std::uint16_t>(tile == TileCount-1U ?
            ScopePlotWidth-tile*TileWidth : TileWidth);
        auto first_y = static_cast<std::uint16_t>(ScopePlotY + (packed & 0xffU));
        auto after_y = static_cast<std::uint16_t>(ScopePlotY + (packed >> 8U) + 1U);
        const auto clip_top = static_cast<std::uint16_t>(ScopePlotY + low);
        const auto clip_after = static_cast<std::uint16_t>(ScopePlotY + high + 1);
        if (first_y < clip_top) { first_y = clip_top; }
        if (after_y > clip_after) { after_y = clip_after; }
        if (tile_y_ == 0U) { tile_y_ = first_y; }
        if (tile_y_ >= after_y) { ++tile_index_; tile_y_ = 0U; continue; }
        const auto remaining = static_cast<std::uint16_t>(budget.pixels-result.pixels);
        const auto rows_left = static_cast<std::uint16_t>(after_y-tile_y_);
        auto height = static_cast<std::uint16_t>(64U/width);
        const auto max_rows_by_budget = static_cast<std::uint16_t>(remaining/width);
        if (height > rows_left) { height = rows_left; }
        if (height > max_rows_by_budget) { height = max_rows_by_budget; }
        if (height == 0U) { break; }
        const auto count = static_cast<std::uint16_t>(width*height);
        for (std::uint16_t row{}; row < height; ++row) {
            raster_span(first_x,static_cast<std::uint16_t>(tile_y_+row),
                row_.data()+static_cast<std::size_t>(row)*width,width);
        }
        result.pixels = static_cast<std::uint16_t>(result.pixels+count);
        ++result.transactions;
        if (!display_.begin_rect(first_x,tile_y_,width,height,row_.data(),count)) {
            display_.abort_span(); pending_count_ = 0U;
            cache_valid_ = false; state_ = RenderState::Fault; break;
        }
        pending_count_ = count;
        tile_y_ = static_cast<std::uint16_t>(tile_y_+height);
        if (tile_y_ >= after_y) { ++tile_index_; tile_y_ = 0U; }
        if (display_.transfer_state() == TransferState::Pending) { break; }
        pending_count_ = 0U;
    }
    if (tile_index_ == TileCount && state_ == RenderState::Drawing) {
        tile_major_active_ = false; tile_map_ready_ = false;
        x_ = 0U; y_ = ScopePlotY + ScopePlotHeight;
    }
    result.state = state_;
    return true;
}

void ScopeScreen::raster_span(std::uint16_t x, std::uint16_t y, std::uint16_t* output,
                              std::uint16_t count) const noexcept
{
    // Every span still emits exactly count pixels. Row classification and
    // glyph decoding are shared; only footprints, never old pixels, are cached.
    const auto background = y == 31U || y == 211U ? Axis :
        y >= 221U && y <= 229U ? FocusBackground : Black;
    for (std::uint16_t i{}; i < count; ++i) { output[i] = background; }
    if (y == 31U || y == 211U) { return; }
    if (y < 30U || y >= 213U) {
        if (y >= 2U && y < 9U) {
            text_span(output,x,count,"DSO138",7U,2U,88U,static_cast<std::uint16_t>(y-2U),Cyan);
        }
        for (const auto& band : TextBands) {
            const auto row = static_cast<std::uint16_t>(y-band.top);
            if (row >= 7U) { continue; }
            auto color = band.color;
            if (band.offset == offsetof(ScreenSnapshot,status) && view_.warning) { color = Orange; }
            if ((band.offset == offsetof(ScreenSnapshot,timebase) && view_.highlight == ScreenHighlight::Timebase) ||
                (band.offset == offsetof(ScreenSnapshot,trigger) && view_.highlight == ScreenHighlight::Trigger)) { color = Cyan; }
            const auto* label = reinterpret_cast<const char*>(&view_) + band.offset;
            text_span(output,x,count,label,band.capacity,band.left,band.right,row,color);
        }
    }
    if (y >= ScopePlotY && y < ScopePlotY + ScopePlotHeight) {
        const auto py = static_cast<std::uint16_t>(y - ScopePlotY);
        const auto first = x > ScopePlotX ? x : ScopePlotX;
        const auto end = static_cast<std::uint16_t>(x + count < ScopePlotX + ScopePlotWidth ? x + count : ScopePlotX + ScopePlotWidth);
        const bool edge_row = py == 0U || py == ScopePlotHeight - 1U;
        const bool center = py == ScopePlotHeight / 2U;
        const bool grid_column = py % 3U == 0U;
        const bool grid_row = py % ScopePixelsPerVerticalDiv == 0U;
        const bool ticks_row = py >= ScopePlotHeight / 2U - 2U && py <= ScopePlotHeight / 2U + 2U;
        const bool ticks_column = py % 11U == 0U;
        for (auto column = first; column < end; ++column) {
            const auto px = static_cast<std::uint16_t>(column - ScopePlotX);
            auto color = Black;
            if (edge_row || px == 0U || px == ScopePlotWidth - 1U) { color = Border; }
            else if (center || px == ScopePlotWidth / 2U) { color = Axis; }
            else if ((grid_column && px % ScopePixelsPerHorizontalDiv == 0U) ||
                     (grid_row && px % 3U == 0U)) { color = Grid; }
            else if ((ticks_row && px % 7U == 0U) ||
                     (ticks_column && px >= ScopePlotWidth / 2U - 2U && px <= ScopePlotWidth / 2U + 2U)) { color = Axis; }
            output[column-x] = color;
        }
        // Overlay in increasing priority: reference, trigger level/position,
        // then waveform. This is the original pixel return order reversed.
        if (view_.zero_y == static_cast<std::int16_t>(py)) {
            for (auto column = first; column < end; ++column) {
                if ((column-ScopePlotX) % 4U == 0U) { output[column-x] = Cyan; }
            }
        }
        if (view_.trigger_y == static_cast<std::int16_t>(py)) {
            for (auto column = first; column < end; ++column) {
                if ((column-ScopePlotX) % 8U < 4U) { output[column-x] = Orange; }
            }
        }
        if (inside_marker(view_.trigger_x,ScopePlotWidth)) {
            const auto tx = static_cast<std::uint16_t>(ScopePlotX + view_.trigger_x);
            if (py < 4U) {
                const auto radius = static_cast<std::uint16_t>(3U-py);
                const auto left = tx > ScopePlotX + radius ? static_cast<std::uint16_t>(tx-radius) : ScopePlotX;
                const auto right = tx + radius + 1U < ScopePlotX + ScopePlotWidth ? static_cast<std::uint16_t>(tx+radius+1U) :
                    static_cast<std::uint16_t>(ScopePlotX + ScopePlotWidth);
                paint_span(output,x,count,left,right,Orange);
            } else if (py % 6U < 3U) { paint_span(output,x,count,tx,static_cast<std::uint16_t>(tx+1U),Orange); }
        }
        if (view_.waveform != nullptr && view_.waveform->available) {
            for (auto column = first; column < end; ++column) {
                const auto& part = view_.waveform->columns[column-ScopePlotX];
                if (part.valid && py >= static_cast<std::uint16_t>(part.top) && py <= static_cast<std::uint16_t>(part.bottom)) {
                    output[column-x] = part.clipped && edge_row ? Orange : Yellow;
                }
            }
        }
    }
    for (unsigned side{}; side < 2U; ++side) {
        const auto marker = side == 0U ? view_.zero_y : view_.trigger_y;
        if (!inside_marker(marker,ScopePlotHeight)) { continue; }
        const auto color = side == 0U ? Cyan : Orange;
        const auto top = marker_top(marker);
        if (y >= top && y < top+7U) {
            text_span(output,x,count,side == 0U ? view_.zero_label : "T",side == 0U ? sizeof(view_.zero_label) : 2U,
                side == 0U ? 2U : 313U,side == 0U ? 22U : 319U,static_cast<std::uint16_t>(y-top),color);
        }
        const auto center = static_cast<std::int32_t>(ScopePlotY) + marker;
        const auto delta = static_cast<std::int32_t>(y) - center;
        const auto distance = delta < 0 ? -delta : delta;
        if (distance <= 3) {
            paint_span(output,x,count,side == 0U ? 24U : static_cast<std::uint16_t>(ScopePlotX+ScopePlotWidth+distance),
                side == 0U ? static_cast<std::uint16_t>(28-distance) : 312U,color);
        }
    }
    if (view_.highlight == ScreenHighlight::Timebase && y == 20U) { paint_span(output,x,count,182U,319U,Cyan); }
    if (view_.highlight == ScreenHighlight::Trigger && y == 30U) { paint_span(output,x,count,2U,188U,Cyan); }
    if (view_.highlight == ScreenHighlight::Position && y == 210U) {
        paint_span(output,x,count,ScopePlotX,ScopePlotX+ScopePlotWidth,Cyan);
    }
}

} // namespace dso
