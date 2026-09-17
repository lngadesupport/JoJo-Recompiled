#pragma once

#include "core/iso9660.h"
#include "core/result.h"

#include <string>
#include <string_view>

namespace jojo {

struct Ps1SystemCnf {
    std::string boot_iso_path;
};

[[nodiscard]] Result<Ps1SystemCnf> parse_ps1_system_cnf(std::string_view text);
[[nodiscard]] Result<Ps1SystemCnf> read_ps1_system_cnf(const Iso9660Image& image);

}
