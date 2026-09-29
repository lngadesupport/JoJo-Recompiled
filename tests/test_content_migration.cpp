#include "content/content_migration.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::cerr << "CHECK failed at line " << line
              << ": " << expression << "\n";
    std::exit(1);
}

#define CHECK(expr) check((expr), #expr, __LINE__)

} // namespace

int main() {
    using jojo::content::ContentKind;

    CHECK(jojo::content::classify_content_path(
              "/P/CAPLOGO.PAC") ==
          ContentKind::graphics_pack);
    CHECK(jojo::content::classify_content_path(
              "/X/JOJO_M00.XA") ==
          ContentKind::audio_xa);
    CHECK(jojo::content::classify_content_path(
              "/C/PL07.CLT") ==
          ContentKind::palette);
    CHECK(jojo::content::classify_content_path(
              "/C/COMMON.FIN") ==
          ContentKind::color_metadata);
    CHECK(jojo::content::classify_content_path(
              "/M/PL00_HIT.BIN") ==
          ContentKind::hitbox_data);
    CHECK(jojo::content::classify_content_path(
              "/M/PL00.BIN") ==
          ContentKind::character_data);
    CHECK(jojo::content::classify_content_path(
              "/M/MENU.BIN") ==
          ContentKind::ui_data);
    CHECK(jojo::content::classify_content_path(
              "/M/OP00.BIN") ==
          ContentKind::script_data);

    CHECK(jojo::content::is_runtime_only_source(
        "/SLUS_010.60"));
    CHECK(jojo::content::is_runtime_only_source(
        "/SYSTEM.CNF"));
    CHECK(jojo::content::is_runtime_only_source(
        "/ZNULL.DAT"));
    CHECK(!jojo::content::is_runtime_only_source(
        "/P/COMMON.PAC"));

    std::cout << "content migration classification tests passed\n";
    return 0;
}
