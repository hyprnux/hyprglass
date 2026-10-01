#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

class CWLSurfaceResource;

// Client hints for subsurface glass items (protocols/hyprglass-item-v1.xml).
enum class eItemShapeMode : uint8_t {
    NONE,           // region extents, corners from subsurfaces:radius
    EXPLICIT,       // x/y/width/height + radii below
    INHERIT_WINDOW, // the parent window's own box, rounding and rounding_power
};

struct SItemHints {
    std::string           preset; // empty = no preset hint
    eItemShapeMode        shapeMode = eItemShapeMode::NONE;
    double                x = 0.0, y = 0.0, width = 0.0, height = 0.0; // surface-local logical px, EXPLICIT only
    std::array<double, 4> radii{};                                     // top-left, top-right, bottom-right, bottom-left (logical px), EXPLICIT only
};

namespace ItemHints {
    // PLUGIN_INIT: loads or adopts the persistent protocol helper, advertises the global, hooks commit latching.
    bool init();
    // PLUGIN_EXIT: withdraws the global (existing objects turn inert) and drops every callback into this plugin.
    void exit();
    // Hints latched by the surface's last applied commit; nullopt when the surface has no active item.
    std::optional<SItemHints> forSurface(CWLSurfaceResource* surface);
    // True once the helper is loaded and the wayland global is advertised (between init() succeeding and exit()).
    bool active();
}
