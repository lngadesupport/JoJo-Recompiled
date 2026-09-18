#include "core/ps1_sio0.h"

#include <algorithm>

namespace jojo {
namespace {

constexpr std::uint16_t kControlTxEnable = 1u << 0u;
constexpr std::uint16_t kControlDtr = 1u << 1u;
constexpr std::uint16_t kControlAcknowledge = 1u << 4u;
constexpr std::uint16_t kControlReset = 1u << 6u;
constexpr std::uint16_t kControlDsrIrqEnable = 1u << 12u;
constexpr std::uint16_t kControlPortSelect = 1u << 13u;
constexpr std::uint16_t kControlStoredMask = 0x3F0Fu;

constexpr std::uint32_t kStatusTxReady = 1u << 0u;
constexpr std::uint32_t kStatusRxReady = 1u << 1u;
constexpr std::uint32_t kStatusTxIdle = 1u << 2u;
constexpr std::uint32_t kStatusDsr = 1u << 7u;
constexpr std::uint32_t kStatusIrq = 1u << 9u;

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= kFnvPrime;
}

void hash_u16(std::uint64_t& hash, std::uint16_t value) noexcept {
    hash_byte(hash, static_cast<std::uint8_t>(value));
    hash_byte(hash, static_cast<std::uint8_t>(value >> 8u));
}

} // namespace

void Ps1Sio0::reset_transaction() noexcept {
    rx_fifo_.clear();
    transaction_ = TransactionState::idle;
    dsr_ = false;
}

void Ps1Sio0::reset_registers() noexcept {
    mode_ = 0u;
    control_ = 0u;
    baud_ = 0u;
    irq_ = false;
    reset_transaction();
}

std::uint32_t Ps1Sio0::selected_port() const noexcept {
    return (control_ & kControlPortSelect) != 0u ? 1u : 0u;
}

std::uint32_t Ps1Sio0::status_value() const noexcept {
    std::uint32_t status = kStatusTxReady | kStatusTxIdle;
    if (!rx_fifo_.empty()) status |= kStatusRxReady;
    if (dsr_) status |= kStatusDsr;
    if (irq_) status |= kStatusIrq;
    return status;
}

void Ps1Sio0::transfer_byte(std::uint8_t value) noexcept {
    std::uint8_t response = 0xFFu;
    bool more_data = false;

    switch (transaction_) {
        case TransactionState::idle:
            if (value == 0x01u) {
                transaction_ = TransactionState::controller_command;
                more_data = true;
            } else {
                transaction_ = TransactionState::done;
            }
            break;
        case TransactionState::controller_command:
            if (value == 0x42u) {
                response = 0x41u;
                transaction_ = TransactionState::controller_id_high;
                more_data = true;
            } else {
                transaction_ = TransactionState::done;
            }
            break;
        case TransactionState::controller_id_high:
            response = 0x5Au;
            transaction_ = TransactionState::controller_buttons_low;
            more_data = true;
            break;
        case TransactionState::controller_buttons_low:
            response = static_cast<std::uint8_t>(
                pad_buttons_[selected_port()] & 0x00FFu);
            transaction_ = TransactionState::controller_buttons_high;
            more_data = true;
            break;
        case TransactionState::controller_buttons_high:
            response = static_cast<std::uint8_t>(
                pad_buttons_[selected_port()] >> 8u);
            transaction_ = TransactionState::done;
            more_data = false;
            break;
        case TransactionState::done:
            response = 0xFFu;
            more_data = false;
            break;
    }

    rx_fifo_.push_back(response);
    if (rx_fifo_.size() > 8u) rx_fifo_.pop_front();

    dsr_ = more_data;
    if (more_data) {
        irq_ = true;
    }
}

R3000aBusResult Ps1Sio0::read8(std::uint32_t physical) noexcept {
    if (physical != data_address) {
        return {R3000aBusStatus::unsupported, 0u};
    }

    if (rx_fifo_.empty()) {
        return {R3000aBusStatus::ok, 0xFFu};
    }
    const auto value = rx_fifo_.front();
    rx_fifo_.pop_front();
    return {R3000aBusStatus::ok, value};
}

