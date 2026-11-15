#include "GeometryEvents.hpp"

#include <hyprland/src/desktop/state/ViewState.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/ipc/s2/S2.hpp>

#include <cstdint>
#include <format>
#include <unordered_map>

namespace {
    struct SGeometry {
        int  x = 0, y = 0, width = 0, height = 0;
        bool operator==(const SGeometry&) const = default;
    };

    // Keyed by address only: never dereferenced, and rebuilt from the live window list on every scan.
    std::unordered_map<uintptr_t, SGeometry> g_sent;
    std::unordered_map<uintptr_t, SGeometry> g_seen;
    // No frame may be drawn between the load (or the option turned on) and a move: that move's frame is then the first
    // scan, so it sends every floating window instead of recording them silently.
    bool g_resync = true;

    // The goal geometry, as `hyprctl clients` reports it in `at` and `size`: it changes when a move or resize starts,
    // not when its animation ends.
    SGeometry goalGeometry(const PHLWINDOW& window) {
        const auto position = window->position(Desktop::View::IGeometric::GEOMETRIC_GOAL);
        const auto size     = window->size(Desktop::View::IGeometric::GEOMETRIC_GOAL);
        return {static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(size.x), static_cast<int>(size.y)};
    }
}

void GeometryEvents::scan() {
    g_seen.clear();
    for (const auto& window : Desktop::viewState()->windows()) {
        if (!window || !window->mapped() || !window->isFloating())
            continue;
        const auto address  = reinterpret_cast<uintptr_t>(window.get());
        const auto geometry = goalGeometry(window);
        g_seen.emplace(address, geometry);
        const auto sent = g_sent.find(address);
        if (!g_resync && (sent == g_sent.end() || sent->second == geometry))
            continue;
        IPC::Socket2::sock()->postEvent(
            {.event = "hyprglass", .data = std::format("geometry,{:x},{},{},{},{}", address, geometry.x, geometry.y, geometry.width, geometry.height)});
    }
    g_sent.swap(g_seen);
    g_resync = false;
}

void GeometryEvents::clear() noexcept {
    g_sent.clear();
    g_seen.clear();
    g_resync = true;
}
