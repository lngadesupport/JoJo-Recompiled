#include "core/sha256.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace jojo {
namespace {

constexpr std::array<std::uint32_t, 64> k{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

constexpr std::uint32_t rotr(std::uint32_t x, unsigned n) noexcept {
    return (x >> n) | (x << (32u - n));
}

}

Result<void> Sha256Hasher::update(std::span<const std::uint8_t> bytes) noexcept {
    if (finalized_) {
        return Result<void>::failure(ErrorCode::invalid_argument, "SHA-256 hasher is already finalized");
    }
    total_bytes_ += bytes.size();
    while (!bytes.empty()) {
        const std::size_t take = std::min<std::size_t>(64u - buffer_size_, bytes.size());
        std::copy_n(bytes.begin(), take, buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_));
        buffer_size_ += take;
        bytes = bytes.subspan(take);
        if (buffer_size_ == 64u) {
            transform(buffer_.data());
            buffer_size_ = 0u;
        }
    }
    return Result<void>::success();
}

Result<Sha256Digest> Sha256Hasher::finalize() noexcept {
    if (finalized_) {
        return Result<Sha256Digest>::failure(ErrorCode::invalid_argument, "SHA-256 hasher is already finalized");
    }
    finalized_ = true;
    const std::uint64_t bit_length = total_bytes_ * 8u;
    buffer_[buffer_size_++] = 0x80u;
    if (buffer_size_ > 56u) {
        std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_), buffer_.end(), 0u);
        transform(buffer_.data());
        buffer_size_ = 0u;
    }
    std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_), buffer_.begin() + 56, 0u);
    for (unsigned i = 0; i < 8; ++i) {
        buffer_[63u - i] = static_cast<std::uint8_t>(bit_length >> (i * 8u));
    }
    transform(buffer_.data());

    Sha256Digest digest{};
    for (std::size_t i = 0; i < state_.size(); ++i) {
        digest[i * 4u + 0u] = static_cast<std::uint8_t>(state_[i] >> 24u);
        digest[i * 4u + 1u] = static_cast<std::uint8_t>(state_[i] >> 16u);
        digest[i * 4u + 2u] = static_cast<std::uint8_t>(state_[i] >> 8u);
        digest[i * 4u + 3u] = static_cast<std::uint8_t>(state_[i]);
    }
    return Result<Sha256Digest>::success(digest);
}

void Sha256Hasher::transform(const std::uint8_t* block) noexcept {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
        const std::size_t o = i * 4u;
        w[i] = (static_cast<std::uint32_t>(block[o]) << 24u) |
               (static_cast<std::uint32_t>(block[o + 1u]) << 16u) |
               (static_cast<std::uint32_t>(block[o + 2u]) << 8u) |
               static_cast<std::uint32_t>(block[o + 3u]);
    }
    for (std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15u], 7u) ^ rotr(w[i - 15u], 18u) ^ (w[i - 15u] >> 3u);
        const std::uint32_t s1 = rotr(w[i - 2u], 17u) ^ rotr(w[i - 2u], 19u) ^ (w[i - 2u] >> 10u);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];

    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr(e, 6u) ^ rotr(e, 11u) ^ rotr(e, 25u);
        const std::uint32_t ch = (e & f) ^ ((~e) & g);
        const std::uint32_t temp1 = h + s1 + ch + k[i] + w[i];
        const std::uint32_t s0 = rotr(a, 2u) ^ rotr(a, 13u) ^ rotr(a, 22u);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

Sha256Digest sha256(std::span<const std::uint8_t> bytes) noexcept {
    Sha256Hasher hasher;
    (void)hasher.update(bytes);
    const auto digest = hasher.finalize();
    return digest ? digest.value : Sha256Digest{};
}

Result<Sha256Digest> sha256_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Result<Sha256Digest>::failure(ErrorCode::file_not_found, "failed to open file for SHA-256: " + path.string());
    }

    Sha256Hasher hasher;
    std::array<char, 64 * 1024> buffer{};
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = in.gcount();
        if (count > 0) {
            const auto updated = hasher.update(std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(buffer.data()), static_cast<std::size_t>(count)));
            if (!updated) return Result<Sha256Digest>::failure(updated.error, updated.detail);
        }
    }
    if (!in.eof()) {
        return Result<Sha256Digest>::failure(ErrorCode::io_error, "failed while reading file for SHA-256: " + path.string());
    }
    return hasher.finalize();
}

std::string sha256_hex(const Sha256Digest& digest) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto byte : digest) out << std::setw(2) << static_cast<unsigned>(byte);
    return out.str();
}

}
