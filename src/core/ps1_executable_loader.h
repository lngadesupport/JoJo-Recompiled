#pragma once

#include "core/ps1_exe.h"
#include "core/ps1_memory_bus.h"
#include "core/r3000a_reference_executor.h"
#include "core/result.h"

namespace jojo {

[[nodiscard]] Result<R3000aState> load_ps1_executable_into_bus(
    Ps1MemoryBus& bus,
    const Ps1Executable& executable);

} // namespace jojo
