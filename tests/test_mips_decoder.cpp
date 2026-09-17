#include "core/mips_decoder.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

int main() {
    const auto addu = jojo::decode_mips(0x012A4021u); // addu $t0,$t1,$t2
    CHECK(addu.op == jojo::MipsOp::addu);
    CHECK(addu.rs == 9u && addu.rt == 10u && addu.rd == 8u);

    const auto addiu = jojo::decode_mips(0x2528FFF0u);
    CHECK(addiu.op == jojo::MipsOp::addiu);
    CHECK(addiu.immediate == 0xFFF0u);

    const auto jal = jojo::decode_mips(0x0C004000u);
    CHECK(jal.op == jojo::MipsOp::jal);
    CHECK(jal.target == 0x004000u);

    CHECK(jojo::decode_mips(0x04100000u).op == jojo::MipsOp::bltzal);
    CHECK(jojo::decode_mips(0x42000010u).op == jojo::MipsOp::rfe);
    CHECK(jojo::decode_mips(0x48000000u).op == jojo::MipsOp::mfc2);
    CHECK(jojo::decode_mips(0x4A000000u).op == jojo::MipsOp::cop2_command);
    CHECK(jojo::decode_mips(0x70000000u).op == jojo::MipsOp::reserved);
    CHECK(jojo::is_control_transfer(jojo::MipsOp::jal));
    CHECK(!jojo::is_control_transfer(jojo::MipsOp::addu));

    return failures ? 1 : 0;
}
