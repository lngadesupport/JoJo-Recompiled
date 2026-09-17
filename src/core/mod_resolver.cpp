#include "core/mod_runtime.h"

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace jojo {

Result<ResolvedModSet> resolve_mod_set(
    const ModCatalog& catalog,
    std::span<const std::string> requested_ids) {
    std::map<std::string, const DiscoveredMod*> by_id;
    for (const auto& mod : catalog) {
        if (!by_id.emplace(mod.manifest.id, &mod).second) {
            return Result<ResolvedModSet>::failure(
                ErrorCode::invalid_argument,
                "duplicate mod id in resolver catalog: " + mod.manifest.id);
        }
    }

    enum class VisitState { visiting, done };
    std::map<std::string, VisitState> visit;
    std::set<std::string> enabled;

    std::function<Result<void>(const std::string&)> include_mod;
    include_mod = [&](const std::string& id) -> Result<void> {
        const auto found = by_id.find(id);
        if (found == by_id.end()) {
            return Result<void>::failure(ErrorCode::invalid_argument, "missing required mod: " + id);
        }

        if (const auto state = visit.find(id); state != visit.end()) {
            if (state->second == VisitState::visiting) {
                return Result<void>::failure(ErrorCode::invalid_argument, "mod dependency cycle detected at: " + id);
            }
            return Result<void>::success();
        }

        const auto& manifest = found->second->manifest;
        if (manifest.api_version.major != kModApiVersion.major ||
            compare_semver(manifest.api_version, kModApiVersion) > 0) {
            return Result<void>::failure(
                ErrorCode::invalid_argument,
                "mod " + id + " requires incompatible API " +
                    to_string(manifest.api_version) + " (host " +
                    to_string(kModApiVersion) + ")");
        }

        visit.emplace(id, VisitState::visiting);
        enabled.insert(id);
        for (const auto& dependency : manifest.dependencies) {
            const auto dependency_it = by_id.find(dependency.id);
            if (dependency_it == by_id.end()) {
                return Result<void>::failure(
                    ErrorCode::invalid_argument,
                    "mod " + id + " requires missing dependency " + dependency.id);
            }
            if (!matches(dependency.requirement, dependency_it->second->manifest.version)) {
                return Result<void>::failure(
                    ErrorCode::invalid_argument,
                    "mod " + id + " dependency " + dependency.id + " has incompatible version " +
                        to_string(dependency_it->second->manifest.version));
            }
            const auto nested = include_mod(dependency.id);
            if (!nested) return nested;
        }
        visit[id] = VisitState::done;
        return Result<void>::success();
    };

    for (const auto& requested : requested_ids) {
        const auto included = include_mod(requested);
        if (!included) return Result<ResolvedModSet>::failure(included.error, included.detail);
    }

    for (const auto& id : enabled) {
        const auto& manifest = by_id.at(id)->manifest;
        for (const auto& conflict : manifest.conflicts) {
            if (enabled.contains(conflict)) {
                return Result<ResolvedModSet>::failure(
                    ErrorCode::invalid_argument,
                    "mod conflict: " + id + " conflicts with " + conflict);
            }
        }
    }

    std::map<std::string, std::size_t> indegree;
    std::map<std::string, std::vector<std::string>> dependents;
    for (const auto& id : enabled) indegree[id] = 0;
    for (const auto& id : enabled) {
        const auto& manifest = by_id.at(id)->manifest;
        for (const auto& dependency : manifest.dependencies) {
            if (!enabled.contains(dependency.id)) continue;
            ++indegree[id];
            dependents[dependency.id].push_back(id);
        }
    }
    for (auto& [_, list] : dependents) {
        std::sort(list.begin(), list.end());
    }

    std::set<std::string> ready;
    for (const auto& [id, degree] : indegree) {
        if (degree == 0) ready.insert(id);
    }

    ResolvedModSet result{};
    while (!ready.empty()) {
        const std::string id = *ready.begin();
        ready.erase(ready.begin());
        result.load_order.push_back(by_id.at(id));

        const auto dep_it = dependents.find(id);
        if (dep_it == dependents.end()) continue;
        for (const auto& dependent : dep_it->second) {
            auto& degree = indegree[dependent];
            if (degree > 0) --degree;
            if (degree == 0) ready.insert(dependent);
        }
    }

    if (result.load_order.size() != enabled.size()) {
        return Result<ResolvedModSet>::failure(ErrorCode::invalid_argument, "mod dependency cycle detected");
    }
    return Result<ResolvedModSet>::success(std::move(result));
}

}
