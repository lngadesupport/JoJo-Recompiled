#pragma once

#include "core/mod_runtime.h"

#include <cstddef>
#include <string>
#include <vector>

namespace jojo {

struct NativeModLoadOptions {
    bool allow_native_plugins{};
};

class NativeModSession {
public:
    NativeModSession() = default;
    ~NativeModSession();

    NativeModSession(const NativeModSession&) = delete;
    NativeModSession& operator=(const NativeModSession&) = delete;
    NativeModSession(NativeModSession&& other) noexcept;
    NativeModSession& operator=(NativeModSession&& other) noexcept;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::vector<std::string> loaded_mod_ids() const;

private:
    struct LoadedNativeMod {
        void* library_handle{};
        void (*on_unload)(void){};
        std::string id;
    };

    void unload_all() noexcept;

    std::vector<LoadedNativeMod> loaded_;

    friend Result<NativeModSession> load_native_mods(
        const ResolvedModSet& mods,
        const NativeModLoadOptions& options);
};

[[nodiscard]] Result<NativeModSession> load_native_mods(
    const ResolvedModSet& mods,
    const NativeModLoadOptions& options = {});

}
