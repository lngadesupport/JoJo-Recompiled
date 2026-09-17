#include "core/conversion.h"
#include <iostream>
#include <string>

int main() {
    const auto known = jojo::identify_observed_disc_revision(
        "bin", 666806112ull, "b8b5dbf79cdb9fcf");
    if (!known || known.value.revision_id != "jojo-usa-observed-b8b5dbf79cdb9fcf") {
        std::cerr << "known USA disc fingerprint was not recognized\n";
        return 1;
    }

    const auto wrong_size = jojo::identify_observed_disc_revision(
        "bin", 666806113ull, "b8b5dbf79cdb9fcf");
    if (wrong_size || wrong_size.error != jojo::ErrorCode::unknown_revision) {
        std::cerr << "size mismatch must remain unknown\n";
        return 1;
    }

    const auto wrong_hash = jojo::identify_observed_disc_revision(
        "bin", 666806112ull, "0000000000000000");
    if (wrong_hash || wrong_hash.error != jojo::ErrorCode::unknown_revision) {
        std::cerr << "hash mismatch must remain unknown\n";
        return 1;
    }

    const auto wrong_format = jojo::identify_observed_disc_revision(
        "iso", 666806112ull, "b8b5dbf79cdb9fcf");
    if (wrong_format || wrong_format.error != jojo::ErrorCode::unknown_revision) {
        std::cerr << "format mismatch must remain unknown\n";
        return 1;
    }

    std::cout << "observed disc revision contract passed\n";
    return 0;
}
