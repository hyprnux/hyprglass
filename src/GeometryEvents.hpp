#pragma once

// Socket2 line `hyprglass>>geometry,ADDRESS,X,Y,W,H` each time a floating window's position or size changes:
// Hyprland emits nothing when one is dragged, resized or moved by a dispatcher.
namespace GeometryEvents {
    // Compares every mapped floating window with what was last sent and posts a line for each that changed. A window
    // seen for the first time (opened or made floating) is recorded silently: socket2 already reports both. The first
    // scan after the load or after clear() posts every floating window.
    void scan();
    // Forgets every window while the option is off: the scan after it is turned back on posts them all.
    void clear() noexcept;
}
