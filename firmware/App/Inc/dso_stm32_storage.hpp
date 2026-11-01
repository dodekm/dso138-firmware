#ifndef DSO_STM32_STORAGE_HPP
#define DSO_STM32_STORAGE_HPP

#include "dso_storage.hpp"
#include <cstdint>

namespace dso {
struct Stm32StorageQualification final {
    bool silicon_verified{};
    bool recovery_verified{};
    bool supply_verified{};
    bool exclusive_fpec_verified{};
    bool interruption_policy_verified{};
};
enum class Stm32StorageState : std::uint8_t { Dormant, Ready, Fault };
enum class Stm32StorageFault : std::uint32_t {
    None, Qualification, Identity, Execution, Clocks, Ownership, Protection,
    InvalidArgument, Program, Verification, BusyTimeout
};

// One foreground owner. Merely obtaining this table/port accesses no hardware.
// All five external qualifications and an explicit nonzero board identity are
// required. Revocation changes RAM only and never repairs a foreign FPEC owner.
// A permanently busy operation enters a diagnostic SRAM stop, not a return to
// unavailable Flash. The port is not an electrical/power-loss qualification.
struct Stm32StorageApi final {
    bool (*prepare)(std::uint64_t board_identity, Stm32StorageQualification) noexcept;
    StoragePort (*port)() noexcept;
    Stm32StorageState (*state)() noexcept;
    Stm32StorageFault (*fault)() noexcept;
};
[[nodiscard]] const Stm32StorageApi& stm32_storage_api() noexcept;
} // namespace dso
#endif
