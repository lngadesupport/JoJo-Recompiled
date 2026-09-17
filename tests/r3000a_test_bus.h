#pragma once

#include "core/r3000a_bus.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

class TestR3000aBus final : public jojo::R3000aBus {
public:
    jojo::R3000aBusResult read8(std::uint32_t address) noexcept override {
        if (unsupported_.contains(address)) {
            return {jojo::R3000aBusStatus::unsupported, 0u};
        }
        if (bus_error_.contains(address)) {
            return {jojo::R3000aBusStatus::bus_error, 0u};
        }
        const auto it = bytes_.find(address);
        return {jojo::R3000aBusStatus::ok, it == bytes_.end() ? 0u : it->second};
    }

    jojo::R3000aBusResult read16(std::uint32_t address) noexcept override {
        const auto lo = read8(address);
        if (lo.status != jojo::R3000aBusStatus::ok) return lo;
        const auto hi = read8(address + 1u);
        if (hi.status != jojo::R3000aBusStatus::ok) return hi;
        return {jojo::R3000aBusStatus::ok, lo.value | (hi.value << 8)};
    }

    jojo::R3000aBusResult read32(std::uint32_t address) noexcept override {
        last_read32_address_ = address;
        std::uint32_t value = 0u;
        for (std::uint32_t i = 0; i < 4u; ++i) {
            const auto byte = read8(address + i);
            if (byte.status != jojo::R3000aBusStatus::ok) return byte;
            value |= byte.value << (8u * i);
        }
        return {jojo::R3000aBusStatus::ok, value};
    }

    jojo::R3000aBusResult write8(std::uint32_t address, std::uint8_t value) noexcept override {
        if (unsupported_.contains(address)) {
            return {jojo::R3000aBusStatus::unsupported, 0u};
        }
        if (bus_error_.contains(address)) {
            return {jojo::R3000aBusStatus::bus_error, 0u};
        }
        bytes_[address] = value;
        return {jojo::R3000aBusStatus::ok, 0u};
    }

    jojo::R3000aBusResult write16(std::uint32_t address, std::uint16_t value) noexcept override {
        const auto a = write8(address, static_cast<std::uint8_t>(value & 0xffu));
        if (a.status != jojo::R3000aBusStatus::ok) return a;
        return write8(address + 1u, static_cast<std::uint8_t>((value >> 8) & 0xffu));
    }

    jojo::R3000aBusResult write32(std::uint32_t address, std::uint32_t value) noexcept override {
        last_write32_address_ = address;
        for (std::uint32_t i = 0; i < 4u; ++i) {
            const auto out = write8(address + i, static_cast<std::uint8_t>((value >> (8u * i)) & 0xffu));
            if (out.status != jojo::R3000aBusStatus::ok) return out;
        }
        return {jojo::R3000aBusStatus::ok, 0u};
    }

    void store8(std::uint32_t address, std::uint8_t value) { bytes_[address] = value; }
    void store16(std::uint32_t address, std::uint16_t value) {
        store8(address, static_cast<std::uint8_t>(value & 0xffu));
        store8(address + 1u, static_cast<std::uint8_t>((value >> 8) & 0xffu));
    }
    void store32(std::uint32_t address, std::uint32_t value) {
        for (std::uint32_t i = 0; i < 4u; ++i) {
            store8(address + i, static_cast<std::uint8_t>((value >> (8u * i)) & 0xffu));
        }
    }

    [[nodiscard]] std::uint32_t peek32(std::uint32_t address) const {
        std::uint32_t value = 0u;
        for (std::uint32_t i = 0; i < 4u; ++i) {
            const auto it = bytes_.find(address + i);
            const auto byte = it == bytes_.end() ? 0u : static_cast<std::uint32_t>(it->second);
            value |= byte << (8u * i);
        }
        return value;
    }

    [[nodiscard]] std::uint32_t last_read32_address() const noexcept { return last_read32_address_; }
    [[nodiscard]] std::uint32_t last_write32_address() const noexcept { return last_write32_address_; }

    void fail_bus_error(std::uint32_t address) { bus_error_.insert(address); }
    void fail_unsupported(std::uint32_t address) { unsupported_.insert(address); }

private:
    std::unordered_map<std::uint32_t, std::uint8_t> bytes_;
    std::unordered_set<std::uint32_t> bus_error_;
    std::unordered_set<std::uint32_t> unsupported_;
    std::uint32_t last_read32_address_{};
    std::uint32_t last_write32_address_{};
};
