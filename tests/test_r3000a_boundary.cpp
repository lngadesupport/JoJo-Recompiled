#include "core/r3000a_reference_executor.h"
#include "r3000a_test_bus.h"

#include <array>
#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

constexpr std::uint32_t cop2(std::uint8_t rs, std::uint8_t rt = 8u, std::uint8_t rd = 3u) noexcept {
    return (0x12u << 26) |
           (std::uint32_t(rs) << 21) |
           (std::uint32_t(rt) << 16) |
           (std::uint32_t(rd) << 11) |
           1u;
}

static jojo::R3000aState base_state() {
    jojo::R3000aState s{};
    s.pc = 0x1000u;
    s.next_pc = 0x1004u;
    s.gpr[8] = 0xAABBCCDDu;
    return s;
}

int main() {
    constexpr std::uint32_t kCu2 = 1u << 30;
    constexpr std::uint32_t kCeMask = 3u << 28;
    constexpr std::uint32_t kCe2 = 2u << 28;
    constexpr std::uint32_t kExcCodeMask = 0x7Cu;
    constexpr std::uint32_t kCpuExcCode = 11u << 2;

    const std::array<std::uint32_t, 5> operations = {
        cop2(0x00u), // MFC2
        cop2(0x02u), // CFC2
        cop2(0x04u), // MTC2
        cop2(0x06u), // CTC2
        cop2(0x10u), // COP2 command
    };

    // CU2 clear: every decoded COP2 operation raises architectural CpU with CE=2.
    for (const auto raw : operations) {
        TestR3000aBus bus;
        auto s = base_state();
        s.cop0.status &= ~kCu2;
        bus.store32(0x1000u, raw);

        const auto r = jojo::step_r3000a(s, bus);
        CHECK(r.status == jojo::R3000aStepStatus::exception);
        CHECK(r.diagnostic.exception_code == jojo::R3000aExceptionCode::coprocessor_unusable);
        CHECK(r.diagnostic.stage == jojo::R3000aStage::cop2);
        CHECK(r.diagnostic.coprocessor && *r.diagnostic.coprocessor == 2u);
        CHECK((s.cop0.cause & kExcCodeMask) == kCpuExcCode);
        CHECK((s.cop0.cause & kCeMask) == kCe2);
        CHECK(s.cop0.epc == 0x1000u);
        CHECK(s.pc == 0x80000080u && s.next_pc == 0x80000084u);
    }

    // CU2 set: GTE/COP2 is deliberately not implemented in M2 and must stop explicitly.
    for (const auto raw : operations) {
        TestR3000aBus bus;
        auto s = base_state();
        s.cop0.status |= kCu2;
        const auto status_before = s.cop0.status;
        const auto cause_before = s.cop0.cause;
        bus.store32(0x1000u, raw);

        const auto r = jojo::step_r3000a(s, bus);
        CHECK(r.status == jojo::R3000aStepStatus::boundary);
        CHECK(r.diagnostic.boundary == jojo::R3000aBoundaryCode::cop2_unimplemented);
        CHECK(r.diagnostic.stage == jojo::R3000aStage::cop2);
        CHECK(r.diagnostic.pc == 0x1000u);
        CHECK(r.diagnostic.opcode && *r.diagnostic.opcode == raw);
        CHECK(s.pc == 0x1000u && s.next_pc == 0x1004u);
        CHECK(s.cop0.status == status_before);
        CHECK(s.cop0.cause == cause_before);
        CHECK(s.gpr[8] == 0xAABBCCDDu);
        CHECK(!s.pending_load.valid);
    }

    return failures ? 1 : 0;
}
