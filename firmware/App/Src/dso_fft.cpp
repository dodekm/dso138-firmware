#include "dso_fft.hpp"

#include <cstddef>
#include <cstdint>

namespace dso::dsp {
namespace {

constexpr std::int32_t kInputLimit = 1 << 28;
constexpr std::uint16_t kPhasePeriod = 512U;

constexpr std::int16_t kSineQuarterQ15[129] = {
    0, 402, 804, 1206, 1608, 2009, 2410, 2811,
    3212, 3612, 4011, 4410, 4808, 5205, 5602, 5998,
    6393, 6786, 7179, 7571, 7962, 8351, 8739, 9126,
    9512, 9896, 10278, 10659, 11039, 11417, 11793, 12167,
    12539, 12910, 13279, 13645, 14010, 14372, 14732, 15090,
    15446, 15800, 16151, 16499, 16846, 17189, 17530, 17869,
    18204, 18537, 18868, 19195, 19519, 19841, 20159, 20475,
    20787, 21096, 21403, 21705, 22005, 22301, 22594, 22884,
    23170, 23452, 23731, 24007, 24279, 24547, 24811, 25072,
    25329, 25582, 25832, 26077, 26319, 26556, 26790, 27019,
    27245, 27466, 27683, 27896, 28105, 28310, 28510, 28706,
    28898, 29085, 29268, 29447, 29621, 29791, 29956, 30117,
    30273, 30424, 30571, 30714, 30852, 30985, 31113, 31237,
    31356, 31470, 31580, 31685, 31785, 31880, 31971, 32057,
    32137, 32213, 32285, 32351, 32412, 32469, 32521, 32567,
    32609, 32646, 32678, 32705, 32728, 32745, 32757, 32765,
    32767
};

std::int32_t round_q15_product(std::int64_t value) noexcept
{
    const auto magnitude = static_cast<std::uint64_t>(value < 0 ? -value : value);
    const auto rounded = static_cast<std::int32_t>((magnitude + 16384U) >> 15U);
    return value < 0 ? -rounded : rounded;
}

template < std::uint8_t Shift >
std::int32_t round_power_of_two(std::int32_t value) noexcept
{
    static_assert(Shift > 0U && Shift < 31U, "shift must fit signed 32-bit rounding");
    const auto magnitude = static_cast<std::uint32_t>(value < 0 ? -value : value);
    const auto rounded = static_cast<std::int32_t>(
        (magnitude + (std::uint32_t{1U} << (Shift - 1U))) >> Shift);
    return value < 0 ? -rounded : rounded;
}

std::int32_t multiply_q15(std::int32_t value, std::int16_t coefficient) noexcept
{
    const auto product = static_cast<std::int64_t>(value) *
                         static_cast<std::int64_t>(coefficient);
    return round_q15_product(product);
}

} // namespace

std::int16_t fft_sine_q15(std::uint16_t phase) noexcept
{
    const auto wrapped = static_cast<std::uint16_t>(phase & 511U);
    const auto quadrant = static_cast<std::uint16_t>(wrapped >> 7U);
    const auto offset = static_cast<std::uint16_t>(wrapped & 127U);
    if (quadrant == 0U) { return kSineQuarterQ15[offset]; }
    if (quadrant == 1U) {
        return kSineQuarterQ15[static_cast<std::uint16_t>(128U - offset)];
    }
    if (quadrant == 2U) {
        return static_cast<std::int16_t>(-kSineQuarterQ15[offset]);
    }
    return static_cast<std::int16_t>(
        -kSineQuarterQ15[static_cast<std::uint16_t>(128U - offset)]);
}

std::int16_t fft_cosine_q15(std::uint16_t phase) noexcept
{
    return fft_sine_q15(static_cast<std::uint16_t>(phase + 128U));
}

void RealFft::reset() noexcept
{
    phase_ = FftPhase::Idle;
    work_ = nullptr;
    length_ = 0U;
    complex_length_ = 0U;
    phase_scale_ = 0U;
    validate_index_ = 0U;
    bit_reverse_index_ = 1U;
    bit_reverse_j_ = 0U;
    stage_length_ = 2U;
    stage_phase_stride_ = 0U;
    block_base_ = 0U;
    butterfly_j_ = 0U;
    reconstruct_k_ = 1U;
    nyquist_real_ = 0;
}

bool RealFft::begin(std::uint16_t length, std::int32_t* work,
                    std::uint16_t capacity_words) noexcept
{
    reset();
    const auto required_words = workspace_words(length);
    if (required_words == 0U || work == nullptr || capacity_words < required_words) {
        phase_ = FftPhase::Invalid;
        return false;
    }
    length_ = length;
    complex_length_ = static_cast<std::uint16_t>(length / 2U);
    phase_scale_ = static_cast<std::uint16_t>(kPhasePeriod / length);
    work_ = work;
    phase_ = FftPhase::Validating;
    return true;
}

void RealFft::cancel() noexcept
{
    work_ = nullptr;
    phase_ = FftPhase::Cancelled;
}

FftPhase RealFft::step(std::uint16_t max_operations) noexcept
{
    auto remaining = max_operations;
    while (remaining != 0U &&
           phase_ != FftPhase::Complete &&
           phase_ != FftPhase::Cancelled &&
           phase_ != FftPhase::Invalid &&
           phase_ != FftPhase::Idle) {
        --remaining;
        switch (phase_) {
        case FftPhase::Validating:
            validate_one();
            break;
        case FftPhase::Permuting:
            permute_one();
            break;
        case FftPhase::Transforming:
            butterfly_one();
            break;
        case FftPhase::Reconstructing:
            reconstruct_pair();
            break;
        default:
            break;
        }
    }
    return phase_;
}

bool RealFft::bin(std::uint16_t index, Complex32& result) const noexcept
{
    if (phase_ != FftPhase::Complete || index > complex_length_) { return false; }
    const Complex32 value = index == complex_length_
        ? Complex32{nyquist_real_, 0}
        : load(index);
    result = value;
    return true;
}

Complex32 RealFft::load(std::uint16_t index) const noexcept
{
    const auto offset = static_cast<std::size_t>(index) * 2U;
    return Complex32{work_[offset], work_[offset + 1U]};
}

void RealFft::store(std::uint16_t index, Complex32 value) noexcept
{
    const auto offset = static_cast<std::size_t>(index) * 2U;
    work_[offset] = value.real;
    work_[offset + 1U] = value.imag;
}

void RealFft::validate_one() noexcept
{
    const Complex32 value = load(validate_index_);
    if (value.real < -kInputLimit || value.real > kInputLimit ||
        value.imag < -kInputLimit || value.imag > kInputLimit) {
        work_ = nullptr;
        phase_ = FftPhase::Invalid;
        return;
    }
    ++validate_index_;
    if (validate_index_ == complex_length_) {
        bit_reverse_index_ = 1U;
        bit_reverse_j_ = 0U;
        phase_ = FftPhase::Permuting;
    }
}

void RealFft::permute_one() noexcept
{
    auto bit = static_cast<std::uint16_t>(complex_length_ >> 1U);
    for (; bit != 0U && (bit_reverse_j_ & bit) != 0U; bit >>= 1U) {
        bit_reverse_j_ = static_cast<std::uint16_t>(bit_reverse_j_ ^ bit);
    }
    if (bit != 0U) { bit_reverse_j_ = static_cast<std::uint16_t>(bit_reverse_j_ ^ bit); }
    if (bit_reverse_index_ < bit_reverse_j_) {
        const Complex32 left = load(bit_reverse_index_);
        store(bit_reverse_index_, load(bit_reverse_j_));
        store(bit_reverse_j_, left);
    }
    ++bit_reverse_index_;
    if (bit_reverse_index_ == complex_length_) {
        stage_length_ = 2U;
        stage_phase_stride_ = static_cast<std::uint16_t>(kPhasePeriod / stage_length_);
        block_base_ = 0U;
        butterfly_j_ = 0U;
        phase_ = FftPhase::Transforming;
    }
}

void RealFft::butterfly_one() noexcept
{
    const auto half = static_cast<std::uint16_t>(stage_length_ >> 1U);
    const Complex32 upper = load(static_cast<std::uint16_t>(block_base_ + butterfly_j_));
    const Complex32 lower = load(static_cast<std::uint16_t>(
        block_base_ + butterfly_j_ + half));

    // The proven complex-norm bound keeps the rounded rotation and each sum /
    // difference below 2^30; only each individual 32x16 Q15 product widens.
    std::int32_t rotated_real = lower.real;
    std::int32_t rotated_imag = lower.imag;
    if (butterfly_j_ != 0U) {
        const auto phase = static_cast<std::uint16_t>(butterfly_j_ * stage_phase_stride_);
        const auto cosine = fft_cosine_q15(phase);
        const auto sine = static_cast<std::int16_t>(-fft_sine_q15(phase));
        rotated_real = multiply_q15(lower.real, cosine) -
                       multiply_q15(lower.imag, sine);
        rotated_imag = multiply_q15(lower.real, sine) +
                       multiply_q15(lower.imag, cosine);
    }

    const auto upper_index = static_cast<std::uint16_t>(block_base_ + butterfly_j_);
    const auto lower_index = static_cast<std::uint16_t>(upper_index + half);
    store(upper_index, Complex32{
        round_power_of_two<1U>(upper.real + rotated_real),
        round_power_of_two<1U>(upper.imag + rotated_imag)});
    store(lower_index, Complex32{
        round_power_of_two<1U>(upper.real - rotated_real),
        round_power_of_two<1U>(upper.imag - rotated_imag)});

    ++butterfly_j_;
    if (butterfly_j_ == half) {
        butterfly_j_ = 0U;
        block_base_ = static_cast<std::uint16_t>(block_base_ + stage_length_);
        if (block_base_ == complex_length_) {
            block_base_ = 0U;
            if (stage_length_ == complex_length_) {
                phase_ = FftPhase::Reconstructing;
                reconstruct_k_ = 1U;
            } else {
                stage_length_ = static_cast<std::uint16_t>(stage_length_ << 1U);
                stage_phase_stride_ = static_cast<std::uint16_t>(
                    kPhasePeriod / stage_length_);
            }
        }
    }
}

Complex32 RealFft::reconstruct(std::uint16_t index, Complex32 a,
                               Complex32 b) const noexcept
{
    const auto sum_real = round_power_of_two<2U>(a.real + b.real);
    const auto sum_imag = round_power_of_two<2U>(a.imag + b.imag);
    const auto difference_real = round_power_of_two<2U>(a.real - b.real);
    const auto difference_imag = round_power_of_two<2U>(a.imag - b.imag);
    const auto twiddle_phase = static_cast<std::uint16_t>(index * phase_scale_);
    const auto cosine = fft_cosine_q15(twiddle_phase);
    const auto sine = static_cast<std::int16_t>(-fft_sine_q15(twiddle_phase));
    const auto rotated_real =
        multiply_q15(difference_real, cosine) -
        multiply_q15(difference_imag, sine);
    const auto rotated_imag =
        multiply_q15(difference_real, sine) +
        multiply_q15(difference_imag, cosine);
    return Complex32{
        static_cast<std::int32_t>(sum_real + rotated_imag),
        static_cast<std::int32_t>(sum_imag - rotated_real)};
}

void RealFft::reconstruct_pair() noexcept
{
    if (reconstruct_k_ == 1U) {
        const Complex32 zero = load(0U);
        store(0U, Complex32{
            round_power_of_two<1U>(zero.real + zero.imag),
            0});
        nyquist_real_ = round_power_of_two<1U>(zero.real - zero.imag);
    }

    const auto mirror = static_cast<std::uint16_t>(complex_length_ - reconstruct_k_);
    const Complex32 source = load(reconstruct_k_);
    const Complex32 mirror_source = load(mirror);
    const Complex32 positive = reconstruct(reconstruct_k_, source,
        Complex32{mirror_source.real, -mirror_source.imag});
    Complex32 mirrored = positive;
    if (mirror != reconstruct_k_) {
        mirrored = reconstruct(mirror, mirror_source,
            Complex32{source.real, -source.imag});
    }
    store(reconstruct_k_, positive);
    store(mirror, mirrored);

    if (reconstruct_k_ == complex_length_ / 2U) {
        phase_ = FftPhase::Complete;
    } else {
        ++reconstruct_k_;
    }
}

} // namespace dso::dsp
