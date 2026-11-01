#ifndef DSO_FAULT_REPORT_HPP
#define DSO_FAULT_REPORT_HPP
#include "dso_cpu_fault.h"
#include <cstdint>

namespace dso {
enum class CpuFaultKind : std::uint8_t { Nmi = 2U, Hard = 3U, Memory = 4U, Bus = 5U, Usage = 6U };
struct CpuFaultReport final {
    CpuFaultKind kind{};
    std::uint16_t attempted{}; // DSO_FAULT_ACTION_*; includes LCD TIM1/DMA stops.
    std::uint16_t failed_readback{};
    bool mmfar_valid{};
    bool bfar_valid{};
    bool stack_frame_unreliable{};
    bool escalated{};
    std::uint32_t mmfar{};
    std::uint32_t bfar{};
};
struct SoftwareTerminalReport final {
    std::uint32_t ipsr{};      // Execution context at the software terminal call.
    std::uint32_t caller_lr{}; // Raw Thumb BL return LR; never an EXC_RETURN.
    std::uint16_t attempted{};
    std::uint16_t failed_readback{};
};
// Interpret an immutable copy obtained while halted. Failure leaves out intact.
// A valid marker is not authentication or a physical quiescence/accuracy claim.
// MSP/PSP are diagnostic values only; neither this API nor the handler reads
// through them. Invalid MMFAR/BFAR contents are not reported as valid addresses.
[[nodiscard]] bool report_cpu_fault(const DsoCpuFaultRecord& record, CpuFaultReport& out) noexcept;
// Interpret an immutable halted snapshot. Failure leaves out unchanged.
[[nodiscard]] bool report_software_terminal(const DsoCpuFaultRecord& record,
                                           SoftwareTerminalReport& out) noexcept;
} // namespace dso
#endif
