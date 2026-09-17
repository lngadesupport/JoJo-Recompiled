#include "core/mod_runtime.h"

namespace jojo {

Result<void> validate_mod_session(
    const ResolvedModSet& mods,
    const ModSetHashes& hashes,
    const ModSessionPolicy& policy) {
    for (const auto* mod : mods.load_order) {
        if (mod == nullptr) {
            return Result<void>::failure(
                ErrorCode::invalid_argument,
                "resolved mod set contains a null mod");
        }
    }

    switch (policy.mode) {
        case ModSessionMode::offline:
            return Result<void>::success();

        case ModSessionMode::ranked:
            for (const auto* mod : mods.load_order) {
                if (mod->manifest.gameplay) {
                    return Result<void>::failure(
                        ErrorCode::invalid_argument,
                        "ranked sessions reject gameplay mod: " + mod->manifest.id);
                }
            }
            return Result<void>::success();

        case ModSessionMode::custom:
            if (!policy.required_mod_set_hash.empty() &&
                hashes.mod_set_hash != policy.required_mod_set_hash) {
                return Result<void>::failure(
                    ErrorCode::invalid_argument,
                    "custom session mod-set hash mismatch: expected " +
                        policy.required_mod_set_hash + ", got " + hashes.mod_set_hash);
            }
            return Result<void>::success();
    }

    return Result<void>::failure(ErrorCode::invalid_argument, "unknown mod session mode");
}

}
