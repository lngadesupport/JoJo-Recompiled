#include "core/native_mod_loader.h"

#include "mod_api/jojo_mod_api.h"

#include <filesystem>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace jojo {
namespace {

void close_native_library(void* handle) noexcept {
    if (handle == nullptr) return;
#if defined(_WIN32)
    (void)FreeLibrary(static_cast<HMODULE>(handle));
#else
    (void)dlclose(handle);
#endif
}

Result<void*> open_native_library(const std::filesystem::path& path) {
#if defined(_WIN32)
    const auto module = LoadLibraryW(path.c_str());
    if (module == nullptr) {
        return Result<void*>::failure(
            ErrorCode::backend_unavailable,
            "failed to load native mod library " + path.string() +
                " (Win32 error " + std::to_string(GetLastError()) + ")");
    }
    return Result<void*>::success(static_cast<void*>(module));
#else
    dlerror();
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char* error = dlerror();
        return Result<void*>::failure(
            ErrorCode::backend_unavailable,
            "failed to load native mod library " + path.string() + ": " +
                (error == nullptr ? "unknown dynamic-loader error" : error));
    }
    return Result<void*>::success(handle);
#endif
}

Result<JojoGetNativeModV1Fn> find_native_mod_entry(void* handle, std::string_view mod_id) {
#if defined(_WIN32)
    const auto symbol = GetProcAddress(static_cast<HMODULE>(handle), "jojo_get_native_mod_v1");
    if (symbol == nullptr) {
        return Result<JojoGetNativeModV1Fn>::failure(
            ErrorCode::backend_unavailable,
            "native mod " + std::string(mod_id) +
                " does not export jojo_get_native_mod_v1 (Win32 error " +
                std::to_string(GetLastError()) + ")");
    }
    return Result<JojoGetNativeModV1Fn>::success(
        reinterpret_cast<JojoGetNativeModV1Fn>(symbol));
#else
    dlerror();
    void* symbol = dlsym(handle, "jojo_get_native_mod_v1");
    const char* error = dlerror();
    if (error != nullptr || symbol == nullptr) {
        return Result<JojoGetNativeModV1Fn>::failure(
            ErrorCode::backend_unavailable,
            "native mod " + std::string(mod_id) +
                " does not export jojo_get_native_mod_v1: " +
                (error == nullptr ? "symbol not found" : error));
    }
    return Result<JojoGetNativeModV1Fn>::success(
        reinterpret_cast<JojoGetNativeModV1Fn>(symbol));
#endif
}

bool safe_relative_entry(const std::filesystem::path& entry) {
    if (entry.empty() || entry.is_absolute() || entry.has_root_name() || entry.has_root_directory()) {
        return false;
    }
    const auto normalized = entry.lexically_normal();
    if (normalized.empty() || normalized == ".") return false;
    for (const auto& component : normalized) {
        if (component == "..") return false;
    }
    return true;
}

Result<std::filesystem::path> validate_native_entry(const DiscoveredMod& mod) {
    if (!safe_relative_entry(mod.manifest.entry)) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::invalid_argument,
            "unsafe native mod entry path for " + mod.manifest.id + ": " +
                mod.manifest.entry.string());
    }

    std::error_code ec;
    const auto root_status = std::filesystem::symlink_status(mod.root, ec);
    if (ec) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "failed to inspect native mod root for " + mod.manifest.id + ": " + ec.message());
    }
    if (std::filesystem::is_symlink(root_status)) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::invalid_argument,
            "symlinked native mod root is not allowed: " + mod.manifest.id);
    }
    if (!std::filesystem::is_directory(root_status)) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::file_not_found,
            "native mod root is not a directory: " + mod.root.string());
    }

    auto current = mod.root;
    const auto normalized = mod.manifest.entry.lexically_normal();
    std::size_t component_index = 0;
    const auto component_count = static_cast<std::size_t>(
        std::distance(normalized.begin(), normalized.end()));
    for (const auto& component : normalized) {
        ++component_index;
        current /= component;
        const auto status = std::filesystem::symlink_status(current, ec);
        if (ec) {
            return Result<std::filesystem::path>::failure(
                ErrorCode::io_error,
                "failed to inspect native mod entry for " + mod.manifest.id + ": " + ec.message());
        }
        if (std::filesystem::is_symlink(status)) {
            return Result<std::filesystem::path>::failure(
                ErrorCode::invalid_argument,
                "symlinked native mod entry is not allowed: " + mod.manifest.id);
        }
        if (!std::filesystem::exists(status)) {
            return Result<std::filesystem::path>::failure(
                ErrorCode::file_not_found,
                "native mod entry does not exist: " + current.string());
        }
        const bool is_last = component_index == component_count;
        if (!is_last && !std::filesystem::is_directory(status)) {
            return Result<std::filesystem::path>::failure(
                ErrorCode::invalid_argument,
                "native mod entry parent is not a directory: " + current.string());
        }
        if (is_last && !std::filesystem::is_regular_file(status)) {
            return Result<std::filesystem::path>::failure(
                ErrorCode::invalid_argument,
                "native mod entry is not a regular file: " + current.string());
        }
    }

    return Result<std::filesystem::path>::success(std::move(current));
}

