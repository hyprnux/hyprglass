#pragma once

#include <array>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <format>
#include <utility>

// Whether the Hyprland headers hyprglass was built against describe the
// running Hyprland. No Hyprland include, so it builds and tests standalone.
namespace HyprlandIdentity {

struct SReleaseLine {
    int major = 0;
    int minor = 0;

    bool operator==(const SReleaseLine&) const = default;
};

// "v0.56.2" -> 0.56. git describe output ("v0.56.0-175-g4bb6844"), "" and
// "unknown" are not releases.
[[nodiscard]] constexpr std::optional<SReleaseLine> releaseLine(std::string_view tag) noexcept {
    if (!tag.starts_with('v'))
        return std::nullopt;
    tag.remove_prefix(1);
    int parts[3] = {};
    for (int index = 0; index < 3; ++index) {
        const auto [end, error] = std::from_chars(tag.data(), tag.data() + tag.size(), parts[index]);
        if (error != std::errc{} || end == tag.data())
            return std::nullopt;
        tag.remove_prefix(static_cast<size_t>(end - tag.data()));
        if (index < 2) {
            if (!tag.starts_with('.'))
                return std::nullopt;
            tag.remove_prefix(1);
        }
    }
    if (!tag.empty())
        return std::nullopt;
    return SReleaseLine{parts[0], parts[1]};
}

[[nodiscard]] constexpr bool isCommitHash(std::string_view hash) noexcept {
    if (hash.size() != 40)
        return false;
    for (const char character : hash)
        if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f')))
            return false;
    return true;
}

enum class EVerdict {
    Match,
    Mismatch,
    Unknown, // no usable tag or commit on one side
};

// Same commit, else same release line when both are releases, else
// different commits. Nix builds of hyprland-git report "v" + VERSION as
// their tag, so they look like releases here: commitOnly skips the release
// line for a build whose Hyprland API changes between commits, and a running
// Hyprland without a commit is then never the one built against.
[[nodiscard]] constexpr EVerdict compare(std::string_view builtTag, std::string_view builtHash, std::string_view runningTag, std::string_view runningHash,
                                         bool commitOnly = false) noexcept {
    if (isCommitHash(builtHash) && builtHash == runningHash)
        return EVerdict::Match;
    const auto builtLine   = releaseLine(builtTag);
    const auto runningLine = releaseLine(runningTag);
    if (!commitOnly && builtLine && runningLine)
        return *builtLine == *runningLine ? EVerdict::Match : EVerdict::Mismatch;
    if (isCommitHash(builtHash) && (commitOnly || isCommitHash(runningHash)))
        return EVerdict::Mismatch;
    return EVerdict::Unknown;
}

// "0.56" for a release, else the commit's first 12 characters.
[[nodiscard]] inline std::string describe(std::string_view tag, std::string_view hash, bool commitOnly = false) {
    if (commitOnly && isCommitHash(hash))
        return std::string(hash.substr(0, 12));
    if (const auto line = releaseLine(tag))
        return std::to_string(line->major) + "." + std::to_string(line->minor);
    return hash.empty() ? std::string("unknown") : std::string(hash.substr(0, 12));
}

// "<commit>_aq_0.15_hu_0.14_hg_0.5_hc_0.1_hlg_0.6" -> "_aq_0.15_…": the
// library versions, without the commit.
[[nodiscard]] constexpr std::string_view dependencySuffix(std::string_view apiHash) noexcept {
    const auto at = apiHash.find("_aq_");
    return at == std::string_view::npos ? apiHash : apiHash.substr(at);
}

// Keys carry both underscores, so "_hg_" never matches inside "_hlg_".
[[nodiscard]] constexpr std::string_view dependencyVersion(std::string_view suffix, std::string_view key) noexcept {
    const auto at = suffix.find(key);
    if (at == std::string_view::npos)
        return {};
    const auto value = suffix.substr(at + key.size());
    return value.substr(0, value.find('_'));
}

// "aquamarine 0.15 -> 0.16, hyprlang 0.6 -> 0.7"
[[nodiscard]] inline std::string dependencyDifferences(std::string_view builtSuffix, std::string_view runningSuffix) {
    constexpr std::array<std::pair<std::string_view, std::string_view>, 5> LIBRARIES = {{
        {"_aq_", "aquamarine"}, {"_hu_", "hyprutils"}, {"_hg_", "hyprgraphics"}, {"_hc_", "hyprcursor"}, {"_hlg_", "hyprlang"},
    }};
    std::string out;
    for (const auto& [key, name] : LIBRARIES) {
        const auto built   = dependencyVersion(builtSuffix, key);
        const auto running = dependencyVersion(runningSuffix, key);
        if (built == running)
            continue;
        if (!out.empty())
            out += ", ";
        out += std::format("{} {} -> {}", name, built, running);
    }
    return out;
}

} // namespace HyprlandIdentity
