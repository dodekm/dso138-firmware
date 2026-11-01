#include "dso_cpu_fault.h"
#include "dso_fault_report.hpp"
#include <cstddef>

volatile DsoCpuFaultRecord dso_cpu_fault_record{};
static_assert(sizeof(DsoCpuFaultRecord) == DSO_CPU_FAULT_RECORD_BYTES, "Assembly record size");
static_assert(alignof(DsoCpuFaultRecord) >= 4U, "Assembly requires word-aligned record storage");
#define DSO_CHECK_OFFSET(field, offset) static_assert(offsetof(DsoCpuFaultRecord, field) == offset, "Assembly offset: " #field)
DSO_CHECK_OFFSET(magic, 0);
DSO_CHECK_OFFSET(ipsr, DSO_FAULT_IPSR);
DSO_CHECK_OFFSET(exc_return, DSO_FAULT_EXC_RETURN);
DSO_CHECK_OFFSET(msp, DSO_FAULT_MSP);
DSO_CHECK_OFFSET(psp, DSO_FAULT_PSP);
DSO_CHECK_OFFSET(cfsr, DSO_FAULT_CFSR);
DSO_CHECK_OFFSET(hfsr, DSO_FAULT_HFSR);
DSO_CHECK_OFFSET(icsr, DSO_FAULT_ICSR);
DSO_CHECK_OFFSET(shcsr, DSO_FAULT_SHCSR);
DSO_CHECK_OFFSET(mmfar, DSO_FAULT_MMFAR);
DSO_CHECK_OFFSET(bfar, DSO_FAULT_BFAR);
DSO_CHECK_OFFSET(rcc_cir, DSO_FAULT_RCC_CIR);
DSO_CHECK_OFFSET(ahbenr, DSO_FAULT_AHBENR);
DSO_CHECK_OFFSET(apb2enr, DSO_FAULT_APB2ENR);
DSO_CHECK_OFFSET(apb1enr, DSO_FAULT_APB1ENR);
DSO_CHECK_OFFSET(actions, DSO_FAULT_ACTIONS);
#undef DSO_CHECK_OFFSET

namespace {
bool valid_actions(const DsoCpuFaultRecord& record, std::uint16_t& attempted_out,
                   std::uint16_t& failed_out) noexcept
{
    constexpr std::uint32_t ActionBits{0x01ff01ffU};
    const auto attempted = record.actions & 0x1ffU;
    const auto failed = record.actions >> 16U;
    const auto expected = ((record.apb1enr & 1U) != 0U ? 1U : 0U) |
        ((record.apb2enr & 0x200U) != 0U ? 2U : 0U) |
        ((record.apb2enr & 0x400U) != 0U ? 4U : 0U) |
        ((record.ahbenr & 1U) != 0U ? 0x1c8U : 0U) |
        ((record.apb2enr & 0x800U) != 0U ? 0x20U : 0U) |
        ((record.rcc_cir & 0x80U) != 0U ? 16U : 0U);
    if ((record.actions & ~ActionBits) != 0U || attempted != expected || (failed & ~attempted) != 0U) {
        return false;
    }
    attempted_out = static_cast<std::uint16_t>(attempted);
    failed_out = static_cast<std::uint16_t>(failed);
    return true;
}
}

bool dso::report_cpu_fault(const DsoCpuFaultRecord& record, CpuFaultReport& out) noexcept
{
    if (record.magic != DSO_CPU_FAULT_MAGIC || record.ipsr < 2U || record.ipsr > 6U ||
        (record.exc_return != 0xfffffff1U && record.exc_return != 0xfffffff9U && record.exc_return != 0xfffffffdU)) {
        return false;
    }
    std::uint16_t attempted{}, failed{};
    if (!valid_actions(record, attempted, failed)) { return false; }
    const bool mmfar_valid = (record.cfsr & (1U << 7U)) != 0U;
    const bool bfar_valid = (record.cfsr & (1U << 15U)) != 0U;
    out = {static_cast<CpuFaultKind>(record.ipsr), attempted, failed,
        mmfar_valid, bfar_valid, (record.cfsr & 0x1818U) != 0U, (record.hfsr & (1U << 30U)) != 0U,
        mmfar_valid ? record.mmfar : 0U, bfar_valid ? record.bfar : 0U};
    return true;
}

bool dso::report_software_terminal(const DsoCpuFaultRecord& record, SoftwareTerminalReport& out) noexcept
{
    constexpr std::uint32_t MaxIpsr{0x1ffU};
    const auto lr = record.exc_return;
    // Cortex-M exception return tokens occupy the reserved 0xffffffe0..0xffffffff range.
    const bool is_exc_return = (lr & 0xffffffe0U) == 0xffffffe0U;
    if (record.magic != DSO_CPU_SOFTWARE_TERMINAL_MAGIC || record.ipsr > MaxIpsr ||
        (record.ipsr != 0U && record.ipsr < 16U) || lr == 0U || (lr & 1U) == 0U || is_exc_return) {
        return false;
    }
    std::uint16_t attempted{}, failed{};
    if (!valid_actions(record, attempted, failed)) { return false; }
    out = {record.ipsr, lr, attempted, failed};
    return true;
}