class PendingLibrary {
public:
    explicit PendingLibrary(void* handle) noexcept : handle_(handle) {}
    ~PendingLibrary() { close_native_library(handle_); }

    PendingLibrary(const PendingLibrary&) = delete;
    PendingLibrary& operator=(const PendingLibrary&) = delete;

    [[nodiscard]] void* get() const noexcept { return handle_; }
    [[nodiscard]] void* release() noexcept {
        void* handle = handle_;
        handle_ = nullptr;
        return handle;
    }

private:
    void* handle_{};
};

}

NativeModSession::~NativeModSession() {
    unload_all();
}

NativeModSession::NativeModSession(NativeModSession&& other) noexcept {
    loaded_.swap(other.loaded_);
}

NativeModSession& NativeModSession::operator=(NativeModSession&& other) noexcept {
    if (this == &other) return *this;
    unload_all();
    loaded_.swap(other.loaded_);
    return *this;
}

bool NativeModSession::empty() const noexcept {
    return loaded_.empty();
}

std::size_t NativeModSession::size() const noexcept {
    return loaded_.size();
}

std::vector<std::string> NativeModSession::loaded_mod_ids() const {
    std::vector<std::string> ids;
    ids.reserve(loaded_.size());
    for (const auto& loaded : loaded_) ids.push_back(loaded.id);
    return ids;
}

void NativeModSession::unload_all() noexcept {
    for (auto it = loaded_.rbegin(); it != loaded_.rend(); ++it) {
        if (it->on_unload != nullptr) {
            try {
                it->on_unload();
            } catch (...) {
            }
        }
        close_native_library(it->library_handle);
        it->library_handle = nullptr;
    }
    loaded_.clear();
}

Result<NativeModSession> load_native_mods(
    const ResolvedModSet& mods,
    const NativeModLoadOptions& options) {
    NativeModSession session;

    for (const auto* mod : mods.load_order) {
        if (mod == nullptr) {
            return Result<NativeModSession>::failure(
                ErrorCode::invalid_argument,
                "resolved mod set contains a null mod");
        }
        if (mod->manifest.kind != ModKind::native) continue;
        if (!options.allow_native_plugins) {
            return Result<NativeModSession>::failure(
                ErrorCode::backend_unavailable,
                "native plugins are disabled; explicit opt-in is required for " +
                    mod->manifest.id);
        }

        const auto entry = validate_native_entry(*mod);
        if (!entry) {
            return Result<NativeModSession>::failure(entry.error, entry.detail);
        }

        const auto opened = open_native_library(entry.value);
        if (!opened) {
            return Result<NativeModSession>::failure(opened.error, opened.detail);
        }
        PendingLibrary pending(opened.value);

        const auto getter = find_native_mod_entry(pending.get(), mod->manifest.id);
        if (!getter) {
            return Result<NativeModSession>::failure(getter.error, getter.detail);
        }

        const JojoNativeModV1* descriptor = nullptr;
        try {
            descriptor = getter.value();
        } catch (...) {
            return Result<NativeModSession>::failure(
                ErrorCode::backend_unavailable,
                "native mod descriptor threw an exception: " + mod->manifest.id);
        }
        if (descriptor == nullptr) {
            return Result<NativeModSession>::failure(
                ErrorCode::backend_unavailable,
                "native mod returned a null descriptor: " + mod->manifest.id);
        }
        if (descriptor->struct_size < static_cast<uint32_t>(sizeof(JojoNativeModV1))) {
            return Result<NativeModSession>::failure(
                ErrorCode::backend_unavailable,
                "native mod descriptor is too small: " + mod->manifest.id);
        }
        if (descriptor->abi_version != JOJO_NATIVE_MOD_ABI_V1) {
            return Result<NativeModSession>::failure(
                ErrorCode::backend_unavailable,
                "native mod ABI mismatch for " + mod->manifest.id + ": expected " +
                    std::to_string(JOJO_NATIVE_MOD_ABI_V1) + ", got " +
                    std::to_string(descriptor->abi_version));
        }
        if (descriptor->mod_id == nullptr ||
            std::string_view(descriptor->mod_id) != mod->manifest.id) {
            return Result<NativeModSession>::failure(
                ErrorCode::backend_unavailable,
                "native mod id does not match manifest: " + mod->manifest.id);
        }
        if (descriptor->on_load == nullptr || descriptor->on_unload == nullptr) {
            return Result<NativeModSession>::failure(
                ErrorCode::backend_unavailable,
                "native mod callbacks are incomplete: " + mod->manifest.id);
        }

        int load_result = -1;
        try {
            load_result = descriptor->on_load();
        } catch (...) {
            return Result<NativeModSession>::failure(
                ErrorCode::backend_unavailable,
                "native mod on_load threw an exception: " + mod->manifest.id);
        }
        if (load_result != 0) {
            return Result<NativeModSession>::failure(
                ErrorCode::backend_unavailable,
                "native mod on_load failed for " + mod->manifest.id +
                    " with code " + std::to_string(load_result));
        }

        session.loaded_.push_back({pending.release(), descriptor->on_unload, mod->manifest.id});
    }

    return Result<NativeModSession>::success(std::move(session));
}

}
