#include "content/content_migration.h"

#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr
            << "usage: jojo_content_importer <game.cue|game.bin|game.iso> "
               "<output-directory>\n";
        return 2;
    }

    const std::filesystem::path source{argv[1]};
    const std::filesystem::path output{argv[2]};
    const auto imported =
        jojo::content::import_game_content(source, output);
    if (!imported) {
        std::cerr << "content import failed: "
                  << imported.detail << "\n";
        return 1;
    }

    std::cout
        << "JOJO content-only import complete\n"
        << "  source format: "
        << imported.value.source_format << "\n"
        << "  files imported: "
        << imported.value.files_imported << "\n"
        << "  files excluded (PS1 runtime/system): "
        << imported.value.files_excluded << "\n"
        << "  bytes imported: "
        << imported.value.bytes_imported << "\n"
        << "  manifest: "
        << (output / "manifest.json").string() << "\n";
    return 0;
}
