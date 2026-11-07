#ifndef DSO_BOARD_BRINGUP_HPP
#define DSO_BOARD_BRINGUP_HPP

#include <cstddef>
#include <cstdint>

// CubeIDE selects the classic-board profile. Activation still requires the
// owner-configured MCU UID below; a zero UID intentionally cannot match.
#ifndef DSO_BOARD_BRINGUP
#define DSO_BOARD_BRINGUP 0
#endif
static_assert(DSO_BOARD_BRINGUP == 0 || DSO_BOARD_BRINGUP == 1, "Invalid board profile");
#ifndef DSO_BOARD_TEST_OUTPUT
#define DSO_BOARD_TEST_OUTPUT 0
#endif
static_assert(DSO_BOARD_TEST_OUTPUT == 0 || DSO_BOARD_TEST_OUTPUT == 1, "Invalid test-output profile");
static_assert(!DSO_BOARD_TEST_OUTPUT || DSO_BOARD_BRINGUP, "Test output requires the identified bench board");
#ifndef DSO_BOARD_ACQUISITION
#define DSO_BOARD_ACQUISITION 0
#endif
static_assert(DSO_BOARD_ACQUISITION == 0 || DSO_BOARD_ACQUISITION == 1, "Invalid acquisition profile");
static_assert(!DSO_BOARD_ACQUISITION || DSO_BOARD_BRINGUP, "Acquisition requires the identified bench board");
#ifndef DSO_BOARD_NOMINAL_GAIN
#define DSO_BOARD_NOMINAL_GAIN 0
#endif
static_assert(DSO_BOARD_NOMINAL_GAIN == 0 || DSO_BOARD_NOMINAL_GAIN == 2 || DSO_BOARD_NOMINAL_GAIN == 8,
              "Nominal gain must be disabled, 2 or 8");
#ifndef DSO_BOARD_ZERO_CODE_Q16
#define DSO_BOARD_ZERO_CODE_Q16 135573463
#endif
static_assert(DSO_BOARD_ZERO_CODE_Q16 >= 0 && DSO_BOARD_ZERO_CODE_Q16 <= 268369920,
              "Nominal zero code must be a Q16 ADC code in range");
static_assert(DSO_BOARD_NOMINAL_GAIN == 0 || (DSO_BOARD_BRINGUP && DSO_BOARD_ACQUISITION),
              "Nominal gain requires the identified bring-up acquisition profile");
// Storage is a separate explicit bench opt-in. The flags supplied to the
// FPEC adapter remain operator-authorized assumptions, not measured proof.
#ifndef DSO_BOARD_STORAGE
#define DSO_BOARD_STORAGE 0
#endif
static_assert(DSO_BOARD_STORAGE == 0 || DSO_BOARD_STORAGE == 1, "Invalid storage profile");
static_assert(!DSO_BOARD_STORAGE || (DSO_BOARD_BRINGUP && DSO_BOARD_ACQUISITION && DSO_BOARD_NOMINAL_GAIN == 2),
              "Storage requires the identified bring-up acquisition gain-2 profile");

namespace dso {
class Ili9341Display;
// Configure these from your own MCU UID before building; see docs/getting-started.md.
inline constexpr std::uint32_t BringupUid[3]{0U, 0U, 0U};
inline constexpr std::uint64_t BringupBoardIdentity{0ULL};
// Populated only after an actual repeated controller-ID read, before building.
inline constexpr std::uint16_t BringupLcdId{0x9341U};
struct BringupStatus final {
    std::uint32_t stage;
    std::uint32_t lcd_id;
    std::uint32_t power_fault;
    std::uint32_t display_fault;
};
extern volatile BringupStatus dso_bringup_status;
// Foreground-only debug telemetry. CYCCNT needs a live core clock and no
// debug halt; Sleep counting depends on debugger clock policy. Milliseconds
// use SysTick. Neither counter measures external timing or CPU occupancy.
struct BringupAcquisitionStatus final {
    std::uint32_t ready_ms, first_screen_ms, last_screen_ms, screen_count;
    std::uint32_t max_service_cycles, frames_ready, acquisition_fault, transport_fault;
    std::uint32_t controller_fault, panel_fault, physical_generation, frame_epoch;
    std::uint32_t frame_code_flags, frame_frequency_millihz;
    std::uint32_t max_running_service_cycles;
    std::uint32_t max_poll_gap_cycles, max_snapshot_cycles;
    std::uint32_t publication_sequence;
};
static_assert(offsetof(BringupAcquisitionStatus, frame_code_flags) == 48U &&
              offsetof(BringupAcquisitionStatus, frame_frequency_millihz) == 52U &&
              offsetof(BringupAcquisitionStatus, publication_sequence) == 68U &&
              sizeof(BringupAcquisitionStatus) == 72U,
              "Keep the compact coherent publication ABI bounded");
extern volatile BringupAcquisitionStatus dso_bringup_acquisition;
[[nodiscard]] std::uint32_t bringup_cycles() noexcept;
// ES096 makes DBGMCU_IDCODE unreliable to running software. Match the
// owner-configured full UID and 64 KiB Flash size instead.
[[nodiscard]] bool bringup_identity_matches() noexcept;
// Classic-board energization, bound to the owner-configured MCU UID. The
// permissions are recorded bench assumptions, not measured rail/electrical
// acceptance. Acquisition, test output and storage are explicit build choices.
[[nodiscard]] bool activate_bringup_board(Ili9341Display& display) noexcept;
[[nodiscard]] std::uint16_t bringup_buttons() noexcept;
} // namespace dso
#endif
