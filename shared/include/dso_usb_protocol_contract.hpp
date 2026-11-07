#ifndef DSO_USB_PROTOCOL_CONTRACT_HPP
#define DSO_USB_PROTOCOL_CONTRACT_HPP

#include "dso_capture_contract.hpp"

#include <cstdint>

namespace dso::usb_protocol_contract {

// Values shared by the STM32 endpoint and Studio's protocol-v1 codec.
inline constexpr std::uint8_t Version{1U};
inline constexpr std::uint16_t CaptureSampleCapacity{capture_contract::MaximumSamples};
inline constexpr std::uint8_t MagicFirst{0x44U};
inline constexpr std::uint8_t MagicSecond{0x53U};
inline constexpr std::uint8_t MagicFirstOffset{0U};
inline constexpr std::uint8_t MagicSecondOffset{1U};
inline constexpr std::uint8_t VersionOffset{2U};
inline constexpr std::uint8_t OpcodeOffset{3U};
inline constexpr std::uint8_t RequestIdOffset{4U};
inline constexpr std::uint8_t PayloadSizeOffset{6U};
inline constexpr std::uint8_t CrcOffset{8U};
inline constexpr std::uint8_t CrcHeaderBytes{8U};
inline constexpr std::uint8_t HeaderBytes{12U};
inline constexpr std::uint8_t MagicBytes{2U};
inline constexpr std::uint8_t PayloadOffset{HeaderBytes};
inline constexpr std::uint8_t StatusBytes{1U};
inline constexpr std::uint8_t ResponseBodyOffset{PayloadOffset + StatusBytes};
inline constexpr std::uint8_t MaximumPayloadBytes{52U};
inline constexpr std::uint8_t MaximumFrameBytes{64U};
inline constexpr std::uint8_t MaximumResponseBody{
    static_cast<std::uint8_t>(MaximumPayloadBytes - StatusBytes)};
inline constexpr std::uint8_t MaximumSamplesPerRead{22U};

enum class Opcode : std::uint8_t {
    Info = 1U,
    Status = 2U,
    BeginFrame = 3U,
    ReadSamples = 4U,
    EndFrame = 5U,
    CaptureSingle = 6U,
    CancelCapture = 7U,
    GetSettings = 8U,
    SetSettings = 9U,
    CaptureSelected = 10U,
    BeginFrameExtended = 11U,
    SetTrigger = 12U,
    HoldLocalRun = 13U
};

enum class Status : std::uint8_t {
    Ok = 0U,
    Invalid = 1U,
    Busy = 2U,
    Stale = 3U,
    Unsupported = 4U,
    Timeout = 5U,
    Cancelled = 6U,
    Fault = 7U
};

namespace info {
inline constexpr std::uint8_t ProtocolVersionOffset{0U};
inline constexpr std::uint8_t CaptureCapacityOffset{1U};
inline constexpr std::uint8_t RawAdcResolutionBitsOffset{3U};
inline constexpr std::uint8_t MaximumSamplesPerReadOffset{4U};
inline constexpr std::uint8_t ReservedOffset{5U};
inline constexpr std::uint8_t BodyBytes{7U};
inline constexpr std::uint8_t ReplyProtocolVersionOffset{StatusBytes + ProtocolVersionOffset};
inline constexpr std::uint8_t ReplyCaptureCapacityOffset{StatusBytes + CaptureCapacityOffset};
inline constexpr std::uint8_t ReplyRawAdcResolutionBitsOffset{
    StatusBytes + RawAdcResolutionBitsOffset};
inline constexpr std::uint8_t ReplyMaximumSamplesPerReadOffset{
    StatusBytes + MaximumSamplesPerReadOffset};
inline constexpr std::uint8_t ReplyReservedOffset{StatusBytes + ReservedOffset};
inline constexpr std::uint8_t ReplyPayloadBytes{StatusBytes + BodyBytes};

static_assert(ProtocolVersionOffset == 0U && CaptureCapacityOffset == ProtocolVersionOffset + 1U &&
                  RawAdcResolutionBitsOffset == CaptureCapacityOffset + 2U &&
                  MaximumSamplesPerReadOffset == RawAdcResolutionBitsOffset + 1U &&
                  ReservedOffset == MaximumSamplesPerReadOffset + 1U &&
                  BodyBytes == ReservedOffset + 2U &&
                  ReplyProtocolVersionOffset == StatusBytes + ProtocolVersionOffset &&
                  ReplyCaptureCapacityOffset == StatusBytes + CaptureCapacityOffset &&
                  ReplyRawAdcResolutionBitsOffset == StatusBytes + RawAdcResolutionBitsOffset &&
                  ReplyMaximumSamplesPerReadOffset == StatusBytes + MaximumSamplesPerReadOffset &&
                  ReplyReservedOffset == StatusBytes + ReservedOffset &&
                  ReplyPayloadBytes == StatusBytes + BodyBytes,
              "INFO request/reply layouts must match protocol v1");
} // namespace info

namespace status_request {
inline constexpr std::uint8_t JobIdOffset{0U};
inline constexpr std::uint8_t PayloadBytes{4U};

static_assert(JobIdOffset == 0U && PayloadBytes == JobIdOffset + 4U,
              "STATUS request layout must match protocol v1");
} // namespace status_request

namespace status_body {
inline constexpr std::uint8_t UiStateOffset{0U};
inline constexpr std::uint8_t ControllerFaultOffset{1U};
inline constexpr std::uint8_t WorkOffset{2U};
inline constexpr std::uint8_t HostStateOffset{3U};
inline constexpr std::uint8_t JobIdOffset{4U};
inline constexpr std::uint8_t FrameAvailableOffset{8U};
inline constexpr std::uint8_t FrameCountOffset{9U};
inline constexpr std::uint8_t StorageBusyOffset{11U};
inline constexpr std::uint8_t BodyBytes{12U};

static_assert(UiStateOffset == 0U && ControllerFaultOffset == UiStateOffset + 1U &&
                  WorkOffset == ControllerFaultOffset + 1U &&
                  HostStateOffset == WorkOffset + 1U && JobIdOffset == HostStateOffset + 1U &&
                  FrameAvailableOffset == JobIdOffset + 4U &&
                  FrameCountOffset == FrameAvailableOffset + 1U &&
                  StorageBusyOffset == FrameCountOffset + 2U && BodyBytes == StorageBusyOffset + 1U,
              "STATUS reply body layout must match protocol v1");
} // namespace status_body

namespace status_reply {
inline constexpr std::uint8_t UiStateOffset{StatusBytes + status_body::UiStateOffset};
inline constexpr std::uint8_t ControllerFaultOffset{StatusBytes + status_body::ControllerFaultOffset};
inline constexpr std::uint8_t WorkOffset{StatusBytes + status_body::WorkOffset};
inline constexpr std::uint8_t HostStateOffset{StatusBytes + status_body::HostStateOffset};
inline constexpr std::uint8_t JobIdOffset{StatusBytes + status_body::JobIdOffset};
inline constexpr std::uint8_t FrameAvailableOffset{StatusBytes + status_body::FrameAvailableOffset};
inline constexpr std::uint8_t FrameCountOffset{StatusBytes + status_body::FrameCountOffset};
inline constexpr std::uint8_t StorageBusyOffset{StatusBytes + status_body::StorageBusyOffset};
inline constexpr std::uint8_t PayloadBytes{StatusBytes + status_body::BodyBytes};

static_assert(UiStateOffset == StatusBytes + status_body::UiStateOffset &&
                  ControllerFaultOffset == StatusBytes + status_body::ControllerFaultOffset &&
                  WorkOffset == StatusBytes + status_body::WorkOffset &&
                  HostStateOffset == StatusBytes + status_body::HostStateOffset &&
                  JobIdOffset == StatusBytes + status_body::JobIdOffset &&
                  FrameAvailableOffset == StatusBytes + status_body::FrameAvailableOffset &&
                  FrameCountOffset == StatusBytes + status_body::FrameCountOffset &&
                  StorageBusyOffset == StatusBytes + status_body::StorageBusyOffset &&
                  PayloadBytes == StatusBytes + status_body::BodyBytes,
              "STATUS reply payload layout must match protocol v1");
} // namespace status_reply

namespace capture_job {
inline constexpr std::uint8_t TimeoutMillisecondsOffset{0U};
inline constexpr std::uint8_t RequestPayloadBytes{4U};
inline constexpr std::uint8_t JobIdOffset{0U};
inline constexpr std::uint8_t ResponseBodyBytes{4U};
inline constexpr std::uint8_t ReplyJobIdOffset{StatusBytes + JobIdOffset};
inline constexpr std::uint8_t ReplyPayloadBytes{StatusBytes + ResponseBodyBytes};

static_assert(TimeoutMillisecondsOffset == 0U && RequestPayloadBytes == 4U &&
                  JobIdOffset == 0U && ResponseBodyBytes == 4U &&
                  ReplyJobIdOffset == StatusBytes + JobIdOffset &&
                  ReplyPayloadBytes == StatusBytes + ResponseBodyBytes &&
                  ReplyPayloadBytes == 5U,
              "Capture request and acknowledgement layouts must match protocol v1");
} // namespace capture_job

namespace settings_request {
inline constexpr std::uint8_t RevisionOffset{0U};
inline constexpr std::uint8_t RevisionBytes{2U};

namespace set_settings {
inline constexpr std::uint8_t TimebaseIndexOffset{RevisionOffset + RevisionBytes};
inline constexpr std::uint8_t TriggerModeOffset{TimebaseIndexOffset + 1U};
inline constexpr std::uint8_t PayloadBytes{TriggerModeOffset + 1U};

static_assert(TimebaseIndexOffset == 2U && TriggerModeOffset == 3U && PayloadBytes == 4U,
              "SET_SETTINGS request layout must match protocol v1");
} // namespace set_settings

namespace set_trigger {
inline constexpr std::uint8_t EdgeOffset{RevisionOffset + RevisionBytes};
inline constexpr std::uint8_t LevelOffset{EdgeOffset + 1U};
inline constexpr std::uint8_t PositionOffset{LevelOffset + 2U};
inline constexpr std::uint8_t PayloadBytes{PositionOffset + 2U};

static_assert(EdgeOffset == 2U && LevelOffset == 3U && PositionOffset == 5U &&
                  PayloadBytes == 7U,
              "SET_TRIGGER request layout must match protocol v1");
} // namespace set_trigger

namespace hold_local_run {
inline constexpr std::uint8_t PayloadBytes{RevisionBytes};

static_assert(PayloadBytes == 2U, "HOLD_LOCAL_RUN request layout must match protocol v1");
} // namespace hold_local_run

static_assert(RevisionOffset == 0U && RevisionBytes == 2U,
              "Settings write requests begin with a 16-bit revision");
} // namespace settings_request

namespace settings_writable {
inline constexpr std::uint8_t Timebase{0x01U};
inline constexpr std::uint8_t TriggerMode{0x02U};
inline constexpr std::uint8_t TriggerEdge{0x04U};
inline constexpr std::uint8_t TriggerLevel{0x08U};
inline constexpr std::uint8_t TriggerPosition{0x10U};
inline constexpr std::uint8_t LocalRunPause{0x20U};
inline constexpr std::uint8_t TriggerFields{TriggerEdge | TriggerLevel | TriggerPosition};
inline constexpr std::uint8_t SupportedMask{
    Timebase | TriggerMode | TriggerEdge | TriggerLevel | TriggerPosition | LocalRunPause};
inline constexpr std::uint8_t ReservedMask{0xc0U};

static_assert(SupportedMask == 0x3fU && (SupportedMask & ReservedMask) == 0U &&
                  (SupportedMask | ReservedMask) == 0xffU,
              "GET_SETTINGS writable capabilities must retain their protocol-v1 bits");
} // namespace settings_writable

namespace settings_flags {
inline constexpr std::uint8_t RunIntent{0x01U};
inline constexpr std::uint8_t PhysicalSettingsQualified{0x02U};
inline constexpr std::uint8_t WriteAdmissibleNow{0x04U};
inline constexpr std::uint8_t ExtendedCapture{0x08U};
inline constexpr std::uint8_t LocalRunPauseAdmissibleNow{0x10U};
inline constexpr std::uint8_t ReservedMask{0xe0U};
inline constexpr std::uint8_t PhysicalAndWriteReady{
    PhysicalSettingsQualified | WriteAdmissibleNow};
inline constexpr std::uint8_t LiveCaptureRequirements{
    PhysicalAndWriteReady | ExtendedCapture};
} // namespace settings_flags

namespace settings_body {
inline constexpr std::uint8_t SchemaOffset{0U};
inline constexpr std::uint8_t WritableMaskOffset{1U};
inline constexpr std::uint8_t UiStateOffset{2U};
inline constexpr std::uint8_t FlagsOffset{3U};
inline constexpr std::uint8_t TimebaseIndexOffset{4U};
inline constexpr std::uint8_t CatalogueCountOffset{5U};
inline constexpr std::uint8_t TriggerModeOffset{6U};
inline constexpr std::uint8_t TriggerEdgeOffset{7U};
inline constexpr std::uint8_t TriggerLevelOffset{8U};
inline constexpr std::uint8_t TriggerPositionOffset{10U};
inline constexpr std::uint8_t MicrovoltsPerDivisionOffset{12U};
inline constexpr std::uint8_t TimerHzOffset{16U};
inline constexpr std::uint8_t TimerDividerOffset{20U};
inline constexpr std::uint8_t CouplingOffset{24U};
inline constexpr std::uint8_t CatalogueIdOffset{25U};
inline constexpr std::uint8_t RevisionOffset{26U};
inline constexpr std::uint8_t TimebaseMicrosecondsPerDivisionOffset{28U};
inline constexpr std::uint8_t BodyBytes{32U};
inline constexpr std::uint8_t BodyInResponseOffset{StatusBytes};
inline constexpr std::uint8_t ResponseBytes{BodyInResponseOffset + BodyBytes};

static_assert(SchemaOffset == 0U && WritableMaskOffset == SchemaOffset + 1U &&
                  UiStateOffset == WritableMaskOffset + 1U && FlagsOffset == UiStateOffset + 1U &&
                  TimebaseIndexOffset == FlagsOffset + 1U &&
                  CatalogueCountOffset == TimebaseIndexOffset + 1U &&
                  TriggerModeOffset == CatalogueCountOffset + 1U &&
                  TriggerEdgeOffset == TriggerModeOffset + 1U &&
                  TriggerLevelOffset == TriggerEdgeOffset + 1U &&
                  TriggerPositionOffset == TriggerLevelOffset + 2U &&
                  MicrovoltsPerDivisionOffset == TriggerPositionOffset + 2U &&
                  TimerHzOffset == MicrovoltsPerDivisionOffset + 4U &&
                  TimerDividerOffset == TimerHzOffset + 4U &&
                  CouplingOffset == TimerDividerOffset + 4U &&
                  CatalogueIdOffset == CouplingOffset + 1U &&
                  RevisionOffset == CatalogueIdOffset + 1U &&
                  TimebaseMicrosecondsPerDivisionOffset == RevisionOffset + 2U &&
                  BodyBytes == TimebaseMicrosecondsPerDivisionOffset + 4U &&
                  BodyInResponseOffset == StatusBytes &&
                  ResponseBytes == BodyInResponseOffset + BodyBytes,
              "GET_SETTINGS response body layout must match protocol v1");
} // namespace settings_body

namespace frame_metadata_body {
inline constexpr std::uint8_t TokenOffset{0U};
inline constexpr std::uint8_t SampleCountOffset{4U};
inline constexpr std::uint8_t PretriggerOffset{6U};
inline constexpr std::uint8_t TimerHzOffset{8U};
inline constexpr std::uint8_t TimerDividerOffset{12U};
inline constexpr std::uint8_t AcquisitionEpochOffset{16U};
inline constexpr std::uint8_t TriggerSequenceOffset{20U};
inline constexpr std::uint8_t PhysicalGenerationOffset{24U};
inline constexpr std::uint8_t MicrovoltsPerDivisionOffset{28U};
inline constexpr std::uint8_t ZeroCodeQ16Offset{32U};
inline constexpr std::uint8_t GainQ16Offset{36U};
inline constexpr std::uint8_t GainShiftOffset{40U};
inline constexpr std::uint8_t CalibrationKindOffset{41U};
inline constexpr std::uint8_t CouplingOffset{42U};
inline constexpr std::uint8_t TriggerModeOffset{43U};
inline constexpr std::uint8_t TriggerEdgeOffset{44U};
inline constexpr std::uint8_t FlagsOffset{45U};
inline constexpr std::uint8_t ReservedOffset{46U};
inline constexpr std::uint8_t BaseBodyBytes{47U};
inline constexpr std::uint8_t ExtendedTimebaseIndexOffset{47U};
inline constexpr std::uint8_t ExtendedTriggerLevelOffset{48U};
inline constexpr std::uint8_t ExtendedReservedOffset{50U};
inline constexpr std::uint8_t ExtendedBodyBytes{51U};
inline constexpr std::uint8_t BodyInResponseOffset{StatusBytes};
inline constexpr std::uint8_t BaseResponseBytes{BodyInResponseOffset + BaseBodyBytes};
inline constexpr std::uint8_t ExtendedTimebaseIndexResponseOffset{
    BodyInResponseOffset + ExtendedTimebaseIndexOffset};
inline constexpr std::uint8_t ExtendedTriggerLevelResponseOffset{
    BodyInResponseOffset + ExtendedTriggerLevelOffset};
inline constexpr std::uint8_t ExtendedReservedResponseOffset{
    BodyInResponseOffset + ExtendedReservedOffset};
inline constexpr std::uint8_t ExtendedResponseBytes{
    BodyInResponseOffset + ExtendedBodyBytes};

static_assert(TokenOffset == 0U && SampleCountOffset == TokenOffset + 4U &&
                  PretriggerOffset == SampleCountOffset + 2U &&
                  TimerHzOffset == PretriggerOffset + 2U &&
                  TimerDividerOffset == TimerHzOffset + 4U &&
                  AcquisitionEpochOffset == TimerDividerOffset + 4U &&
                  TriggerSequenceOffset == AcquisitionEpochOffset + 4U &&
                  PhysicalGenerationOffset == TriggerSequenceOffset + 4U &&
                  MicrovoltsPerDivisionOffset == PhysicalGenerationOffset + 4U &&
                  ZeroCodeQ16Offset == MicrovoltsPerDivisionOffset + 4U &&
                  GainQ16Offset == ZeroCodeQ16Offset + 4U &&
                  GainShiftOffset == GainQ16Offset + 4U &&
                  CalibrationKindOffset == GainShiftOffset + 1U &&
                  CouplingOffset == CalibrationKindOffset + 1U &&
                  TriggerModeOffset == CouplingOffset + 1U &&
                  TriggerEdgeOffset == TriggerModeOffset + 1U &&
                  FlagsOffset == TriggerEdgeOffset + 1U &&
                  ReservedOffset == FlagsOffset + 1U &&
                  BaseBodyBytes == ReservedOffset + 1U &&
                  ExtendedTimebaseIndexOffset == BaseBodyBytes &&
                  ExtendedTriggerLevelOffset == ExtendedTimebaseIndexOffset + 1U &&
                  ExtendedReservedOffset == ExtendedTriggerLevelOffset + 2U &&
                  ExtendedBodyBytes == ExtendedReservedOffset + 1U &&
                  BodyInResponseOffset == StatusBytes &&
                  BaseResponseBytes == BodyInResponseOffset + BaseBodyBytes &&
                  ExtendedTimebaseIndexResponseOffset ==
                      BodyInResponseOffset + ExtendedTimebaseIndexOffset &&
                  ExtendedTriggerLevelResponseOffset ==
                      BodyInResponseOffset + ExtendedTriggerLevelOffset &&
                  ExtendedReservedResponseOffset == BodyInResponseOffset + ExtendedReservedOffset &&
                  ExtendedResponseBytes == BodyInResponseOffset + ExtendedBodyBytes,
              "Capture metadata response layouts must match protocol v1");
} // namespace frame_metadata_body

namespace read_samples {
inline constexpr std::uint8_t RequestTokenOffset{0U};
inline constexpr std::uint8_t RequestSampleOffset{4U};
inline constexpr std::uint8_t RequestCountOffset{6U};
inline constexpr std::uint8_t RequestBytes{7U};

inline constexpr std::uint8_t ResponseTokenOffset{0U};
inline constexpr std::uint8_t ResponseSampleOffset{4U};
inline constexpr std::uint8_t ResponseCountOffset{6U};
inline constexpr std::uint8_t ResponseSamplesOffset{7U};
inline constexpr std::uint8_t ResponsePrefixBytes{ResponseSamplesOffset};
inline constexpr std::uint8_t SampleBytes{2U};
inline constexpr std::uint8_t ReplyTokenOffset{StatusBytes + ResponseTokenOffset};
inline constexpr std::uint8_t ReplySampleOffset{StatusBytes + ResponseSampleOffset};
inline constexpr std::uint8_t ReplyCountOffset{StatusBytes + ResponseCountOffset};
inline constexpr std::uint8_t ReplySamplesOffset{StatusBytes + ResponseSamplesOffset};
inline constexpr std::uint8_t MinimumReplyBytes{StatusBytes + ResponsePrefixBytes};

// The status, chunk prefix and maximum sample batch fill one protocol payload.
static_assert(MinimumReplyBytes + MaximumSamplesPerRead * SampleBytes == MaximumPayloadBytes,
              "READ_SAMPLES maximum batch must fit exactly in protocol v1");

static_assert(RequestTokenOffset == 0U && RequestSampleOffset == RequestTokenOffset + 4U &&
                  RequestCountOffset == RequestSampleOffset + 2U &&
                  RequestBytes == RequestCountOffset + 1U && ResponseTokenOffset == 0U &&
                  ResponseSampleOffset == ResponseTokenOffset + 4U &&
                  ResponseCountOffset == ResponseSampleOffset + 2U &&
                  ResponseSamplesOffset == ResponseCountOffset + 1U &&
                  ResponsePrefixBytes == ResponseSamplesOffset &&
                  ReplyTokenOffset == StatusBytes + ResponseTokenOffset &&
                  ReplySampleOffset == StatusBytes + ResponseSampleOffset &&
                  ReplyCountOffset == StatusBytes + ResponseCountOffset &&
                  ReplySamplesOffset == StatusBytes + ResponseSamplesOffset &&
                  MinimumReplyBytes == StatusBytes + ResponsePrefixBytes,
              "READ_SAMPLES request and response layouts must match protocol v1");
} // namespace read_samples

static_assert(MagicFirstOffset == 0U && MagicSecondOffset == MagicFirstOffset + 1U &&
                  MagicBytes == MagicSecondOffset + 1U && VersionOffset == MagicBytes &&
                  OpcodeOffset == VersionOffset + 1U && RequestIdOffset == OpcodeOffset + 1U &&
                  PayloadSizeOffset == RequestIdOffset + 2U && CrcOffset == PayloadSizeOffset + 2U &&
                  CrcHeaderBytes == CrcOffset && HeaderBytes == CrcOffset + 4U &&
                  PayloadOffset == HeaderBytes && StatusBytes == 1U &&
                  ResponseBodyOffset == PayloadOffset + StatusBytes &&
                  MaximumResponseBody + StatusBytes == MaximumPayloadBytes &&
                  HeaderBytes + MaximumPayloadBytes == MaximumFrameBytes,
              "USB outer-frame offsets and bounds must match protocol v1");

} // namespace dso::usb_protocol_contract

#endif // DSO_USB_PROTOCOL_CONTRACT_HPP
