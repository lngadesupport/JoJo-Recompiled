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
    memory_command_ = 0u;
    memory_stage_ = 0u;
    memory_sector_ = 0u;
    memory_checksum_ = 0u;
    memory_previous_byte_ = 0u;
    memory_end_byte_ = 0x47u;
    memory_write_buffer_.fill(0u);
    memory_sector_valid_ = false;
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


void Ps1Sio0::transfer_memory_byte(
    std::uint8_t value,
    std::uint8_t& response,
    bool& more_data) noexcept {
    auto& card = memory_cards_[selected_port()];

    if (memory_command_ == 0u) {
        memory_command_ = value;
        response = card.flag_byte();
        memory_stage_ = 0u;
        more_data = value == 0x52u || value == 0x57u || value == 0x53u;
        if (!more_data) transaction_ = TransactionState::done;
        return;
    }

    if (memory_command_ == 0x53u) {
        static constexpr std::array<std::uint8_t, 8> id{
            0x5Au, 0x5Du, 0x5Cu, 0x5Du, 0x04u, 0x00u, 0x00u, 0x80u};
        if (memory_stage_ < id.size()) {
            response = id[memory_stage_++];
            more_data = memory_stage_ < id.size();
            if (!more_data) transaction_ = TransactionState::done;
        } else {
            response = 0xFFu;
            more_data = false;
            transaction_ = TransactionState::done;
        }
        return;
    }

    if (memory_command_ == 0x52u) {
        switch (memory_stage_) {
            case 0u:
                response = 0x5Au;
                break;
            case 1u:
                response = 0x5Du;
                break;
            case 2u:
                memory_sector_ = static_cast<std::uint16_t>(value) << 8u;
                memory_checksum_ = value;
                response = 0x00u;
                break;
            case 3u:
                memory_sector_ = static_cast<std::uint16_t>(
                    memory_sector_ | value);
                memory_checksum_ ^= value;
                memory_sector_valid_ =
                    memory_sector_ < Ps1MemoryCard::sector_count;
                response = memory_previous_byte_;
                break;
            case 4u:
                response = 0x5Cu;
                break;
            case 5u:
                response = 0x5Du;
                break;
            case 6u:
                response = memory_sector_valid_
                    ? static_cast<std::uint8_t>(memory_sector_ >> 8u)
                    : 0xFFu;
                break;
            case 7u:
                response = memory_sector_valid_
                    ? static_cast<std::uint8_t>(memory_sector_)
                    : 0xFFu;
                if (!memory_sector_valid_) {
                    more_data = false;
                    transaction_ = TransactionState::done;
                    ++memory_stage_;
                    memory_previous_byte_ = value;
                    return;
                }
                break;
            default:
                if (memory_stage_ >= 8u && memory_stage_ < 136u) {
                    const auto sector = card.read_sector(memory_sector_);
                    const auto index = static_cast<std::size_t>(memory_stage_ - 8u);
                    response = sector ? (*sector)[index] : 0xFFu;
                    memory_checksum_ ^= response;
                } else if (memory_stage_ == 136u) {
                    response = memory_checksum_;
                } else if (memory_stage_ == 137u) {
                    response = 0x47u;
                    more_data = false;
                    transaction_ = TransactionState::done;
                    ++memory_stage_;
                    memory_previous_byte_ = value;
                    return;
                } else {
                    response = 0xFFu;
                    more_data = false;
                    transaction_ = TransactionState::done;
                    ++memory_stage_;
                    memory_previous_byte_ = value;
                    return;
                }
                break;
        }

        ++memory_stage_;
        memory_previous_byte_ = value;
        more_data = true;
        return;
    }

    if (memory_command_ == 0x57u) {
        switch (memory_stage_) {
            case 0u:
                response = 0x5Au;
                break;
            case 1u:
                response = 0x5Du;
                break;
            case 2u:
                memory_sector_ = static_cast<std::uint16_t>(value) << 8u;
                memory_checksum_ = value;
                response = 0x00u;
                break;
            case 3u:
                memory_sector_ = static_cast<std::uint16_t>(
                    memory_sector_ | value);
                memory_checksum_ ^= value;
                memory_sector_valid_ =
                    memory_sector_ < Ps1MemoryCard::sector_count;
                response = memory_previous_byte_;
                break;
            default:
                if (memory_stage_ >= 4u && memory_stage_ < 132u) {
                    const auto index = static_cast<std::size_t>(memory_stage_ - 4u);
                    memory_write_buffer_[index] = value;
                    memory_checksum_ ^= value;
                    response = memory_previous_byte_;
                } else if (memory_stage_ == 132u) {
                    const bool checksum_ok = value == memory_checksum_;
                    if (!memory_sector_valid_) {
                        memory_end_byte_ = 0xFFu;
                    } else if (!checksum_ok) {
                        memory_end_byte_ = 0x4Eu;
                    } else if (!card.write_sector(
                                   memory_sector_,
                                   memory_write_buffer_)) {
                        memory_end_byte_ = 0xFFu;
                    } else {
                        memory_end_byte_ = 0x47u;
                    }
                    response = memory_previous_byte_;
                } else if (memory_stage_ == 133u) {
                    response = 0x5Cu;
                } else if (memory_stage_ == 134u) {
                    response = 0x5Du;
                } else if (memory_stage_ == 135u) {
                    response = memory_end_byte_;
                    more_data = false;
                    transaction_ = TransactionState::done;
                    ++memory_stage_;
                    memory_previous_byte_ = value;
                    return;
                } else {
                    response = 0xFFu;
                    more_data = false;
                    transaction_ = TransactionState::done;
                    ++memory_stage_;
                    memory_previous_byte_ = value;
                    return;
                }
                break;
        }

        ++memory_stage_;
        memory_previous_byte_ = value;
        more_data = true;
        return;
    }

    response = 0xFFu;
    more_data = false;
    transaction_ = TransactionState::done;
}

