#ifndef DSO_QUALIFICATION_MAILBOX_HPP
#define DSO_QUALIFICATION_MAILBOX_HPP

#include "dso_panel_sampler.hpp"

#include <cstddef>
#include <cstdint>

namespace dso {

// The command nibble deliberately uses the existing single-button mask value.
enum class QualificationAction : std::uint8_t { Ok = 1U, Plus = 2U, Minus = 4U, Select = 8U };
enum class QualificationMailboxResult : std::uint8_t {
    None = 0U, Dispatched = 1U, InvalidCommand = 2U, Duplicate = 3U,
    Stale = 4U, RouteFailed = 5U
};

// Wire-visible bytes only. Payload packs command in bits 31..28 and a
// modulo-2^28 action sequence in bits 27..0. Producer-owned writes are
// payload-first then commit-byte; consumer-owned writes are result-first then
// acknowledgement-byte. The producer must not replace a pending payload.
struct alignas(4) QualificationMailboxWire final {
    volatile std::uint32_t payload{};
    volatile std::uint8_t commit{};
    std::uint8_t producer_reserved[3]{};
    volatile std::uint8_t acknowledgement{};
    volatile std::uint8_t result{};
    std::uint8_t consumer_reserved[2]{};
};

static_assert(alignof(QualificationMailboxWire) >= 4U);
static_assert(offsetof(QualificationMailboxWire, payload) == 0U);
static_assert(offsetof(QualificationMailboxWire, commit) == 4U);
static_assert(offsetof(QualificationMailboxWire, acknowledgement) == 8U);
static_assert(offsetof(QualificationMailboxWire, result) == 9U);
static_assert(sizeof(QualificationMailboxWire) == 12U);

using QualificationActionRoute = bool (*)(void*, QualificationAction) noexcept;

// A command may share the foreground UI boundary only after a complete,
// current-epoch panel scan proves both the switches and raw active-low keys
// are stable and released. Checking only debounced events would allow a key
// still inside its debounce interval to overlap a synthetic action.
[[nodiscard]] inline bool qualification_action_boundary(
    const PanelSamplerUpdate& panel, const PanelUpdate& update,
    bool paused, bool storage_busy) noexcept
{
    return panel.state == PanelSamplerState::Ready && panel.observation_available &&
        panel.observation.senses_valid &&
        normalize_panel_buttons(panel.observation.gpio_b_idr) == 0U &&
        update.ready && !update.configuration_changed &&
        update.buttons.pressed == 0U && update.buttons.released == 0U &&
        update.buttons.held == 0U && !update.buttons.discontinuity &&
        !paused && !storage_busy;
}

// Separate target-owned ordering state. A successful/invalid newer sequence is
// remembered, so changing its command and recommitting cannot replay an action.
class QualificationMailboxConsumer final {
public:
    static constexpr std::uint32_t SequenceMask{0x0fffffffU};
    static constexpr std::uint32_t SequenceHalfRange{0x08000000U};

    QualificationMailboxConsumer() noexcept = default;
    QualificationMailboxConsumer(const QualificationMailboxConsumer&) = delete;
    QualificationMailboxConsumer& operator=(const QualificationMailboxConsumer&) = delete;

    // barrier() is the target DMB on firmware and a deterministic no-op/test
    // hook on the host. A true return means exactly one pending commit was
    // resolved and its result published before acknowledgement.
    template <typename Barrier>
    [[nodiscard]] bool poll(QualificationMailboxWire& wire,
                            QualificationActionRoute route, void* context,
                            Barrier barrier) noexcept
    {
        const auto committed = wire.commit;
        if (committed == wire.acknowledgement) { return false; }
        barrier(); // Acquire payload after observing the producer's commit byte.
        const auto payload = wire.payload;
        const auto sequence = payload & SequenceMask;
        const auto command = static_cast<std::uint8_t>(payload >> 28U);

        QualificationMailboxResult terminal{QualificationMailboxResult::None};
        if (have_sequence_) {
            const auto delta = (sequence - last_sequence_) & SequenceMask;
            if (delta == 0U) {
                terminal = QualificationMailboxResult::Duplicate;
            } else if (delta >= SequenceHalfRange) {
                terminal = QualificationMailboxResult::Stale;
            }
        }

        if (terminal == QualificationMailboxResult::None) {
            last_sequence_ = sequence;
            have_sequence_ = true;
            if (command == 0U || command > 8U || (command & (command - 1U)) != 0U) {
                terminal = QualificationMailboxResult::InvalidCommand;
            } else {
                const auto action = static_cast<QualificationAction>(command);
                terminal = route != nullptr && route(context, action) ?
                    QualificationMailboxResult::Dispatched : QualificationMailboxResult::RouteFailed;
            }
        }

        wire.result = static_cast<std::uint8_t>(terminal);
        barrier(); // Release result before publishing acknowledgement.
        wire.acknowledgement = committed;
        return true;
    }

    [[nodiscard]] static constexpr std::uint32_t pack(std::uint32_t sequence,
                                                       std::uint8_t command) noexcept
    { return (static_cast<std::uint32_t>(command) << 28U) | (sequence & SequenceMask); }

private:
    std::uint32_t last_sequence_{};
    bool have_sequence_{};
};

static_assert(sizeof(QualificationMailboxConsumer) <= 8U);
static_assert(sizeof(QualificationMailboxWire) + sizeof(QualificationMailboxConsumer) <= 24U);

} // namespace dso

#endif
