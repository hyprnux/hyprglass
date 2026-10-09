#pragma once

// main follows hyprland-git, whose plugin API changes between commits: only the
// commit built against may load it. A stable line cut from main sets this to
// false; CI refuses to release it otherwise, and the forward merge keeps main's.
inline constexpr bool BUILT_FOR_HYPRLAND_GIT = true;
