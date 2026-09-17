#include "core/ps1_executable_loader.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace jojo {

Result<R3000aState> load_ps1_executable_into_bus(
    Ps1MemoryBus& bus,
    const Ps1Executable& executable) {
    constexpr std::size_t header_size = 0x800u;
    const auto text_size = static_cast<std::size_t>(executable.metadata.text_size);

    if (executable.file_bytes.size() < header_size ||
        text_size > executable.file_bytes.size() - header_size) {
        return Result<R3000aState>::failure(
            ErrorCode::invalid_installation,
            "validated PS-X EXE payload is truncated");
    }

    if (executable.metadata.text_size != 0u &&
        executable.metadata.text_load_address >
            std::numeric_limits<std::uint32_t>::max() -
                (executable.metadata.text_size - 1u)) {
        return Result<R3000aState>::failure(
            ErrorCode::invalid_installation,
            "PS-X EXE guest payload range wraps 32-bit address space");
    }

    const std::span<const std::uint8_t> payload{
        executable.file_bytes.data() + header_size, text_size};
    auto copied = bus.load_main_ram(executable.metadata.text_load_address, payload);
    if (!copied) {
        return Result<R3000aState>::failure(copied.error, copied.detail);
    }

    return Result<R3000aState>::success(
        initialize_r3000a_for_psx_exe(executable.metadata));
}

} // namespace jojo
