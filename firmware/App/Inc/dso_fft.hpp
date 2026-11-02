#ifndef DSO_FFT_HPP
#define DSO_FFT_HPP

#include <cstdint>

namespace dso::dsp {

struct Complex32 final {
    std::int32_t real;
    std::int32_t imag;
};

enum class FftPhase : std::uint8_t {
    Idle,
    Validating,
    Permuting,
    Transforming,
    Reconstructing,
    Complete,
    Cancelled,
    Invalid
};

std::int16_t fft_sine_q15(std::uint16_t phase) noexcept;
std::int16_t fft_cosine_q15(std::uint16_t phase) noexcept;

class RealFft final {
public:
    RealFft() = default;
    RealFft(const RealFft&) = delete;
    RealFft& operator=(const RealFft&) = delete;
    RealFft(RealFft&&) = delete;
    RealFft& operator=(RealFft&&) = delete;

    // Work contains N interleaved complex components of prepared real samples
    // in original order, in caller-selected units. Components must be within
    // +/-2^28. The caller retains exclusive storage ownership until cancel;
    // completion preserves normalized bins in work for bin(). A new begin
    // discards the previous run and releases its prior workspace borrow.
    // Each quarter-table coefficient pair has norm <32768/32768. For a radix-2
    // group of L complex inputs, the /2 Hadamard stages contract vector 2-norm by
    // 1/sqrt(L), so inputs bounded componentwise by A have ideal group norm
    // <=sqrt(2)*A. At L<=256, the accumulated stage-rounding reserve is <78 units,
    // so intermediate complex norm is <sqrt(2)*2^28+78 <2^29. Thus component
    // adds/subtracts used before /2 and /4 stay strictly inside signed 32-bit.
    [[nodiscard]] bool begin(std::uint16_t length, std::int32_t* work,
                             std::uint16_t capacity_words) noexcept;
    [[nodiscard]] FftPhase step(std::uint16_t max_operations) noexcept;
    FftPhase phase() const noexcept { return phase_; }
    std::uint16_t length() const noexcept { return length_; }
    [[nodiscard]] bool bin(std::uint16_t index, Complex32& result) const noexcept;
    void cancel() noexcept;

    static constexpr std::uint16_t workspace_words(std::uint16_t length) noexcept
    {
        return length == 256U || length == 512U ? length : 0U;
    }

private:
    Complex32 load(std::uint16_t index) const noexcept;
    void store(std::uint16_t index, Complex32 value) noexcept;
    void validate_one() noexcept;
    void permute_one() noexcept;
    void butterfly_one() noexcept;
    void reconstruct_pair() noexcept;
    Complex32 reconstruct(std::uint16_t index, Complex32 a,
                          Complex32 b) const noexcept;
    void reset() noexcept;

    FftPhase phase_{FftPhase::Idle};
    std::int32_t* work_{nullptr};
    std::uint16_t length_{0U};
    std::uint16_t complex_length_{0U};
    std::uint16_t phase_scale_{0U};
    std::uint16_t validate_index_{0U};
    std::uint16_t bit_reverse_index_{1U};
    std::uint16_t bit_reverse_j_{0U};
    std::uint16_t stage_length_{2U};
    std::uint16_t stage_phase_stride_{0U};
    std::uint16_t block_base_{0U};
    std::uint16_t butterfly_j_{0U};
    std::uint16_t reconstruct_k_{1U};
    std::int32_t nyquist_real_{0};
};

} // namespace dso::dsp

#endif // DSO_FFT_HPP