R3000aBusResult Ps1Sio0::read16(std::uint32_t physical) noexcept {
    if (physical == mode_address) {
        return {R3000aBusStatus::ok, mode_};
    }
    if (physical == control_address) {
        return {R3000aBusStatus::ok, control_};
    }
    if (physical == baud_address) {
        return {R3000aBusStatus::ok, baud_};
    }
    return {R3000aBusStatus::unsupported, 0u};
}

R3000aBusResult Ps1Sio0::read32(std::uint32_t physical) noexcept {
    if (physical == status_address) {
        return {R3000aBusStatus::ok, status_value()};
    }
    if (physical == data_address) {
        std::uint32_t value = 0xFFFFFFFFu;
        if (!rx_fifo_.empty()) {
            value = (value & 0xFFFFFF00u) | rx_fifo_.front();
            for (std::size_t i = 1u; i < std::min<std::size_t>(4u, rx_fifo_.size()); ++i) {
                const auto mask = ~(0xFFu << (i * 8u));
                value = (value & mask) |
                        (static_cast<std::uint32_t>(rx_fifo_[i]) << (i * 8u));
            }
            rx_fifo_.pop_front();
        }
        return {R3000aBusStatus::ok, value};
    }
    return {R3000aBusStatus::unsupported, 0u};
}

R3000aBusResult Ps1Sio0::write8(
    std::uint32_t physical,
    std::uint8_t value) noexcept {
    if (physical != data_address) {
        return {R3000aBusStatus::unsupported, 0u};
    }

    if ((control_ & kControlTxEnable) == 0u ||
        (control_ & kControlDtr) == 0u) {
        return {R3000aBusStatus::ok, 0u};
    }

    transfer_byte(value);
    return {R3000aBusStatus::ok, 0u};
}

R3000aBusResult Ps1Sio0::write16(
    std::uint32_t physical,
    std::uint16_t value) noexcept {
    if (physical == mode_address) {
        mode_ = value;
        return {R3000aBusStatus::ok, 0u};
    }
    if (physical == baud_address) {
        baud_ = value;
        return {R3000aBusStatus::ok, 0u};
    }
    if (physical != control_address) {
        return {R3000aBusStatus::unsupported, 0u};
    }

    if ((value & kControlReset) != 0u) {
        reset_registers();
        return {R3000aBusStatus::ok, 0u};
    }

    if ((value & kControlAcknowledge) != 0u) {
        irq_ = false;
    }

    const auto previous_control = control_;
    control_ = static_cast<std::uint16_t>(value & kControlStoredMask);
    const bool dtr_fell =
        (previous_control & kControlDtr) != 0u &&
        (control_ & kControlDtr) == 0u;
    const bool port_changed =
        ((previous_control ^ control_) & kControlPortSelect) != 0u;
    if (dtr_fell || port_changed) {
        reset_transaction();
    }
    if ((control_ & kControlDtr) == 0u) {
        dsr_ = false;
    }

    return {R3000aBusStatus::ok, 0u};
}

R3000aBusResult Ps1Sio0::write32(
    std::uint32_t physical,
    std::uint32_t value) noexcept {
    if (physical == data_address) {
        return write8(physical, static_cast<std::uint8_t>(value));
    }
    return {R3000aBusStatus::unsupported, 0u};
}

void Ps1Sio0::set_digital_pad_buttons(
    std::uint32_t port,
    std::uint16_t active_low_buttons) noexcept {
    if (port < pad_buttons_.size()) {
        pad_buttons_[port] = active_low_buttons;
    }
}

bool Ps1Sio0::irq_pending() const noexcept {
    return irq_;
}

std::uint64_t Ps1Sio0::diagnostic_state_hash() const noexcept {
    std::uint64_t hash = kFnvOffset;
    hash_u16(hash, mode_);
    hash_u16(hash, control_);
    hash_u16(hash, baud_);
    for (const auto buttons : pad_buttons_) hash_u16(hash, buttons);
    hash_byte(hash, static_cast<std::uint8_t>(transaction_));
    hash_byte(hash, static_cast<std::uint8_t>(dsr_ ? 1u : 0u));
    hash_byte(hash, static_cast<std::uint8_t>(irq_ ? 1u : 0u));
    for (const auto value : rx_fifo_) hash_byte(hash, value);
    return hash;
}

} // namespace jojo
