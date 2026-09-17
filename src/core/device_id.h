#pragma once
#include <string>
#include <string_view>

namespace jojo {
[[nodiscard]] std::string make_xinput_device_id(unsigned index);
[[nodiscard]] std::string make_hid_device_id(std::string_view interface_path);
}
