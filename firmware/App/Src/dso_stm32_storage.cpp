#include "dso_stm32_storage.hpp"
#include "dso_board_bringup.hpp"
#include "dso_stm32_shared_guard.hpp"
#include "main.h"

extern "C" {
extern std::uint8_t __dso_storage_ram_start;
extern std::uint8_t __dso_storage_ram_end;

// The entire busy interval, including literal pools and terminal stop, belongs
// to this copied SRAM section. No call or Flash data access is allowed here.
__attribute__((section(".RamFunc.dso_storage"), noinline, noclone, used))
std::uint32_t dso_storage_busy_kernel(std::uintptr_t address, std::uint32_t value,
                                    bool erase, volatile dso::Stm32StorageFault* fault) noexcept
{
    const auto began = DWT->CYCCNT;
    const std::uint32_t deadline = erase ? 7200000U : 72000U;
    if (erase) {
        FLASH->CR = FLASH_CR_PER;
        FLASH->AR = static_cast<std::uint32_t>(address);
        FLASH->CR = FLASH_CR_PER | FLASH_CR_STRT;
    } else {
        FLASH->CR = FLASH_CR_PG;
        *reinterpret_cast<volatile std::uint16_t*>(address) = static_cast<std::uint16_t>(value);
    }
    __DSB();
    // ES096: allow the page-erase STRT -> BSY assertion delay before polling.
    __NOP(); __NOP();
    // Every iteration consumes at least one qualified core cycle. This bound
    // cannot precede the cycle deadline, and also handles a stopped counter.
    for (std::uint32_t remaining{deadline}; remaining != 0U; --remaining) {
        const auto status = FLASH->SR;
        if ((status & FLASH_SR_BSY) == 0U) {
            FLASH->CR = FLASH_CR_LOCK;
            __DSB();
            return status;
        }
        if (static_cast<std::uint32_t>(DWT->CYCCNT - began) >= deadline) { break; }
    }
    // Busy Flash cannot supply the caller/exception vectors. Never return,
    // write CR/option bytes, unmask interrupts or claim that cancel is possible.
    *fault = dso::Stm32StorageFault::BusyTimeout;
    for (;;) { __NOP(); }
}
}

