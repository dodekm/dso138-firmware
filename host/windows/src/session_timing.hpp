#pragma once

namespace dso::session_timing
{

inline constexpr int kExchangeTimerPollIntervalMs = 10;
inline constexpr int kExchangeRetryIntervalMs = 250; // Delay between attempts for one exchange.
inline constexpr int kMaximumExchangeRetransmissions = 2; // Retries after the initial send.
inline constexpr int kCaptureReadinessMs = 1000;
inline constexpr int kCaptureCompletionGraceMs = 1500; // Allow device work beyond the host timeout.
inline constexpr int kStatusPollMs = 20;
inline constexpr int kSettingsPollMs = 350;
inline constexpr int kLiveSettingsPollMs = 500;
inline constexpr int kLiveStartBusyRetryDelayMs = 150;
inline constexpr int kFrameAvailabilityRetryMs = 15; // Retry BEGIN_FRAME while its record is pending.
inline constexpr int kLiveReadinessPollMs = 100; // Delay between post-Busy settings readbacks.
inline constexpr int kMaximumLiveReadinessReads = 7; // Total reads before abandoning the retry.

} // namespace dso::session_timing
