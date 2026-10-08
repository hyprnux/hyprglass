#include "LoadGuard.hpp"

#include <cstdlib>
#include <format>

namespace LoadGuard {

SVerdict checkCompatibility(HANDLE handle) {
    const SVersionInfo running = HyprlandAPI::getHyprlandVersion(handle);

    SVerdict verdict;
    verdict.identity = HyprlandIdentity::compare(GIT_TAG, GIT_COMMIT_HASH, running.tag, running.hash);
    if (verdict.identity == HyprlandIdentity::EVerdict::Mismatch) {
        verdict.reason  = EPauseReason::HyprlandVersion;
        verdict.message = std::format("built for Hyprland {}, running {}", HyprlandIdentity::describe(GIT_TAG, GIT_COMMIT_HASH),
                                      HyprlandIdentity::describe(running.tag, running.hash));
        return verdict;
    }

    // _get_hash() is Hyprland's, _get_client_hash() the one compiled into this plugin.
    const auto runningLibraries = HyprlandIdentity::dependencySuffix(__hyprland_api_get_hash());
    const auto builtLibraries   = HyprlandIdentity::dependencySuffix(__hyprland_api_get_client_hash());
    if (runningLibraries != builtLibraries) {
        verdict.reason        = EPauseReason::Dependencies;
        const auto difference = HyprlandIdentity::dependencyDifferences(builtLibraries, runningLibraries);
        verdict.message       = std::format("built with {}", difference.empty() ? std::string("other Hyprland libraries") : difference);
    }
    return verdict;
}

bool skipRequested() {
    const char* skip = std::getenv("HYPRGLASS_SKIP_VERSION_CHECK");
    return skip && *skip && std::string_view{skip} != "0";
}

std::string_view reasonCode(EPauseReason reason) noexcept {
    switch (reason) {
        case EPauseReason::HyprlandVersion: return "hyprland_version";
        case EPauseReason::Dependencies: return "dependencies";
        case EPauseReason::None: break;
    }
    return "";
}

std::string_view checkResult(const SVerdict& verdict) noexcept {
    if (verdict.reason != EPauseReason::None)
        return "skipped";
    return verdict.identity == HyprlandIdentity::EVerdict::Match ? "match" : "unknown";
}

} // namespace LoadGuard