namespace dso {
namespace {
constexpr std::uintptr_t RegionBegin{0x0800fc00U};
static_assert(RegionBegin + StorageRegionBytes == 0x08010000U,
              "Storage must occupy only the final 1 KiB page of Flash");
constexpr std::uintptr_t RamBegin{0x20000000U}, RamEnd{0x20005000U};
constexpr std::uint32_t HsiReady{RCC_CR_HSION | RCC_CR_HSIRDY};
constexpr std::uint32_t OwnedFlags{FLASH_SR_EOP | FLASH_SR_PGERR | FLASH_SR_WRPRTERR};
struct Binding final {
    volatile Stm32StorageFault fault{Stm32StorageFault::None};
    bool qualified{};
};
Binding binding;
static_assert(sizeof(Binding) <= 12U, "Keep storage port state within C8 SRAM budget");

bool fail(Stm32StorageFault fault) noexcept
{ binding.qualified = false; binding.fault = fault; return false; }


bool permitted(void* context) noexcept
{
    if (!binding.qualified || binding.fault != Stm32StorageFault::None) { return false; }
    if (context != &binding) { return fail(Stm32StorageFault::InvalidArgument); }
    return stm32_foreground() || fail(Stm32StorageFault::Execution);
}

bool ready_hardware() noexcept
{
    if (HAL_RCC_GetSysClockFreq() != 72000000U || HAL_RCC_GetHCLKFreq() != 72000000U ||
        (RCC->CR & HsiReady) != HsiReady) { return fail(Stm32StorageFault::Clocks); }
    if (!stm32_stopped_storage_producers() || FLASH->CR != FLASH_CR_LOCK || FLASH->SR != 0U) {
        return fail(Stm32StorageFault::Ownership);
    }
    return true;
}

bool counter_progress() noexcept
{
    if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0U || (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U) { return false; }
    const auto start = DWT->CYCCNT;
    for (unsigned i{}; i < 64U; ++i) {
        const auto elapsed = static_cast<std::uint32_t>(DWT->CYCCNT - start);
        if (elapsed != 0U && elapsed < 72000U) { return true; }
        __NOP();
    }
    return false;
}

bool prepare(std::uint64_t board_identity, Stm32StorageQualification q) noexcept
{
    binding.qualified = false;
    if (!q.silicon_verified || !q.recovery_verified || !q.supply_verified ||
        !q.exclusive_fpec_verified || !q.interruption_policy_verified) {
        binding.fault = Stm32StorageFault::Qualification; return false;
    }
    if (board_identity == 0U) { return fail(Stm32StorageFault::Identity); }
    if (!stm32_foreground()) { return fail(Stm32StorageFault::Execution); }
    if (!ready_hardware()) { return false; }
    if (board_identity != BringupBoardIdentity || !bringup_identity_matches()) {
        return fail(Stm32StorageFault::Identity);
    }
    if ((FLASH->WRPR & 0x8000U) != 0x8000U) { return fail(Stm32StorageFault::Protection); }
    const auto begin = reinterpret_cast<std::uintptr_t>(&__dso_storage_ram_start);
    const auto end = reinterpret_cast<std::uintptr_t>(&__dso_storage_ram_end);
    const auto kernel = reinterpret_cast<std::uintptr_t>(&dso_storage_busy_kernel) & ~std::uintptr_t{1U};
    if (begin < RamBegin || end <= begin || end > RamEnd || kernel < begin || kernel >= end) {
        return fail(Stm32StorageFault::Ownership);
    }
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    if (!counter_progress()) { return fail(Stm32StorageFault::Clocks); }
    binding.fault = Stm32StorageFault::None; binding.qualified = true; return true;
}

bool output_valid(std::uint8_t* output, std::uint16_t count) noexcept
{
    const auto address = reinterpret_cast<std::uintptr_t>(output);
    if (address < RamBegin || address > RamEnd || count > RamEnd - address) { return false; }
    const auto begin = reinterpret_cast<std::uintptr_t>(&binding);
    const auto end = begin + sizeof(binding);
    const auto code_begin = reinterpret_cast<std::uintptr_t>(&__dso_storage_ram_start);
    const auto code_end = reinterpret_cast<std::uintptr_t>(&__dso_storage_ram_end);
    return count == 0U || ((address >= end || address + count <= begin) &&
        (address >= code_end || address + count <= code_begin));
}

bool read(void* context, std::uint32_t offset, std::uint8_t* output, std::uint16_t count) noexcept
{
    if (!permitted(context)) { return false; }
    if (offset > StorageRegionBytes || count > StorageRegionBytes - offset || !output_valid(output, count)) {
        return fail(Stm32StorageFault::InvalidArgument);
    }
    if (!ready_hardware()) { return false; }
    if (!counter_progress()) { return fail(Stm32StorageFault::Clocks); }
    for (std::uint16_t i{}; i < count;) {
        const auto address = RegionBegin + offset + i;
        const auto value = *reinterpret_cast<volatile const std::uint16_t*>(address & ~std::uintptr_t{1U});
        output[i++] = static_cast<std::uint8_t>((address & 1U) != 0U ? value >> 8U : value);
        if ((address & 1U) == 0U && i < count) { output[i++] = static_cast<std::uint8_t>(value >> 8U); }
    }
    return true;
}

bool change(std::uintptr_t address, std::uint16_t value, bool erase) noexcept
{
    if (!ready_hardware()) { return false; }
    if (!counter_progress()) { return fail(Stm32StorageFault::Clocks); }
    const auto mask = __get_PRIMASK();
    __disable_irq();
    // An interrupt could have restarted a producer after the first preflight.
    if (!ready_hardware() || (!erase && *reinterpret_cast<volatile const std::uint16_t*>(address) != 0xffffU)) {
        if (binding.qualified) { static_cast<void>(fail(Stm32StorageFault::Program)); }
        __set_PRIMASK(mask); return false;
    }
    FLASH->KEYR = FLASH_KEY1;
    FLASH->KEYR = FLASH_KEY2;
    if (FLASH->CR != 0U) {
        static_cast<void>(fail(Stm32StorageFault::Ownership)); __set_PRIMASK(mask); return false;
    }
    const auto status = dso_storage_busy_kernel(address, value, erase, &binding.fault);
    const auto flags = status & OwnedFlags;
    if (flags != 0U) { FLASH->SR = flags; }
    __DSB();
    const bool success = status == FLASH_SR_EOP && FLASH->SR == 0U && FLASH->CR == FLASH_CR_LOCK;
    if (!success) { static_cast<void>(fail(Stm32StorageFault::Program)); }
    __set_PRIMASK(mask);
    if (!success) { return false; }
    const auto words = erase ? 512U : 1U;
    const auto expected = erase ? std::uint16_t{0xffffU} : value;
    const auto* array = reinterpret_cast<volatile const std::uint16_t*>(address);
    for (unsigned i{}; i < words; ++i) {
        if (array[i] != expected) { return fail(Stm32StorageFault::Verification); }
    }
    return true;
}

bool program(void* context, std::uint32_t offset, std::uint16_t value) noexcept
{
    if (!permitted(context)) { return false; }
    if ((offset & 1U) != 0U || offset >= StorageRegionBytes) { return fail(Stm32StorageFault::InvalidArgument); }
    return change(RegionBegin + offset, value, false);
}

bool erase(void* context, std::uint8_t page) noexcept
{
    if (!permitted(context)) { return false; }
    if (page >= StorageRegionBytes / StoragePageBytes) { return fail(Stm32StorageFault::InvalidArgument); }
    return change(RegionBegin + std::uint32_t{page} * StoragePageBytes, 0U, true);
}

StoragePort port() noexcept { return {&binding, read, program, erase}; }
Stm32StorageFault fault() noexcept { return binding.fault; }
Stm32StorageState state() noexcept
{
    const auto value = binding.fault;
    return value == Stm32StorageFault::None ? (binding.qualified ? Stm32StorageState::Ready : Stm32StorageState::Dormant) :
        value == Stm32StorageFault::Qualification ? Stm32StorageState::Dormant : Stm32StorageState::Fault;
}
const Stm32StorageApi Api{prepare, port, state, fault};
} // namespace
const Stm32StorageApi& stm32_storage_api() noexcept { return Api; }
} // namespace dso
