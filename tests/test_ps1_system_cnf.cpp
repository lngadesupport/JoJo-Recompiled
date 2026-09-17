#include "core/ps1_system_cnf.h"

#include <iostream>
#include <string_view>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static void expect_ok(std::string_view input, std::string_view expected) {
    const auto parsed = jojo::parse_ps1_system_cnf(input);
    CHECK(parsed);
    if (parsed) CHECK(parsed.value.boot_iso_path == expected);
}

static void expect_fail(std::string_view input) {
    const auto parsed = jojo::parse_ps1_system_cnf(input);
    CHECK(!parsed);
    if (!parsed) CHECK(parsed.error == jojo::ErrorCode::unsupported_format);
}

int main() {
    expect_ok("BOOT = cdrom:\\SLUS_TEST.00;1\r\n", "/SLUS_TEST.00");
    expect_ok("  boot=CDROM:\\DIR\\GAME.EXE;1  \n", "/DIR/GAME.EXE");
    expect_fail("TCB = 4\n");
    expect_fail("BOOT = cdrom:\\..\\GAME.EXE;1\n");
    expect_fail("BOOT = cdrom:\\A.EXE;1\nBOOT = cdrom:\\B.EXE;1\n");
    expect_fail("BOOT = host0:GAME.EXE\n");
    return failures ? 1 : 0;
}
