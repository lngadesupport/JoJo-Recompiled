#include "core/ps1_spu.h"

#include <algorithm>
#include <array>

namespace jojo {
namespace {

constexpr std::uint32_t kVoiceBase = 0x1F801C00u;
constexpr std::uint32_t kVoiceEnd = 0x1F801D7Fu;
constexpr std::uint32_t kVoiceStride = 0x10u;
constexpr std::uint32_t kMainVolumeLeft = 0x1F801D80u;
constexpr std::uint32_t kMainVolumeRight = 0x1F801D82u;
constexpr std::uint32_t kReverbVolumeLeft = 0x1F801D84u;
constexpr std::uint32_t kReverbVolumeRight = 0x1F801D86u;
constexpr std::uint32_t kKeyOnLow = 0x1F801D88u;
constexpr std::uint32_t kKeyOnHigh = 0x1F801D8Au;
constexpr std::uint32_t kKeyOffLow = 0x1F801D8Cu;
constexpr std::uint32_t kKeyOffHigh = 0x1F801D8Eu;
constexpr std::uint32_t kPitchModLow = 0x1F801D90u;
constexpr std::uint32_t kPitchModHigh = 0x1F801D92u;
constexpr std::uint32_t kNoiseLow = 0x1F801D94u;
constexpr std::uint32_t kNoiseHigh = 0x1F801D96u;
constexpr std::uint32_t kReverbLow = 0x1F801D98u;
constexpr std::uint32_t kReverbHigh = 0x1F801D9Au;
constexpr std::uint32_t kEndxLow = 0x1F801D9Cu;
constexpr std::uint32_t kEndxHigh = 0x1F801D9Eu;
constexpr std::uint32_t kReverbWorkStart = 0x1F801DA2u;
constexpr std::uint32_t kIrqAddress = 0x1F801DA4u;
constexpr std::uint32_t kTransferAddress = 0x1F801DA6u;
constexpr std::uint32_t kTransferFifo = 0x1F801DA8u;
constexpr std::uint32_t kControl = 0x1F801DAAu;
constexpr std::uint32_t kTransferControl = 0x1F801DACu;
constexpr std::uint32_t kStatus = 0x1F801DAEu;
constexpr std::uint32_t kCdVolumeLeft = 0x1F801DB0u;
constexpr std::uint32_t kCdVolumeRight = 0x1F801DB2u;
constexpr std::uint32_t kExternalVolumeLeft = 0x1F801DB4u;
constexpr std::uint32_t kExternalVolumeRight = 0x1F801DB6u;
constexpr std::uint32_t kCurrentMainVolumeLeft = 0x1F801DB8u;
constexpr std::uint32_t kCurrentMainVolumeRight = 0x1F801DBAu;
constexpr std::uint32_t kReverbConfigBase = 0x1F801DC0u;
constexpr std::uint32_t kReverbConfigEnd = 0x1F801DFFu;
constexpr std::uint32_t kValidVoiceMask = 0x00FFFFFFu;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= kFnvPrime;
}

void hash_bool(std::uint64_t& hash, bool value) noexcept {
    hash_byte(hash, static_cast<std::uint8_t>(value ? 1u : 0u));
}

void hash_u16(std::uint64_t& hash, std::uint16_t value) noexcept {
    hash_byte(hash, static_cast<std::uint8_t>(value));
    hash_byte(hash, static_cast<std::uint8_t>(value >> 8u));
}

void hash_u32(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (unsigned shift = 0; shift < 32u; shift += 8u) {
        hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

} // namespace

Ps1Spu::Ps1Spu() : sound_ram_(sound_ram_size, 0u) {}

Ps1SpuDecodedBlock Ps1Spu::decode_adpcm_block(
    std::span<const std::uint8_t, 16> block,
    Ps1SpuAdpcmHistory& history) noexcept {
    static constexpr std::array<std::int32_t, 5> positive{
        0, 60, 115, 98, 122};
    static constexpr std::array<std::int32_t, 5> negative{
        0, 0, -52, -55, -60};

    Ps1SpuDecodedBlock decoded{};
    decoded.flags = block[1];

    const auto shift = std::min<std::uint32_t>(block[0] & 0x0Fu, 12u);
    const auto raw_filter = static_cast<std::uint32_t>((block[0] >> 4u) & 0x0Fu);
    const auto filter = std::min<std::uint32_t>(raw_filter, 4u);

    std::size_t output = 0u;
    for (std::size_t byte_index = 2u; byte_index < block.size(); ++byte_index) {
        const auto packed = block[byte_index];
        for (unsigned half = 0u; half < 2u; ++half) {
            const auto nibble_raw = static_cast<std::uint8_t>(
                half == 0u ? (packed & 0x0Fu) : (packed >> 4u));
            const auto nibble = static_cast<std::int32_t>(
                nibble_raw >= 8u
                    ? static_cast<int>(nibble_raw) - 16
                    : static_cast<int>(nibble_raw));

            std::int32_t sample = (nibble * 4096) >> shift;
            const std::int32_t predicted =
                (history.previous * positive[filter] +
                 history.older * negative[filter] + 32) >> 6;
            sample += predicted;
            sample = std::clamp<std::int32_t>(sample, -32768, 32767);

            decoded.samples[output++] = static_cast<std::int16_t>(sample);
            history.older = history.previous;
            history.previous = sample;
        }
    }
    return decoded;
}


bool Ps1Spu::decode_voice_register(
    std::uint32_t physical,
    std::size_t& voice_index,
    std::uint32_t& offset) const noexcept {
    if (physical < kVoiceBase || physical > kVoiceEnd ||
        (physical & 1u) != 0u) {
        return false;
    }
    const auto relative = physical - kVoiceBase;
    voice_index = static_cast<std::size_t>(relative / kVoiceStride);
    offset = relative % kVoiceStride;
    return voice_index < voices_.size() && offset <= 0x0Eu;
}

std::uint16_t Ps1Spu::read_flag_half(
    std::uint32_t flags,
    bool high) const noexcept {
    return static_cast<std::uint16_t>(
        high ? ((flags >> 16u) & 0x00FFu) : (flags & 0xFFFFu));
}

void Ps1Spu::write_flag_half(
    std::uint32_t& flags,
    bool high,
    std::uint16_t value) noexcept {
    if (high) {
        flags = (flags & 0x0000FFFFu) |
                ((static_cast<std::uint32_t>(value) & 0x00FFu) << 16u);
    } else {
        flags = (flags & 0x00FF0000u) | value;
    }
    flags &= kValidVoiceMask;
}

void Ps1Spu::key_on(bool high, std::uint16_t mask) noexcept {
    write_flag_half(key_on_latch_, high, mask);
    const std::uint32_t expanded =
        (static_cast<std::uint32_t>(mask) << (high ? 16u : 0u)) &
        kValidVoiceMask;
    for (std::size_t i = 0; i < voices_.size(); ++i) {
        if ((expanded & (1u << i)) == 0u) continue;
        auto& voice_state = voices_[i];
        voice_state.keyed_on = true;
        voice_state.releasing = false;
        voice_state.adsr_volume = 0u;
        voice_state.current_address =
            (static_cast<std::uint32_t>(voice_state.start_address) * 8u) %
            static_cast<std::uint32_t>(sound_ram_size);
        endx_flags_ &= ~(1u << i);
    }
}

void Ps1Spu::key_off(bool high, std::uint16_t mask) noexcept {
    write_flag_half(key_off_latch_, high, mask);
    const std::uint32_t expanded =
        (static_cast<std::uint32_t>(mask) << (high ? 16u : 0u)) &
        kValidVoiceMask;
    for (std::size_t i = 0; i < voices_.size(); ++i) {
        if ((expanded & (1u << i)) == 0u) continue;
        voices_[i].keyed_on = false;
        voices_[i].releasing = true;
    }
}

void Ps1Spu::write_sound_ram16(std::uint16_t value) noexcept {
    const auto address =
        transfer_current_address_ % static_cast<std::uint32_t>(sound_ram_size);
    sound_ram_[address] = static_cast<std::uint8_t>(value);
    sound_ram_[(address + 1u) % sound_ram_size] =
        static_cast<std::uint8_t>(value >> 8u);
    transfer_current_address_ =
        (address + 2u) % static_cast<std::uint32_t>(sound_ram_size);
}

std::uint16_t Ps1Spu::read_sound_ram16() noexcept {
    const auto address =
        transfer_current_address_ % static_cast<std::uint32_t>(sound_ram_size);
    const auto value = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(sound_ram_[address]) |
        (static_cast<std::uint16_t>(
             sound_ram_[(address + 1u) % sound_ram_size])
         << 8u));
    transfer_current_address_ =
        (address + 2u) % static_cast<std::uint32_t>(sound_ram_size);
    return value;
}

R3000aBusResult Ps1Spu::read16(std::uint32_t physical) noexcept {
    std::size_t voice_index = 0u;
    std::uint32_t offset = 0u;
    if (decode_voice_register(physical, voice_index, offset)) {
        const auto& v = voices_[voice_index];
        switch (offset) {
            case 0x00u: return {R3000aBusStatus::ok, v.volume_left};
            case 0x02u: return {R3000aBusStatus::ok, v.volume_right};
            case 0x04u: return {R3000aBusStatus::ok, v.pitch};
            case 0x06u: return {R3000aBusStatus::ok, v.start_address};
            case 0x08u: return {R3000aBusStatus::ok, v.adsr1};
            case 0x0Au: return {R3000aBusStatus::ok, v.adsr2};
            case 0x0Cu: return {R3000aBusStatus::ok, v.adsr_volume};
            case 0x0Eu: return {R3000aBusStatus::ok, v.repeat_address};
            default: break;
        }
    }

    switch (physical) {
        case kMainVolumeLeft: return {R3000aBusStatus::ok, main_volume_left_};
        case kMainVolumeRight: return {R3000aBusStatus::ok, main_volume_right_};
        case kReverbVolumeLeft: return {R3000aBusStatus::ok, reverb_volume_left_};
        case kReverbVolumeRight: return {R3000aBusStatus::ok, reverb_volume_right_};
        case kKeyOnLow: return {R3000aBusStatus::ok, read_flag_half(key_on_latch_, false)};
        case kKeyOnHigh: return {R3000aBusStatus::ok, read_flag_half(key_on_latch_, true)};
        case kKeyOffLow: return {R3000aBusStatus::ok, read_flag_half(key_off_latch_, false)};
        case kKeyOffHigh: return {R3000aBusStatus::ok, read_flag_half(key_off_latch_, true)};
        case kPitchModLow: return {R3000aBusStatus::ok, read_flag_half(pitch_modulation_flags_, false)};
        case kPitchModHigh: return {R3000aBusStatus::ok, read_flag_half(pitch_modulation_flags_, true)};
        case kNoiseLow: return {R3000aBusStatus::ok, read_flag_half(noise_flags_, false)};
        case kNoiseHigh: return {R3000aBusStatus::ok, read_flag_half(noise_flags_, true)};
        case kReverbLow: return {R3000aBusStatus::ok, read_flag_half(reverb_flags_, false)};
        case kReverbHigh: return {R3000aBusStatus::ok, read_flag_half(reverb_flags_, true)};
        case kEndxLow: return {R3000aBusStatus::ok, read_flag_half(endx_flags_, false)};
        case kEndxHigh: return {R3000aBusStatus::ok, read_flag_half(endx_flags_, true)};
        case kReverbWorkStart: return {R3000aBusStatus::ok, reverb_work_start_};
        case kIrqAddress: return {R3000aBusStatus::ok, irq_address_};
        case kTransferAddress: return {R3000aBusStatus::ok, transfer_address_};
        case kControl: return {R3000aBusStatus::ok, control_};
        case kTransferControl: return {R3000aBusStatus::ok, transfer_control_};
        case kStatus: return {R3000aBusStatus::ok, status_};
        case kCdVolumeLeft: return {R3000aBusStatus::ok, cd_volume_left_};
        case kCdVolumeRight: return {R3000aBusStatus::ok, cd_volume_right_};
        case kExternalVolumeLeft: return {R3000aBusStatus::ok, external_volume_left_};
        case kExternalVolumeRight: return {R3000aBusStatus::ok, external_volume_right_};
        case kCurrentMainVolumeLeft: return {R3000aBusStatus::ok, main_volume_left_};
        case kCurrentMainVolumeRight: return {R3000aBusStatus::ok, main_volume_right_};
        default: break;
    }

    if (physical >= kReverbConfigBase && physical <= kReverbConfigEnd &&
        (physical & 1u) == 0u) {
        const auto index = (physical - kReverbConfigBase) / 2u;
        return {R3000aBusStatus::ok, reverb_registers_[index]};
    }

    return {R3000aBusStatus::unsupported, 0u};
}

R3000aBusResult Ps1Spu::write16(
    std::uint32_t physical,
    std::uint16_t value) noexcept {
    std::size_t voice_index = 0u;
    std::uint32_t offset = 0u;
    if (decode_voice_register(physical, voice_index, offset)) {
        auto& v = voices_[voice_index];
        switch (offset) {
            case 0x00u: v.volume_left = value; return {R3000aBusStatus::ok, 0u};
            case 0x02u: v.volume_right = value; return {R3000aBusStatus::ok, 0u};
            case 0x04u: v.pitch = value; return {R3000aBusStatus::ok, 0u};
            case 0x06u: v.start_address = value; return {R3000aBusStatus::ok, 0u};
            case 0x08u: v.adsr1 = value; return {R3000aBusStatus::ok, 0u};
            case 0x0Au: v.adsr2 = value; return {R3000aBusStatus::ok, 0u};
            case 0x0Cu: v.adsr_volume = value; return {R3000aBusStatus::ok, 0u};
            case 0x0Eu: v.repeat_address = value; return {R3000aBusStatus::ok, 0u};
            default: break;
        }
    }

    switch (physical) {
        case kMainVolumeLeft: main_volume_left_ = value; return {R3000aBusStatus::ok, 0u};
        case kMainVolumeRight: main_volume_right_ = value; return {R3000aBusStatus::ok, 0u};
        case kReverbVolumeLeft: reverb_volume_left_ = value; return {R3000aBusStatus::ok, 0u};
        case kReverbVolumeRight: reverb_volume_right_ = value; return {R3000aBusStatus::ok, 0u};
        case kKeyOnLow: key_on(false, value); return {R3000aBusStatus::ok, 0u};
        case kKeyOnHigh: key_on(true, value); return {R3000aBusStatus::ok, 0u};
        case kKeyOffLow: key_off(false, value); return {R3000aBusStatus::ok, 0u};
        case kKeyOffHigh: key_off(true, value); return {R3000aBusStatus::ok, 0u};
        case kPitchModLow: write_flag_half(pitch_modulation_flags_, false, value); return {R3000aBusStatus::ok, 0u};
        case kPitchModHigh: write_flag_half(pitch_modulation_flags_, true, value); return {R3000aBusStatus::ok, 0u};
        case kNoiseLow: write_flag_half(noise_flags_, false, value); return {R3000aBusStatus::ok, 0u};
        case kNoiseHigh: write_flag_half(noise_flags_, true, value); return {R3000aBusStatus::ok, 0u};
        case kReverbLow: write_flag_half(reverb_flags_, false, value); return {R3000aBusStatus::ok, 0u};
        case kReverbHigh: write_flag_half(reverb_flags_, true, value); return {R3000aBusStatus::ok, 0u};
        case kReverbWorkStart: reverb_work_start_ = value; return {R3000aBusStatus::ok, 0u};
        case kIrqAddress: irq_address_ = value; return {R3000aBusStatus::ok, 0u};
        case kTransferAddress:
            transfer_address_ = value;
            transfer_current_address_ =
                (static_cast<std::uint32_t>(value) * 8u) %
                static_cast<std::uint32_t>(sound_ram_size);
            return {R3000aBusStatus::ok, 0u};
        case kTransferFifo:
            write_sound_ram16(value);
            return {R3000aBusStatus::ok, 0u};
        case kControl:
            control_ = value;
            status_ = static_cast<std::uint16_t>(
                (status_ & 0xFFC0u) | (control_ & 0x003Fu));
            return {R3000aBusStatus::ok, 0u};
        case kTransferControl:
            transfer_control_ = value;
            return {R3000aBusStatus::ok, 0u};
        case kCdVolumeLeft: cd_volume_left_ = value; return {R3000aBusStatus::ok, 0u};
        case kCdVolumeRight: cd_volume_right_ = value; return {R3000aBusStatus::ok, 0u};
        case kExternalVolumeLeft: external_volume_left_ = value; return {R3000aBusStatus::ok, 0u};
        case kExternalVolumeRight: external_volume_right_ = value; return {R3000aBusStatus::ok, 0u};
        case kEndxLow:
        case kEndxHigh:
        case kStatus:
        case kCurrentMainVolumeLeft:
        case kCurrentMainVolumeRight:
            return {R3000aBusStatus::unsupported, 0u};
        default:
            break;
    }

    if (physical >= kReverbConfigBase && physical <= kReverbConfigEnd &&
        (physical & 1u) == 0u) {
        const auto index = (physical - kReverbConfigBase) / 2u;
        reverb_registers_[index] = value;
        return {R3000aBusStatus::ok, 0u};
    }

    return {R3000aBusStatus::unsupported, 0u};
}

bool Ps1Spu::dma_write_words(std::span<const std::uint32_t> words) noexcept {
    for (const auto value : words) {
        write_sound_ram16(static_cast<std::uint16_t>(value));
        write_sound_ram16(static_cast<std::uint16_t>(value >> 16u));
    }
    return true;
}

bool Ps1Spu::dma_read_words(std::span<std::uint32_t> words) noexcept {
    for (auto& value : words) {
        const auto low = static_cast<std::uint32_t>(read_sound_ram16());
        const auto high = static_cast<std::uint32_t>(read_sound_ram16());
        value = low | (high << 16u);
    }
    return true;
}

const Ps1SpuVoiceState& Ps1Spu::voice(std::size_t index) const noexcept {
    static const Ps1SpuVoiceState empty{};
    return index < voices_.size() ? voices_[index] : empty;
}

std::uint32_t Ps1Spu::endx_flags() const noexcept {
    return endx_flags_ & kValidVoiceMask;
}

std::uint32_t Ps1Spu::transfer_current_address() const noexcept {
    return transfer_current_address_;
}

std::uint8_t Ps1Spu::sound_ram_byte(std::uint32_t address) const noexcept {
    return sound_ram_[address % static_cast<std::uint32_t>(sound_ram_size)];
}

std::uint64_t Ps1Spu::diagnostic_state_hash() const noexcept {
    std::uint64_t hash = kFnvOffset;
    for (const auto& v : voices_) {
        hash_u16(hash, v.volume_left);
        hash_u16(hash, v.volume_right);
        hash_u16(hash, v.pitch);
        hash_u16(hash, v.start_address);
        hash_u16(hash, v.adsr1);
        hash_u16(hash, v.adsr2);
        hash_u16(hash, v.adsr_volume);
        hash_u16(hash, v.repeat_address);
        hash_bool(hash, v.keyed_on);
        hash_bool(hash, v.releasing);
        hash_u32(hash, v.current_address);
    }
    hash_u16(hash, main_volume_left_);
    hash_u16(hash, main_volume_right_);
    hash_u16(hash, reverb_volume_left_);
    hash_u16(hash, reverb_volume_right_);
    hash_u32(hash, key_on_latch_);
    hash_u32(hash, key_off_latch_);
    hash_u32(hash, pitch_modulation_flags_);
    hash_u32(hash, noise_flags_);
    hash_u32(hash, reverb_flags_);
    hash_u32(hash, endx_flags_);
    hash_u16(hash, reverb_work_start_);
    hash_u16(hash, irq_address_);
    hash_u16(hash, transfer_address_);
    hash_u32(hash, transfer_current_address_);
    hash_u16(hash, control_);
    hash_u16(hash, transfer_control_);
    hash_u16(hash, status_);
    hash_u16(hash, cd_volume_left_);
    hash_u16(hash, cd_volume_right_);
    hash_u16(hash, external_volume_left_);
    hash_u16(hash, external_volume_right_);
    for (const auto value : reverb_registers_) hash_u16(hash, value);
    for (const auto value : sound_ram_) hash_byte(hash, value);
    return hash;
}

} // namespace jojo
