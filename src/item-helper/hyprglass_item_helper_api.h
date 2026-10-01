#ifndef HYPRGLASS_ITEM_HELPER_API_H
#define HYPRGLASS_ITEM_HELPER_API_H

// Stable ABI between the persistent hyprglass_item_v1 protocol helper (built
// as its own shared library, embedded in and loaded by hyprglass.so, but
// never unmapped across a plugin reload — see AGENTS.md's unload-safety
// note) and the reloadable plugin itself. Plain C so both a plain-C
// translation unit (the helper) and a C++ one (ItemHints.cpp) can share it
// without either depending on the other's language runtime.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct wl_display;
struct wl_resource;

// Bumped on any incompatible layout change to this struct or to
// hyprglass_item_state; the plugin refuses to use a helper whose
// abi_version does not match its own compiled-in constant.
#define HYPRGLASS_ITEM_HELPER_V1_ABI_VERSION 1u

// Preset names are truncated to this many bytes (including the NUL) rather
// than rejected, matching the protocol's "hints are sanitised, not errored" rule.
#define HYPRGLASS_ITEM_HELPER_PRESET_CAP 128

enum hyprglass_item_shape_mode {
    HYPRGLASS_ITEM_SHAPE_NONE           = 0, // region extents, corners from subsurfaces:radius
    HYPRGLASS_ITEM_SHAPE_EXPLICIT       = 1, // x/y/width/height + radii below
    HYPRGLASS_ITEM_SHAPE_INHERIT_WINDOW = 2, // the parent window's own box, rounding and rounding_power
};

// Plain C mirror of one surface's latched (post-commit) item state. Always
// fully populated by hyprglass_item_helper_v1_api::get(); fields outside the
// active shape_mode are zeroed rather than left indeterminate.
struct hyprglass_item_state {
    char                           preset[HYPRGLASS_ITEM_HELPER_PRESET_CAP]; // always NUL-terminated, "" = no preset hint
    enum hyprglass_item_shape_mode shape_mode;
    double                         x, y, width, height; // surface-local logical px, EXPLICIT only
    double                         radii[4];             // top-left, top-right, bottom-right, bottom-left, EXPLICIT only
};

// Fired synchronously from within the get_item request (on_item_created) and
// from whichever of a surface/item destruction happens first (on_item_destroyed),
// each exactly once per item's active lifetime. surface is the wl_surface
// wire resource the item was created for.
typedef void (*hyprglass_item_created_fn)(struct wl_resource* surface, void* userdata);
typedef void (*hyprglass_item_destroyed_fn)(struct wl_resource* surface, void* userdata);
typedef void (*hyprglass_item_foreach_fn)(struct wl_resource* surface, void* userdata);

struct hyprglass_item_helper_v1_api {
    uint32_t abi_version;

    // Creates the hyprglass_item_manager_v1 global if none is currently active.
    // Idempotent. Returns 0 on success, -1 on failure (e.g. wl_global_create failed).
    int (*start)(struct wl_display* display);

    // Removes the active global (existing bound managers get a `finished` event)
    // and marks every object created under it inert. Idempotent (a no-op when
    // already stopped). The removed global itself is kept allocated, not
    // destroyed, until a later start() call — see AGENTS.md's retired-global note.
    void (*stop)(void);

    // Replaces the item-created/item-destroyed callbacks; either may be NULL to
    // clear it. Must be called with NULL callbacks before the caller's own code
    // becomes unreachable (e.g. right before its containing library is unloaded).
    void (*set_callbacks)(void* userdata, hyprglass_item_created_fn on_created, hyprglass_item_destroyed_fn on_destroyed);

    // Copies the surface's active item's pending request state into its cached
    // state. Call from the surface's own precommit (fires on every
    // wl_surface.commit request, including a synchronized subsurface's own,
    // before that subsurface's state is actually applied).
    void (*snapshot)(struct wl_resource* surface);

    // Copies the surface's active item's cached state into its current
    // (latched) state. Call from the surface's commit event (fires when that
    // state is actually applied — deferred for a synchronized subsurface until
    // the ancestor commit that flushes it).
    void (*apply)(struct wl_resource* surface);

    // Fills out_state with the surface's active item's current state. Returns
    // 1 if the surface has an active item (out_state is filled), 0 otherwise
    // (out_state is left untouched).
    int (*get)(struct wl_resource* surface, struct hyprglass_item_state* out_state);

    // Invokes callback once per surface that currently has an active item.
    // Used to recover per-surface listeners after adopting a helper instance
    // left behind by a plugin load that never ran its exit path (crash/forced eject).
    void (*for_each_active_item)(hyprglass_item_foreach_fn callback, void* userdata);
};

// Exported by the helper library; fetch with dlsym (dlopen'd instance) or
// dlsym(RTLD_DEFAULT, ...) (adopting one loaded by a previous plugin load).
extern const struct hyprglass_item_helper_v1_api hyprglass_item_helper_v1_api;

#ifdef __cplusplus
}
#endif

#endif
