#include "core/ps1_cdrom.h"

#include <array>
#include <vector>

namespace jojo {
namespace {

constexpr std::uint32_t kCdBase = 0x1F801800u;
constexpr std::uint32_t kCdStatus = kCdBase + 0u;
constexpr std::uint32_t kCdCommandResponse = kCdBase + 1u;
constexpr std::uint32_t kCdParameterData = kCdBase + 2u;
constexpr std::uint32_t kCdInterrupt = kCdBase + 3u;
constexpr std::uint8_t kStatMotor = 1u << 1u;
constexpr std::uint8_t kStatRead = 1u << 5u;
constexpr std::uint8_t kStatSeek = 1u << 6u;
constexpr std::uint8_t kStatPlay = 1u << 7u;
constexpr std::uint8_t kStatActivityMask =
    static_cast<std::uint8_t>(kStatRead | kStatSeek | kStatPlay);
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;
constexpr std::uint32_t kCdCommandCompletionCycles = 33869u;
constexpr std::uint32_t kCdSectorCycles = 451584u;

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= kFnvPrime;
}

void hash_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned shift = 0u; shift < 64u; shift += 8u) {
        hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

void hash_bytes(
    std::uint64_t& hash,
    const std::deque<std::uint8_t>& values) noexcept {
    hash_u64(hash, values.size());
    for (const auto value : values) hash_byte(hash, value);
}

bool bcd_to_binary(std::uint8_t bcd, std::uint32_t& out) noexcept {
    const auto hi = static_cast<std::uint32_t>(bcd >> 4u);
    const auto lo = static_cast<std::uint32_t>(bcd & 0x0Fu);
    if (hi > 9u || lo > 9u) return false;
    out = hi * 10u + lo;
    return true;
}

std::uint8_t binary_to_bcd(std::uint32_t value) noexcept {
    value %= 100u;
    return static_cast<std::uint8_t>(
        ((value / 10u) << 4u) | (value % 10u));
}

} // namespace

void Ps1CdromController::attach_disc(const Ps1DiscSession* disc) noexcept {
    disc_ = disc;
}

R3000aBusResult Ps1CdromController::read8(std::uint32_t physical) noexcept {
    if (physical == kCdStatus) {
        std::uint32_t status = index_ & 0x03u;
        if (parameters_.empty()) status |= 1u << 3u;
        if (parameters_.size() < parameter_capacity) status |= 1u << 4u;
        if (!responses_.empty()) status |= 1u << 5u;
        if (!data_.empty()) status |= 1u << 6u;
        return {R3000aBusStatus::ok, status};
    }
    if (physical == kCdCommandResponse) {
        if (responses_.empty()) return {R3000aBusStatus::unsupported, 0u};
        const auto value = responses_.front();
        responses_.pop_front();
        return {R3000aBusStatus::ok, value};
    }
    if (physical == kCdParameterData) {
        if (data_.empty()) return {R3000aBusStatus::unsupported, 0u};
        const auto value = data_.front();
        data_.pop_front();
        return {R3000aBusStatus::ok, value};
    }
    if (physical == kCdInterrupt) {
        return {R3000aBusStatus::ok,
                (index_ == 0u || index_ == 2u)
                    ? static_cast<std::uint32_t>(interrupt_enable_ | 0xE0u)
                    : static_cast<std::uint32_t>(interrupt_flags_ | 0xE0u)};
    }
    return {R3000aBusStatus::unsupported, 0u};
}