void Ps1Sio0::transfer_byte(std::uint8_t value) noexcept {
    std::uint8_t response = 0xFFu;
    bool more_data = false;

    switch (transaction_) {
        case TransactionState::idle:
            if (value == 0x01u) {
                transaction_ = TransactionState::controller_command;
                more_data = true;
            } else if (value == 0x81u) {
                transaction_ = TransactionState::memory_command;
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
        case TransactionState::memory_command:
            transaction_ = TransactionState::memory_transfer;
            transfer_memory_byte(value, response, more_data);
            break;
        case TransactionState::memory_transfer:
            transfer_memory_byte(value, response, more_data);
            break;
        case TransactionState::done:
            response = 0xFFu;
            more_data = false;
            break;
    }

    rx_fifo_.push_back(response);
    if (rx_fifo_.size() > 8u) rx_fifo_.pop_front();

    dsr_ = more_data;
    if (more_data && (control_ & kControlDsrIrqEnable) != 0u) {
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
    if (physical == data_address) {
        if (rx_fifo_.empty()) {
            return {R3000aBusStatus::ok, 0xFFFFu};
        }
        std::uint32_t value = rx_fifo_.front();
        if (rx_fifo_.size() >= 2u) {
            value |= static_cast<std::uint32_t>(rx_fifo_[1]) << 8u;
        } else {
            value |= 0xFF00u;
        }
        rx_fifo_.pop_front();
        return {R3000aBusStatus::ok, value};
    }
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
        const auto count = std::min<std::size_t>(4u, rx_fifo_.size());
        for (std::size_t i = 0u; i < count; ++i) {
            const auto mask = ~(0xFFu << (i * 8u));
            value = (value & mask) |
                    (static_cast<std::uint32_t>(rx_fifo_[i]) << (i * 8u));
        }
        for (std::size_t i = 0u; i < count; ++i) {
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

    const bool acknowledge = (value & kControlAcknowledge) != 0u;
    const auto previous_control = control_;
    control_ = static_cast<std::uint16_t>(value & kControlStoredMask);

    if (acknowledge) {
        irq_ = dsr_ && (control_ & kControlDsrIrqEnable) != 0u;
    } else if (dsr_ &&
               (previous_control & kControlDsrIrqEnable) == 0u &&
               (control_ & kControlDsrIrqEnable) != 0u) {
        irq_ = true;
    }
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

Ps1MemoryCard& Ps1Sio0::memory_card(std::uint32_t port) noexcept {
    return memory_cards_[port < memory_cards_.size() ? port : 0u];
}

const Ps1MemoryCard& Ps1Sio0::memory_card(std::uint32_t port) const noexcept {
    return memory_cards_[port < memory_cards_.size() ? port : 0u];
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
    for (const auto& card : memory_cards_) {
        const auto card_hash = card.content_hash();
        for (unsigned shift = 0u; shift < 64u; shift += 8u) {
            hash_byte(hash, static_cast<std::uint8_t>(card_hash >> shift));
        }
        hash_byte(hash, card.flag_byte());
        hash_byte(hash, static_cast<std::uint8_t>(card.dirty() ? 1u : 0u));
    }
    hash_byte(hash, static_cast<std::uint8_t>(transaction_));
    hash_byte(hash, memory_command_);
    hash_u16(hash, memory_stage_);
    hash_u16(hash, memory_sector_);
    hash_byte(hash, memory_checksum_);
    hash_byte(hash, memory_previous_byte_);
    hash_byte(hash, memory_end_byte_);
    hash_byte(hash, static_cast<std::uint8_t>(memory_sector_valid_ ? 1u : 0u));
    hash_byte(hash, static_cast<std::uint8_t>(dsr_ ? 1u : 0u));
    hash_byte(hash, static_cast<std::uint8_t>(irq_ ? 1u : 0u));
    for (const auto value : rx_fifo_) hash_byte(hash, value);
    return hash;
}

} // namespace jojo
