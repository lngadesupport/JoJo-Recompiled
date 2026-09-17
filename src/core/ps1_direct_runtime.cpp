#include "core/runtime.h"

#include "core/ps1_boot_report_io.h"
#include "core/ps1_boot_runtime.h"

namespace jojo {

Result<Ps1BootReport> bootstrap_runtime_checkpoint_from_disc(
    const std::filesystem::path& source,
    const Ps1DiscOpenOptions& open_options,
    const Ps1BootOptions& options) {
    auto session = Ps1DiscSession::open(source, open_options);
    if (!session) {
        return Result<Ps1BootReport>::failure(session.error, session.detail);
    }

    auto runtime = Ps1BootRuntime::create(session.value.boot_executable());
    if (!runtime) {
        return Result<Ps1BootReport>::failure(runtime.error, runtime.detail);
    }

    return Result<Ps1BootReport>::success(runtime.value.run(options));
}

Result<Ps1BootReport> bootstrap_runtime_checkpoint_from_disc_to_file(
    const std::filesystem::path& source,
    const Ps1DiscOpenOptions& open_options,
    const std::filesystem::path& report_path,
    const Ps1BootOptions& options) {
    auto report = bootstrap_runtime_checkpoint_from_disc(source, open_options, options);
    if (!report) return report;

    auto saved = save_ps1_boot_report_atomic(report_path, report.value);
    if (!saved) {
        return Result<Ps1BootReport>::failure(saved.error, saved.detail);
    }
    return report;
}

} // namespace jojo