R3000aBusResult Ps1CdromController::write8(std::uint32_t physical,
                                           std::uint8_t value) noexcept {
    if (physical == kCdStatus) {
        index_ = static_cast<std::uint8_t>(value & 0x03u);
        return {R3000aBusStatus::ok, 0u};
    }
    if (physical == kCdCommandResponse) {
        if (index_ == 0u) return execute_command(value);
        if (index_ == 1u) {
            // WRDATA is used only by manual sound-map uploads. Accept the
            // host-interface write; no sound-map transfer is active yet.
            return {R3000aBusStatus::ok, 0u};
        }
        if (index_ == 2u) {
            // CI coding-info register for manual XA sound-map mode.
            return {R3000aBusStatus::ok, 0u};
        }
        if (index_ == 3u) {
            pending_audio_matrix_[2] = value; // ATV2 R->R
            return {R3000aBusStatus::ok, 0u};
        }
        return {R3000aBusStatus::unsupported, 0u};
    }
    if (physical == kCdParameterData) {
        if (index_ == 0u) {
            if (parameters_.size() >= parameter_capacity) {
                return {R3000aBusStatus::unsupported, 0u};
            }
            parameters_.push_back(value);
            return {R3000aBusStatus::ok, 0u};
        }
        if (index_ == 1u) {
            interrupt_enable_ = static_cast<std::uint8_t>(value & 0x1Fu);
            return {R3000aBusStatus::ok, 0u};
        }
        if (index_ == 2u) {
            pending_audio_matrix_[0] = value; // ATV0 L->L
            return {R3000aBusStatus::ok, 0u};
        }
        if (index_ == 3u) {
            pending_audio_matrix_[3] = value; // ATV3 R->L
            return {R3000aBusStatus::ok, 0u};
        }
        return {R3000aBusStatus::unsupported, 0u};
    }
    if (physical == kCdInterrupt) {
        if (index_ == 0u) {
            // CD request register (bank 0). Bit 7 requests sector-buffer
            // reads; bit 5/6 are sound-map/write controls. JoJo clears the
            // register with 00h during bootstrap, which is a valid operation.
            request_register_ = value;
            return {R3000aBusStatus::ok, 0u};
        }
        if (index_ == 1u) {
            if ((value & 0x1Fu) != 0u) {
                interrupt_flags_ = 0u;
            }
            if ((value & 0x40u) != 0u) parameters_.clear();
            return {R3000aBusStatus::ok, 0u};
        }
        if (index_ == 2u) {
            pending_audio_matrix_[1] = value; // ATV1 L->R
            return {R3000aBusStatus::ok, 0u};
        }
        if (index_ == 3u) {
            adpcm_muted_ = (value & 0x01u) != 0u;
            if ((value & 0x20u) != 0u) {
                active_audio_matrix_ = pending_audio_matrix_;
            }
            return {R3000aBusStatus::ok, 0u};
        }
        return {R3000aBusStatus::unsupported, 0u};
    }
    return {R3000aBusStatus::unsupported, 0u};
}

void Ps1CdromController::step(std::uint32_t cpu_cycles) noexcept {
    if (deferred_responses_.empty() || interrupt_flags_ != 0u) {
        return;
    }

    auto& pending = deferred_responses_.front();
    if (pending.delay_cycles > cpu_cycles) {
        pending.delay_cycles -= cpu_cycles;
        return;
    }

    if (pending.apply_response_to_status) {
        status_byte_ = pending.response;
    }
    if (!push_response(pending.response)) {
        return;
    }
    if (!pending.data.empty()) {
        data_.assign(pending.data.begin(), pending.data.end());
    }
    if (pending.advance_lba) {
        ++current_lba_;
    }
    interrupt_flags_ = pending.interrupt_code;
    deferred_responses_.pop_front();
}

std::size_t Ps1CdromController::response_bytes_available() const noexcept {
    return responses_.size();
}

std::size_t Ps1CdromController::data_bytes_available() const noexcept {
    return data_.size();
}

std::size_t Ps1CdromController::read_data_words(std::span<std::uint32_t> out) noexcept {
    std::size_t written = 0u;
    while (written < out.size() && data_.size() >= 4u) {
        const auto b0 = static_cast<std::uint32_t>(data_.front()); data_.pop_front();
        const auto b1 = static_cast<std::uint32_t>(data_.front()); data_.pop_front();
        const auto b2 = static_cast<std::uint32_t>(data_.front()); data_.pop_front();
        const auto b3 = static_cast<std::uint32_t>(data_.front()); data_.pop_front();
        out[written++] = b0 | (b1 << 8u) | (b2 << 16u) | (b3 << 24u);
    }
    return written;
}

