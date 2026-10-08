#pragma once

#include "HyprlandIdentity.hpp"

#include <hyprland/src/plugins/PluginAPI.hpp>

#include <string>
#include <string_view>

// Decides, before PLUGIN_INIT touches anything else, whether this copy may run.
// A paused copy registers nothing: no hook, decoration, listener, config value,
// hyprctl command, protocol global or GL object.
namespace LoadGuard {

enum class EPauseReason {
    None,
    HyprlandVersion, // built for another Hyprland release line or commit
    Dependencies,    // same Hyprland, other aquamarine/hyprutils/... versions
};

struct SVerdict {
    EPauseReason               reason   = EPauseReason::None;
    HyprlandIdentity::EVerdict identity = HyprlandIdentity::EVerdict::Unknown;
    std::string                message; // "built for Hyprland 0.56, running 0.57"
};

// Calls only getHyprlandVersion() and __hyprland_api_get_hash(): both
// extern "C" and unchanged across Hyprland versions, so they are safe to call
// from a build made for another Hyprland.
[[nodiscard]] SVerdict checkCompatibility(HANDLE handle);

// HYPRGLASS_SKIP_VERSION_CHECK, read from Hyprland's own environment.
[[nodiscard]] bool skipRequested();

[[nodiscard]] std::string_view reasonCode(EPauseReason reason) noexcept;

// `versionCheck` in `hyprctl hyprglass status`, for a copy that went on to load.
[[nodiscard]] std::string_view checkResult(const SVerdict& verdict) noexcept;

} // namespace LoadGuard
