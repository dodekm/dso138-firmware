#include "dso_usb_cdc.hpp"

namespace dso {
namespace {
constexpr std::uint8_t DeviceDescriptorType{1U};
constexpr std::uint8_t ConfigurationDescriptorType{2U};
constexpr std::uint8_t StringDescriptorType{3U};
constexpr std::uint8_t RequestGetStatus{0U};
constexpr std::uint8_t RequestClearFeature{1U};
constexpr std::uint8_t RequestSetFeature{3U};
constexpr std::uint8_t RequestSetAddress{5U};
constexpr std::uint8_t RequestGetDescriptor{6U};
constexpr std::uint8_t RequestGetConfiguration{8U};
constexpr std::uint8_t RequestSetConfiguration{9U};
constexpr std::uint8_t RequestGetInterface{10U};
constexpr std::uint8_t RequestSetInterface{11U};
constexpr std::uint8_t CdcSetLineCoding{0x20U};
constexpr std::uint8_t CdcGetLineCoding{0x21U};
constexpr std::uint8_t CdcSetControlLineState{0x22U};
constexpr std::uint8_t CdcSendBreak{0x23U};
constexpr std::uint8_t FeatureEndpointHalt{0U};
constexpr std::uint8_t Ep0Out{0x00U};
constexpr std::uint8_t Ep0In{0x80U};
constexpr std::uint8_t Ep1Out{0x01U};
constexpr std::uint8_t Ep1In{0x81U};
constexpr std::uint8_t Ep2In{0x82U};
constexpr std::array<std::uint8_t, 75U> ConfigurationDescriptor{{
    9U,2U,75U,0U,2U,1U,0U,0xc0U,0U,
    8U,0x0bU,0U,2U,0x02U,0x02U,0x01U,0U,
    9U,4U,0U,0U,1U,2U,2U,1U,0U,
    5U,0x24U,0U,0x10U,0x01U,
    5U,0x24U,1U,0U,1U,
    4U,0x24U,2U,6U,
    5U,0x24U,6U,0U,1U,
    7U,5U,0x82U,3U,16U,0U,16U,
    9U,4U,1U,0U,2U,0x0aU,0U,0U,0U,
    7U,5U,0x81U,2U,64U,0U,0U,
    7U,5U,0x01U,2U,64U,0U,0U
}};
}

const std::uint8_t* UsbCdcDevice::configuration_descriptor() const noexcept
{ return ConfigurationDescriptor.data(); }

bool UsbCdcDevice::initialize(std::uint16_t vendor_id, std::uint16_t product_id) noexcept
{
    if (initialized_) { return vendor_id_ == vendor_id && product_id_ == product_id; }
    if (vendor_id == 0U || vendor_id == 0xffffU || product_id == 0U || product_id == 0xffffU) { return false; }
    const std::uint8_t device[18U]{
        18U, 1U, 0x00U, 0x02U, 0xefU, 0x02U, 0x01U, 64U,
        static_cast<std::uint8_t>(vendor_id), static_cast<std::uint8_t>(vendor_id >> 8U),
        static_cast<std::uint8_t>(product_id), static_cast<std::uint8_t>(product_id >> 8U),
        0x00U, 0x01U, 0U, 1U, 0U, 1U
    };
    for (std::uint8_t i{}; i < 18U; ++i) { device_[i] = device[i]; }

    initialized_ = true;
    vendor_id_ = vendor_id; product_id_ = product_id;
    return true;
}

void UsbCdcDevice::bump_session() noexcept
{
    ++session_epoch_;
    if (session_epoch_ == 0U) { ++session_epoch_; }
}

std::uint32_t UsbCdcDevice::session() const noexcept
{ return configured_ && vbus_present_ && !suspended_ ? session_epoch_ : 0U; }

UsbStreamPort UsbCdcDevice::stream_port() noexcept
{ return UsbStreamPort{this, &stream_session, &stream_read, &stream_write}; }

void UsbCdcDevice::set_rx(std::uint8_t endpoint, bool enabled) noexcept
{ if (port_.set_rx_enabled != nullptr) { port_.set_rx_enabled(port_.context, endpoint, enabled); } }

void UsbCdcDevice::configure(bool enabled) noexcept
{
    configured_ = enabled;
    configuration_value_ = enabled ? 1U : 0U;
    halt_ep1_in_ = halt_ep1_out_ = halt_ep2_in_ = false;
    bulk_tx_queued_ = bulk_tx_inflight_ = bulk_zlp_pending_ = bulk_zlp_inflight_ = false;
    bulk_rx_pending_ = false; bulk_rx_size_ = bulk_rx_offset_ = 0U;
    interface_alt_[0] = interface_alt_[1] = 0U;
    if (port_.set_configuration != nullptr) { port_.set_configuration(port_.context, enabled); }
    set_rx(1U, enabled);
    bump_session();
}

void UsbCdcDevice::on_vbus(bool present) noexcept
{
    if (present == vbus_present_) { return; }
    vbus_present_ = present;
    bump_session();
    if (!present) {
        configure(false);
        address_ = 0U;
        if (port_.set_address != nullptr) { port_.set_address(port_.context, 0U); }
    }
}

void UsbCdcDevice::on_reset() noexcept
{
    configure(false);
    address_ = 0U; suspended_ = false; line_state_ = pending_line_state_ = 0U;
    line_coding_ = {{0x00U,0xc2U,0x01U,0x00U,0x00U,0x00U,0x08U}};
    control_phase_ = ControlPhase::Idle; deferred_ = Deferred::None;
    control_data_ = nullptr; control_length_ = control_offset_ = control_packet_size_ = 0U;
    control_zlp_after_data_ = control_zlp_inflight_ = false;
    if (port_.set_address != nullptr) { port_.set_address(port_.context, 0U); }
    bump_session();
}

void UsbCdcDevice::on_suspend() noexcept
{
    if (suspended_) { return; }
    suspended_ = true;
    // A suspended connection starts a new stream epoch. Discard buffered bulk
    // data and ask the backend to quiesce both directions before resume.
    bulk_tx_queued_ = bulk_tx_inflight_ = bulk_zlp_pending_ = bulk_zlp_inflight_ = false;
    bulk_rx_pending_ = false; bulk_rx_size_ = bulk_rx_offset_ = 0U;
    if (port_.cancel_bulk != nullptr) { port_.cancel_bulk(port_.context); }
    bump_session();
}

void UsbCdcDevice::on_resume() noexcept
{ if (suspended_) { suspended_ = false; bump_session(); } }

void UsbCdcDevice::stall_control() noexcept
{
    control_phase_ = ControlPhase::Idle; deferred_ = Deferred::None;
    control_data_ = nullptr; control_length_ = control_offset_ = control_packet_size_ = 0U;
    if (port_.set_halt != nullptr) {
        port_.set_halt(port_.context, Ep0Out, true);
        port_.set_halt(port_.context, Ep0In, true);
    }
}

void UsbCdcDevice::control_in(const std::uint8_t* data, std::uint16_t length,
                              std::uint16_t requested) noexcept
{
    if (data == nullptr && length != 0U) { stall_control(); return; }
    control_data_ = data;
    control_length_ = length < requested ? length : requested;
    control_requested_ = requested;
    control_offset_ = 0U;
    control_packet_size_ = 0U;
    control_zlp_after_data_ = control_length_ < requested &&
        (control_length_ % 64U) == 0U && control_length_ != 0U;
    control_zlp_inflight_ = false;
    if (control_length_ == 0U) {
        control_phase_ = ControlPhase::StatusOut;
        set_rx(0U, true);
    } else {
        control_phase_ = ControlPhase::DataIn;
        next_control_packet();
    }
}

void UsbCdcDevice::next_control_packet() noexcept
{
    if (control_phase_ != ControlPhase::DataIn || port_.send_in == nullptr) { return; }
    if (control_offset_ < control_length_) {
        const auto remain = static_cast<std::uint16_t>(control_length_ - control_offset_);
        const auto amount = static_cast<std::uint8_t>(remain < 64U ? remain : 64U);
        if (!port_.send_in(port_.context, 0U, control_data_ + control_offset_, amount)) { stall_control(); return; }
        control_packet_size_ = amount;
        return;
    }
    if (control_zlp_after_data_ && !control_zlp_inflight_) {
        if (!port_.send_in(port_.context, 0U, nullptr, 0U)) { return; }
        control_zlp_inflight_ = true;
        control_packet_size_ = 0U;
        return;
    }
    control_phase_ = ControlPhase::StatusOut;
    control_packet_size_ = 0U;
    set_rx(0U, true);
}

void UsbCdcDevice::control_status_in(Deferred action, std::uint8_t value,
                                     std::uint16_t auxiliary) noexcept
{
    deferred_ = action; pending_value_ = value; pending_line_state_ = auxiliary;
    control_phase_ = ControlPhase::StatusIn;
    if (port_.send_in == nullptr || !port_.send_in(port_.context, 0U, nullptr, 0U)) { stall_control(); }
}

void UsbCdcDevice::commit_deferred() noexcept
{
    const auto action = deferred_; deferred_ = Deferred::None;
    switch (action) {
    case Deferred::Address:
        address_ = pending_value_;
        if (port_.set_address != nullptr) { port_.set_address(port_.context, address_); }
        break;
    case Deferred::Configuration:
        configure(pending_value_ != 0U);
        break;
    case Deferred::LineState: {
        const auto prior = line_state_;
        line_state_ = pending_line_state_;
        if ((prior & 1U) != 0U && (line_state_ & 1U) == 0U && configured_) {
            bulk_tx_queued_ = bulk_tx_inflight_ = bulk_zlp_pending_ = bulk_zlp_inflight_ = false;
            bulk_rx_pending_ = false; bulk_rx_size_ = bulk_rx_offset_ = 0U;
            if (port_.cancel_bulk != nullptr) { port_.cancel_bulk(port_.context); }
            else { set_rx(1U, !halt_ep1_out_); }
            bump_session();
        }
        break;
    }
    case Deferred::LineCoding:
        line_coding_ = pending_line_coding_;
        break;
    case Deferred::Halt:
        if (pending_value_ != 0U) {
            if (pending_ep_ == Ep1In) {
                bulk_tx_queued_ = bulk_tx_inflight_ = bulk_zlp_pending_ = bulk_zlp_inflight_ = false;
            } else if (pending_ep_ == Ep1Out) {
                bulk_rx_pending_ = false; bulk_rx_size_ = bulk_rx_offset_ = 0U;
            }
        }
        if (pending_ep_ == Ep1In) { halt_ep1_in_ = pending_value_ != 0U; }
        else if (pending_ep_ == Ep1Out) { halt_ep1_out_ = pending_value_ != 0U; }
        else if (pending_ep_ == Ep2In) { halt_ep2_in_ = pending_value_ != 0U; }
        if (port_.set_halt != nullptr) { port_.set_halt(port_.context, pending_ep_, pending_value_ != 0U); }
        break;
    case Deferred::None:
        break;
    }
    control_phase_ = ControlPhase::Idle;
    set_rx(0U, true);
}

bool UsbCdcDevice::known_endpoint(std::uint8_t address) const noexcept
{
    if (address == Ep0Out || address == Ep0In) { return true; }
    return configured_ && (address == Ep1Out || address == Ep1In || address == Ep2In);
}

bool UsbCdcDevice::endpoint_halted(std::uint8_t address) const noexcept
{
    if (address == Ep1In) { return halt_ep1_in_; }
    if (address == Ep1Out) { return halt_ep1_out_; }
    if (address == Ep2In) { return halt_ep2_in_; }
    return false;
}

void UsbCdcDevice::on_setup(const std::uint8_t setup[8]) noexcept
{
    if (setup == nullptr) { return; }
    if (port_.abort_control != nullptr) { port_.abort_control(port_.context); }
    control_phase_ = ControlPhase::Idle; deferred_ = Deferred::None;
    control_data_ = nullptr; control_length_ = control_offset_ = control_packet_size_ = 0U;
    control_zlp_after_data_ = control_zlp_inflight_ = false;
    const auto request_type = setup[0];
    const auto request = setup[1];
    const auto value = static_cast<std::uint16_t>(std::uint16_t{setup[2]} | (std::uint16_t{setup[3]} << 8U));
    const auto index = static_cast<std::uint16_t>(std::uint16_t{setup[4]} | (std::uint16_t{setup[5]} << 8U));
    const auto length = static_cast<std::uint16_t>(std::uint16_t{setup[6]} | (std::uint16_t{setup[7]} << 8U));
    const auto recipient = static_cast<std::uint8_t>(request_type & 0x1fU);
    const auto type = static_cast<std::uint8_t>(request_type & 0x60U);
    const auto direction_in = (request_type & 0x80U) != 0U;
    static constexpr std::uint8_t language[4U]{4U,3U,0x09U,0x04U};
    static constexpr std::uint8_t product[14U]{14U,3U,'D',0,'S',0,'O',0,'1',0,'3',0,'8',0};
    static constexpr std::uint8_t ep0_status[2U]{0U,0U};
    if (type == 0U) {
        if (request == RequestGetDescriptor && direction_in && recipient == 0U) {
            const auto descriptor_type = static_cast<std::uint8_t>(value >> 8U);
            const auto descriptor_index = static_cast<std::uint8_t>(value);
            if (descriptor_type == DeviceDescriptorType && descriptor_index == 0U) {
                control_in(device_.data(), static_cast<std::uint16_t>(device_.size()), length); return;
            }
            if (descriptor_type == ConfigurationDescriptorType && descriptor_index == 0U) {
                control_in(ConfigurationDescriptor.data(), static_cast<std::uint16_t>(ConfigurationDescriptor.size()), length); return;
            }
            if (descriptor_type == StringDescriptorType && descriptor_index == 0U) {
                control_in(language, 4U, length); return;
            }
            if (descriptor_type == StringDescriptorType && descriptor_index == 1U && index == 0x0409U) {
                control_in(product, 14U, length); return;
            }
            stall_control(); return;
        }
        if (request == RequestSetAddress && request_type == 0U && value <= 127U && index == 0U && length == 0U) {
            control_status_in(Deferred::Address, static_cast<std::uint8_t>(value)); return;
        }
        if (request == RequestSetConfiguration && request_type == 0U && value <= 1U && index == 0U && length == 0U &&
            (value == 0U || address_ != 0U)) {
            control_status_in(Deferred::Configuration, static_cast<std::uint8_t>(value)); return;
        }
        if (request == RequestGetConfiguration && request_type == 0x80U && value == 0U && index == 0U && length == 1U) {
            const std::uint8_t cfg{configuration_value_}; control_in(&cfg, 1U, 1U); return;
        }
        if (request == RequestGetStatus && direction_in && recipient == 0U && value == 0U && index == 0U && length == 2U) {
            static constexpr std::uint8_t self_powered[2U]{1U,0U};
            control_in(self_powered, 2U, 2U); return;
        }
        if (request == RequestGetStatus && direction_in && recipient == 1U && configured_ &&
            value == 0U && index <= 1U && length == 2U) {
            control_in(ep0_status, 2U, 2U); return;
        }
        if (request == RequestGetStatus && direction_in && recipient == 2U && value == 0U && length == 2U) {
            const auto endpoint = static_cast<std::uint8_t>(index);
            if (index > 0xffU || !known_endpoint(endpoint)) { stall_control(); return; }
            const std::uint8_t status[2U]{static_cast<std::uint8_t>(endpoint_halted(endpoint) ? 1U : 0U),0U};
            control_in(status, 2U, 2U); return;
        }
        if ((request == RequestSetFeature || request == RequestClearFeature) && !direction_in &&
            recipient == 2U && value == FeatureEndpointHalt && index <= 0xffU && length == 0U) {
            const auto endpoint = static_cast<std::uint8_t>(index);
            if (index > 0xffU || !configured_ ||
                (endpoint != Ep1Out && endpoint != Ep1In && endpoint != Ep2In)) { stall_control(); return; }
            pending_ep_ = endpoint;
            control_status_in(Deferred::Halt, request == RequestSetFeature ? 1U : 0U); return;
        }
        if (request == RequestGetInterface && request_type == 0x81U && configured_ && value == 0U && index <= 1U && length == 1U) {
            control_in(&interface_alt_[index], 1U, 1U); return;
        }
        if (request == RequestSetInterface && request_type == 0x01U && configured_ && value == 0U && index <= 1U && length == 0U) {
            interface_alt_[index] = 0U; control_status_in(Deferred::None, 0U); return;
        }
    } else if (type == 0x20U && recipient == 1U && index == 0U) {
        if (request == CdcGetLineCoding && direction_in && value == 0U && length == 7U) {
            control_in(line_coding_.data(), 7U, 7U); return;
        }
        if (request == CdcSetLineCoding && !direction_in && value == 0U && length == 7U) {
            control_phase_ = ControlPhase::DataOutLineCoding; set_rx(0U, true); return;
        }
        if (request == CdcSetControlLineState && !direction_in && length == 0U && (value & ~3U) == 0U) {
            control_status_in(Deferred::LineState, 0U, value); return;
        }
        if (request == CdcSendBreak && request_type == 0x21U && length == 0U) {
            // The byte stream has no physical UART break signal to change.
            control_status_in(Deferred::None, 0U); return;
        }
    }
    stall_control();
}

void UsbCdcDevice::on_out(std::uint8_t endpoint, const std::uint8_t* data,
                          std::uint8_t length) noexcept
{
    if (endpoint == 0U) {
        if (control_phase_ == ControlPhase::StatusOut && length == 0U) {
            control_phase_ = ControlPhase::Idle; set_rx(0U, true); return;
        }
        if (control_phase_ == ControlPhase::DataOutLineCoding && length == 7U && data != nullptr) {
            const auto parity = data[5]; const auto stop = data[4];
            if (data[6] == 0U || parity > 4U || stop > 2U) { stall_control(); return; }
            for (std::uint8_t i{}; i < 7U; ++i) { pending_line_coding_[i] = data[i]; }
            set_rx(0U, false); control_status_in(Deferred::LineCoding, 0U); return;
        }
        stall_control(); return;
    }
    if (endpoint != 1U || !configured_ || suspended_ || halt_ep1_out_) { return; }
    if (length == 0U) { set_rx(1U, !bulk_rx_pending_); return; }
    if (length > bulk_rx_.size() || data == nullptr || bulk_rx_pending_) { set_rx(1U, false); return; }
    for (std::uint8_t i{}; i < length; ++i) { bulk_rx_[i] = data[i]; }
    bulk_rx_size_ = length; bulk_rx_offset_ = 0U; bulk_rx_pending_ = true;
    set_rx(1U, false);
}

void UsbCdcDevice::on_in_complete(std::uint8_t endpoint) noexcept
{
    if (endpoint == 0U) {
        if (control_phase_ == ControlPhase::StatusIn) { commit_deferred(); return; }
        if (control_phase_ != ControlPhase::DataIn) { return; }
        if (control_zlp_inflight_) {
            control_zlp_inflight_ = false;
            control_phase_ = ControlPhase::StatusOut; set_rx(0U, true); return;
        }
        control_offset_ = static_cast<std::uint16_t>(control_offset_ + control_packet_size_);
        control_packet_size_ = 0U;
        next_control_packet();
        return;
    }
    if (endpoint == 1U) {
        if (bulk_zlp_inflight_) { bulk_zlp_inflight_ = false; return; }
        if (bulk_tx_inflight_) {
            bulk_tx_inflight_ = false;
            if (bulk_zlp_pending_) {
                if (port_.send_in != nullptr && port_.send_in(port_.context, 1U, nullptr, 0U)) {
                    bulk_zlp_pending_ = false; bulk_zlp_inflight_ = true;
                }
            }
        }
        return;
    }
}

void UsbCdcDevice::poll() noexcept
{
    if (!configured_ || suspended_ || port_.send_in == nullptr) { return; }
    if (bulk_zlp_pending_ && !bulk_zlp_inflight_ && !bulk_tx_inflight_) {
        if (port_.send_in(port_.context, 1U, nullptr, 0U)) {
            bulk_zlp_pending_ = false; bulk_zlp_inflight_ = true;
        }
    }
    if (bulk_tx_queued_ && !bulk_tx_inflight_ && !bulk_zlp_inflight_ && !bulk_zlp_pending_ && !halt_ep1_in_) {
        if (port_.send_in(port_.context, 1U, bulk_tx_.data(), bulk_tx_size_)) {
            bulk_tx_queued_ = false; bulk_tx_inflight_ = true;
            bulk_zlp_pending_ = bulk_tx_size_ == 64U;
        }
    }
}

std::uint32_t UsbCdcDevice::stream_session(void* context) noexcept
{ return context == nullptr ? 0U : static_cast<UsbCdcDevice*>(context)->session(); }

std::uint16_t UsbCdcDevice::stream_read(void* context, std::uint8_t* output,
                                        std::uint16_t capacity) noexcept
{
    if (context == nullptr || output == nullptr || capacity == 0U) { return 0U; }
    auto& self = *static_cast<UsbCdcDevice*>(context);
    if (!self.bulk_rx_pending_) { return 0U; }
    const auto remain = static_cast<std::uint8_t>(self.bulk_rx_size_ - self.bulk_rx_offset_);
    const auto amount = static_cast<std::uint8_t>(capacity < remain ? capacity : remain);
    for (std::uint8_t i{}; i < amount; ++i) { output[i] = self.bulk_rx_[self.bulk_rx_offset_ + i]; }
    self.bulk_rx_offset_ = static_cast<std::uint8_t>(self.bulk_rx_offset_ + amount);
    if (self.bulk_rx_offset_ == self.bulk_rx_size_) {
        self.bulk_rx_pending_ = false; self.bulk_rx_size_ = self.bulk_rx_offset_ = 0U;
        self.set_rx(1U, self.configured_ && !self.halt_ep1_out_);
    }
    return amount;
}

bool UsbCdcDevice::stream_write(void* context, const std::uint8_t* input,
                                std::uint16_t bytes) noexcept
{
    if (context == nullptr || bytes > 64U || (bytes != 0U && input == nullptr)) { return false; }
    auto& self = *static_cast<UsbCdcDevice*>(context);
    if (!self.configured_ || self.suspended_ || self.halt_ep1_in_ || self.bulk_tx_queued_ ||
        self.bulk_tx_inflight_ || self.bulk_zlp_pending_ || self.bulk_zlp_inflight_) { return false; }
    for (std::uint16_t i{}; i < bytes; ++i) { self.bulk_tx_[i] = input[i]; }
    self.bulk_tx_size_ = static_cast<std::uint8_t>(bytes);
    self.bulk_tx_queued_ = true;
    return true;
}

} // namespace dso