std::uint64_t Ps1CdromController::current_lba() const noexcept {
    return current_lba_;
}

std::uint64_t Ps1CdromController::command_count() const noexcept {
    return command_count_;
}

std::uint8_t Ps1CdromController::request_register() const noexcept {
    return request_register_;
}

bool Ps1CdromController::irq_pending() const noexcept {
    if (interrupt_flags_ < 1u || interrupt_flags_ > 5u) return false;
    const auto mask = static_cast<std::uint8_t>(
        1u << (interrupt_flags_ - 1u));
    return (interrupt_enable_ & mask) != 0u;
}

std::size_t Ps1CdromController::deferred_response_count() const noexcept {
    return deferred_responses_.size();
}

bool Ps1CdromController::muted() const noexcept {
    return muted_;
}

bool Ps1CdromController::adpcm_muted() const noexcept {
    return adpcm_muted_;
}

const std::array<std::uint8_t, 4>&
Ps1CdromController::pending_audio_matrix() const noexcept {
    return pending_audio_matrix_;
}

const std::array<std::uint8_t, 4>&
Ps1CdromController::active_audio_matrix() const noexcept {
    return active_audio_matrix_;
}

std::uint64_t Ps1CdromController::diagnostic_state_hash() const noexcept {
    std::uint64_t hash = kFnvOffset;
    hash_byte(hash, index_);
    hash_byte(hash, interrupt_enable_);
    hash_byte(hash, interrupt_flags_);
    hash_byte(hash, request_register_);
    hash_byte(hash, status_byte_);
    hash_byte(hash, static_cast<std::uint8_t>(muted_ ? 1u : 0u));
    hash_byte(hash, static_cast<std::uint8_t>(adpcm_muted_ ? 1u : 0u));
    for (const auto value : pending_audio_matrix_) hash_byte(hash, value);
    for (const auto value : active_audio_matrix_) hash_byte(hash, value);
    hash_u64(hash, current_lba_);
    hash_bytes(hash, parameters_);
    hash_bytes(hash, responses_);
    hash_bytes(hash, data_);
    hash_u64(hash, deferred_responses_.size());
    for (const auto& pending : deferred_responses_) {
        hash_byte(hash, pending.interrupt_code);
        hash_byte(hash, pending.response);
        hash_u64(hash, pending.delay_cycles);
        hash_u64(hash, pending.data.size());
        for (const auto value : pending.data) hash_byte(hash, value);
        hash_byte(hash, static_cast<std::uint8_t>(
            pending.advance_lba ? 1u : 0u));
        hash_byte(hash, static_cast<std::uint8_t>(
            pending.apply_response_to_status ? 1u : 0u));
    }
    hash_u64(hash, command_count_);
    hash_u64(hash, recent_commands_.size());
    for (const auto& event : recent_commands_) {
        hash_byte(hash, event.command);
        hash_byte(hash, event.index);
        hash_byte(hash, event.status);
    }
    hash_byte(
        hash,
        static_cast<std::uint8_t>(
            last_unsupported_command_.has_value() ? 1u : 0u));
    if (last_unsupported_command_) {
        hash_byte(hash, *last_unsupported_command_);
    }
    return hash;
}

const std::deque<Ps1CdromCommandEvent>&
Ps1CdromController::recent_commands() const noexcept {
    return recent_commands_;
}

const std::optional<std::uint8_t>&
Ps1CdromController::last_unsupported_command() const noexcept {
    return last_unsupported_command_;
}

bool Ps1CdromController::push_response(std::uint8_t value) noexcept {
    if (responses_.size() >= response_capacity) return false;
    responses_.push_back(value);
    return true;
}

