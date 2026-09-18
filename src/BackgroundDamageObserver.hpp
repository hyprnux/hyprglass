#pragma once

// Client surface commits are the only signal that content below a glassed layer
// or window changed (e.g. a playing video) - the scene-generation cache only
// sees window events. They are also the only signal that a self_sample window's
// own content changed.
namespace BackgroundDamageObserver {
    // Follows layers:enabled and windows:live_resample. Enabling subscribes to
    // view creation and sweeps the surfaces that already exist; disabling drops
    // every listener.
    void setEnabled(bool enabled);

    // setEnabled() from layers:enabled, windows:live_resample and the cached
    // selfSampleConfigured flag. Cheap enough for a per-frame call; it never
    // walks the presets.
    void refreshEnabled();
}
