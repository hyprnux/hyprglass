#include "LoadGuard.hpp"
#include "BuildChannel.hpp"
#include "Globals.hpp"

#include <hyprland/src/plugins/PluginSystem.hpp>

#include <cstdlib>
#include <dlfcn.h>
#include <format>

namespace LoadGuard {

SVerdict checkCompatibility(HANDLE handle) {
    const SVersionInfo running = HyprlandAPI::getHyprlandVersion(handle);

    SVerdict verdict;
    verdict.identity = HyprlandIdentity::compare(GIT_TAG, GIT_COMMIT_HASH, running.tag, running.hash, BUILT_FOR_HYPRLAND_GIT);
    if (verdict.identity == HyprlandIdentity::EVerdict::Mismatch) {
        verdict.reason  = EPauseReason::HyprlandVersion;
        verdict.message = std::format("built for Hyprland {}, running {}", HyprlandIdentity::describe(GIT_TAG, GIT_COMMIT_HASH, BUILT_FOR_HYPRLAND_GIT),
                                      HyprlandIdentity::describe(running.tag, running.hash, BUILT_FOR_HYPRLAND_GIT));
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

std::optional<std::string> otherActiveCopyPath() {
    for (const auto* plugin : g_pPluginSystem->getAllPlugins()) {
        // The copy being initialised has no name yet; a same-handle entry is
        // this file loaded through another path (symlink, hardlink).
        if (plugin->m_name != PLUGIN_NAME)
            continue;
        // A paused copy must not keep a working one from loading. Copies older
        // than this check have no such symbol and are always active.
        using FInstanceActive     = bool (*)();
        const auto instanceActive = reinterpret_cast<FInstanceActive>(dlsym(plugin->m_handle, "hyprglass_instance_active"));
        if (!instanceActive || instanceActive())
            return plugin->m_path;
    }
    return std::nullopt;
}

bool skipRequested() {
    const char* skip = std::getenv("HYPRGLASS_SKIP_VERSION_CHECK");
    return skip && *skip && std::string_view{skip} != "0";
}

std::string_view reasonCode(EPauseReason reason) noexcept {
    switch (reason) {
        case EPauseReason::HyprlandVersion: return "hyprland_version";
        case EPauseReason::Dependencies: return "dependencies";
        case EPauseReason::Duplicate: return "duplicate";
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