void Ps1CdromController::clear_transfer_fifos() noexcept {
    parameters_.clear();
    responses_.clear();
    data_.clear();
}

R3000aBusResult Ps1CdromController::execute_command(std::uint8_t command) noexcept {
    last_unsupported_command_.reset();
    ++command_count_;
    if (recent_commands_.size() == command_history_capacity) {
        recent_commands_.pop_front();
    }
    recent_commands_.push_back(Ps1CdromCommandEvent{
        command,
        index_,
        status_byte_,
    });

    switch (command) {
        case 0x01u: // Getstat
            if (!push_response(status_byte_)) return {R3000aBusStatus::unsupported, 0u};
            interrupt_flags_ = 0x03u;
            return {R3000aBusStatus::ok, 0u};

        case 0x02u: { // Setloc
            if (parameters_.size() < 3u) return {R3000aBusStatus::unsupported, 0u};
            const std::array<std::uint8_t, 3> msf{
                parameters_[0], parameters_[1], parameters_[2]};
            std::uint32_t minute = 0u;
            std::uint32_t second = 0u;
            std::uint32_t frame = 0u;
            if (!bcd_to_binary(msf[0], minute) ||
                !bcd_to_binary(msf[1], second) ||
                !bcd_to_binary(msf[2], frame) ||
                second >= 60u || frame >= 75u) {
                return {R3000aBusStatus::unsupported, 0u};
            }
            const std::uint64_t absolute_frame =
                static_cast<std::uint64_t>(minute) * 60u * 75u +
                static_cast<std::uint64_t>(second) * 75u + frame;
            if (absolute_frame < 150u) return {R3000aBusStatus::unsupported, 0u};
            current_lba_ = absolute_frame - 150u;
            parameters_.clear();
            if (!push_response(status_byte_)) return {R3000aBusStatus::unsupported, 0u};
            interrupt_flags_ = 0x03u;
            return {R3000aBusStatus::ok, 0u};
        }

        case 0x06u: { // ReadN: INT3 acknowledge, then INT1+sector
            if (disc_ == nullptr) return {R3000aBusStatus::unsupported, 0u};
            auto sector = disc_->read_sectors(current_lba_, 1u);
            if (!sector || sector.value.size() != data_capacity) {
                return {R3000aBusStatus::unsupported, 0u};
            }
            data_.clear();
            const auto reading_status = static_cast<std::uint8_t>(
                (status_byte_ & ~kStatActivityMask) |
                kStatMotor | kStatRead);
            deferred_responses_.push_back(DeferredResponse{
                0x01u,
                reading_status,
                kCdSectorCycles,
                std::move(sector.value),
                true,
                true,
            });
            if (!push_response(status_byte_)) return {R3000aBusStatus::unsupported, 0u};
            interrupt_flags_ = 0x03u;
            return {R3000aBusStatus::ok, 0u};
        }

        case 0x07u: { // MotorOn
            data_.clear();
            const auto completed_status = static_cast<std::uint8_t>(
                (status_byte_ & ~kStatActivityMask) | kStatMotor);
            deferred_responses_.push_back(DeferredResponse{
                0x02u,
                completed_status,
                kCdCommandCompletionCycles,
                {},
                false,
                true,
            });
            if (!push_response(status_byte_)) return {R3000aBusStatus::unsupported, 0u};
            interrupt_flags_ = 0x03u;
            return {R3000aBusStatus::ok, 0u};
        }

        case 0x08u: { // Stop
            data_.clear();
            status_byte_ = static_cast<std::uint8_t>(
                status_byte_ & ~kStatActivityMask);
            deferred_responses_.push_back(DeferredResponse{
                0x02u,
                static_cast<std::uint8_t>(status_byte_ & ~kStatMotor),
                kCdCommandCompletionCycles,
                {},
                false,
                true,
            });
            if (!push_response(status_byte_)) return {R3000aBusStatus::unsupported, 0u};
            interrupt_flags_ = 0x03u;
            return {R3000aBusStatus::ok, 0u};
        }

        case 0x09u: { // Pause
            data_.clear();
            const auto completed_status = static_cast<std::uint8_t>(
                (status_byte_ & ~kStatActivityMask) |
                (status_byte_ & kStatMotor));
            deferred_responses_.push_back(DeferredResponse{
                0x02u,
                completed_status,
                kCdCommandCompletionCycles,
                {},
                false,
                true,
            });
            if (!push_response(status_byte_)) return {R3000aBusStatus::unsupported, 0u};
            interrupt_flags_ = 0x03u;
            return {R3000aBusStatus::ok, 0u};
        }

        case 0x0Bu: // Mute
            muted_ = true;
            if (!push_response(status_byte_)) {
                return {R3000aBusStatus::unsupported, 0u};
            }
            interrupt_flags_ = 0x03u;
            return {R3000aBusStatus::ok, 0u};

        case 0x0Cu: // Demute
            muted_ = false;
            if (!push_response(status_byte_)) {
                return {R3000aBusStatus::unsupported, 0u};
            }
            interrupt_flags_ = 0x03u;
            return {R3000aBusStatus::ok, 0u};

        case 0x13u: // GetTN: title disc has one data track.
            parameters_.clear();
            if (!push_response(status_byte_) ||
                !push_response(0x01u) ||
                !push_response(0x01u)) {
                return {R3000aBusStatus::unsupported, 0u};
            }
            interrupt_flags_ = 0x03u;
            return {R3000aBusStatus::ok, 0u};

        case 0x14u: { // GetTD(track): track 01 or 00=lead-out.
            if (disc_ == nullptr || parameters_.size() != 1u) {
                return {R3000aBusStatus::unsupported, 0u};
            }
            const auto track_bcd = parameters_.front();
            parameters_.clear();

            std::uint32_t track = 0u;
            if (!bcd_to_binary(track_bcd, track) || track > 1u) {
                const auto error_status =
                    static_cast<std::uint8_t>(status_byte_ | 0x01u);
                if (!push_response(error_status) ||
                    !push_response(0x10u)) {
                    return {R3000aBusStatus::unsupported, 0u};
                }
                interrupt_flags_ = 0x05u;
                return {R3000aBusStatus::ok, 0u};
            }

            std::uint64_t absolute_frames = 150u;
            if (track == 0u) {
                absolute_frames += disc_->logical_sector_count();
            }
            const auto total_seconds =
                absolute_frames / 75u;
            const auto minute = static_cast<std::uint32_t>(
                total_seconds / 60u);
            const auto second = static_cast<std::uint32_t>(
                total_seconds % 60u);

            if (!push_response(status_byte_) ||
                !push_response(binary_to_bcd(minute)) ||
                !push_response(binary_to_bcd(second))) {
                return {R3000aBusStatus::unsupported, 0u};
            }
            interrupt_flags_ = 0x03u;
            return {R3000aBusStatus::ok, 0u};
        }

        case 0x0Au: { // Init: preserve host HINTMSK, INT3 then INT2
            const auto* attached = disc_;
            const auto interrupt_enable = interrupt_enable_;
            clear_transfer_fifos();
            deferred_responses_.clear();
            disc_ = attached;
            index_ = 0u;
            interrupt_enable_ = interrupt_enable;
            interrupt_flags_ = 0u;
            status_byte_ = 0u;
            current_lba_ = 0u;
            last_unsupported_command_.reset();
            deferred_responses_.push_back(DeferredResponse{
                0x02u,
                kStatMotor,
                kCdCommandCompletionCycles,
                {},
                false,
                true,
            });
            if (!push_response(status_byte_)) return {R3000aBusStatus::unsupported, 0u};
            interrupt_flags_ = 0x03u;
            return {R3000aBusStatus::ok, 0u};
        }

        default:
            last_unsupported_command_ = command;
            return {R3000aBusStatus::unsupported, 0u};
    }
}

} // namespace jojo
