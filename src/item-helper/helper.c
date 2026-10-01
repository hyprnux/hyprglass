// Persistent hyprglass_item_v1 protocol helper.
//
// Built as its own shared library and never dlclose'd (see ItemHelperBlob.S
// and ItemHints.cpp), so every wl_global/wl_resource/wl_interface this file
// owns stays valid memory across a hyprglass.so unload+reload. That is what
// lets an object become permanently inert instead of being destroyed out
// from under a client that is still mid-request — destroying resources
// before dlclose would disconnect a client that raced the removal (see
// feasibility study §1.2).
//
// Depends only on libwayland-server: no Hyprland headers, no C++.

#include "hyprglass-item-v1-server-protocol.h"
#include "hyprglass_item_helper_api.h"

#include <wayland-server.h>

#include <stdlib.h>
#include <string.h>

struct manager_object {
    struct wl_resource* resource;
    int                 inert;
    struct wl_list      link; // in g_managers
};

struct item_object {
    struct wl_resource* resource;
    struct wl_resource* surface; // borrowed; NULL once the surface has died
    struct wl_listener  surface_destroy_listener;
    int                 listening; // surface_destroy_listener is linked into the surface's destroy signal
    int                 inert;

    struct hyprglass_item_state pending;
    struct hyprglass_item_state cached;
    struct hyprglass_item_state current;

    struct wl_list link; // in g_items
};

struct retired_global {
    struct wl_global* global;
    struct wl_list    link; // in g_retired
};

static struct wl_list g_managers;
static struct wl_list g_items;
static struct wl_list g_retired;
static int            g_lists_initialized;

static struct wl_global* g_active_global;

static void*                        g_callback_userdata;
static hyprglass_item_created_fn   g_on_created;
static hyprglass_item_destroyed_fn g_on_destroyed;

static void ensure_lists_initialized(void) {
    if (g_lists_initialized)
        return;
    g_lists_initialized = 1;
    wl_list_init(&g_managers);
    wl_list_init(&g_items);
    wl_list_init(&g_retired);
}

static struct item_object* find_active_item_for_surface(struct wl_resource* surface) {
    struct item_object* item;
    wl_list_for_each(item, &g_items, link) {
        if (!item->inert && item->surface == surface)
            return item;
    }
    return NULL;
}

// ── hyprglass_item_v1 (item object) request handlers ────────────────────────

static void item_handle_destroy(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    wl_resource_destroy(resource);
}

static void item_handle_set_preset(struct wl_client* client, struct wl_resource* resource, const char* name) {
    (void)client;
    struct item_object* item = wl_resource_get_user_data(resource);
    if (item->inert)
        return;

    if (!name || !name[0]) {
        item->pending.preset[0] = '\0';
        return;
    }

    size_t length = strlen(name);
    if (length >= HYPRGLASS_ITEM_HELPER_PRESET_CAP)
        length = HYPRGLASS_ITEM_HELPER_PRESET_CAP - 1;
    memcpy(item->pending.preset, name, length);
    item->pending.preset[length] = '\0';
}

static void item_handle_unset_preset(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    struct item_object* item = wl_resource_get_user_data(resource);
    if (item->inert)
        return;
    item->pending.preset[0] = '\0';
}

static void clear_pending_shape(struct item_object* item) {
    item->pending.shape_mode = HYPRGLASS_ITEM_SHAPE_NONE;
    item->pending.x = item->pending.y = item->pending.width = item->pending.height = 0.0;
    item->pending.radii[0] = item->pending.radii[1] = item->pending.radii[2] = item->pending.radii[3] = 0.0;
}

static void item_handle_set_shape(struct wl_client* client, struct wl_resource* resource, wl_fixed_t x, wl_fixed_t y, wl_fixed_t width, wl_fixed_t height,
                                   wl_fixed_t radius_top_left, wl_fixed_t radius_top_right, wl_fixed_t radius_bottom_right, wl_fixed_t radius_bottom_left) {
    (void)client;
    struct item_object* item = wl_resource_get_user_data(resource);
    if (item->inert)
        return;

    // A zero/negative extent is a transient layout state, not an error: fall
    // back to the same pending state as unset_shape.
    double w = wl_fixed_to_double(width);
    double h = wl_fixed_to_double(height);
    if (w <= 0.0 || h <= 0.0) {
        clear_pending_shape(item);
        return;
    }

    item->pending.shape_mode = HYPRGLASS_ITEM_SHAPE_EXPLICIT;
    item->pending.x          = wl_fixed_to_double(x);
    item->pending.y          = wl_fixed_to_double(y);
    item->pending.width      = w;
    item->pending.height     = h;

    double radii[4] = {wl_fixed_to_double(radius_top_left), wl_fixed_to_double(radius_top_right), wl_fixed_to_double(radius_bottom_right),
                        wl_fixed_to_double(radius_bottom_left)};
    for (int i = 0; i < 4; ++i)
        item->pending.radii[i] = radii[i] < 0.0 ? 0.0 : radii[i];
}

static void item_handle_set_inherit_shape(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    struct item_object* item = wl_resource_get_user_data(resource);
    if (item->inert)
        return;
    // set_shape/set_inherit_shape are last-wins: both just overwrite shape_mode.
    item->pending.shape_mode = HYPRGLASS_ITEM_SHAPE_INHERIT_WINDOW;
}

static void item_handle_unset_shape(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    struct item_object* item = wl_resource_get_user_data(resource);
    if (item->inert)
        return;
    clear_pending_shape(item);
}

static const struct hyprglass_item_v1_interface item_impl = {
    .destroy            = item_handle_destroy,
    .set_preset         = item_handle_set_preset,
    .unset_preset       = item_handle_unset_preset,
    .set_shape          = item_handle_set_shape,
    .set_inherit_shape  = item_handle_set_inherit_shape,
    .unset_shape        = item_handle_unset_shape,
};

static void handle_surface_destroy(struct wl_listener* listener, void* data) {
    (void)data;
    struct item_object* item;
    item = wl_container_of(listener, item, surface_destroy_listener);

    // The item object itself stays around, inert, per hyprglass_item_v1's own
    // description — only the surface association is dropped.
    struct wl_resource* surface = item->surface;
    item->listening             = 0; // libwayland unlinks listeners as it emits a destroy signal
    item->inert                 = 1;
    item->surface               = NULL;

    if (g_on_destroyed)
        g_on_destroyed(surface, g_callback_userdata);
}

static void item_resource_destroy(struct wl_resource* resource) {
    struct item_object* item = wl_resource_get_user_data(resource);

    // Tracked apart from inert: stop() makes items inert while their surfaces
    // live on, and a listener left linked past free() would make the surface's
    // later destroy run handle_surface_destroy on freed memory.
    if (item->listening) {
        wl_list_remove(&item->surface_destroy_listener.link);
        item->listening = 0;
    }

    // Only notify if this item was still the surface's active one — a surface
    // destroy already fired this (and cleared item->surface) for an item that
    // outlived its surface.
    if (!item->inert && item->surface && g_on_destroyed)
        g_on_destroyed(item->surface, g_callback_userdata);

    wl_list_remove(&item->link);
    free(item);
}

// ── hyprglass_item_manager_v1 (manager object) request handlers ─────────────

static void manager_handle_destroy(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    wl_resource_destroy(resource);
}

static void manager_handle_get_item(struct wl_client* client, struct wl_resource* manager_resource, uint32_t id, struct wl_resource* surface_resource) {
    struct manager_object* manager = wl_resource_get_user_data(manager_resource);

    struct wl_resource* item_resource = wl_resource_create(client, &hyprglass_item_v1_interface, wl_resource_get_version(manager_resource), id);
    if (!item_resource) {
        wl_client_post_no_memory(client);
        return;
    }

    // The item_exists check only applies to a manager that is still the
    // active one: an inert (old-generation) manager's get_item must still
    // create the new_id resource — just inert — or the client would later
    // hit "invalid object" for an id it thinks it owns.
    if (!manager->inert) {
        struct item_object* existing = find_active_item_for_surface(surface_resource);
        if (existing) {
            wl_resource_post_error(manager_resource, HYPRGLASS_ITEM_MANAGER_V1_ERROR_ITEM_EXISTS, "surface already has an active hyprglass_item_v1 object");
            wl_resource_destroy(item_resource);
            return;
        }
    }

    struct item_object* item = calloc(1, sizeof(*item));
    if (!item) {
        wl_resource_destroy(item_resource);
        wl_client_post_no_memory(client);
        return;
    }

    item->resource = item_resource;
    item->surface  = surface_resource;
    item->inert    = manager->inert;

    wl_list_insert(&g_items, &item->link);
    wl_resource_set_implementation(item_resource, &item_impl, item, item_resource_destroy);

    if (!item->inert) {
        item->surface_destroy_listener.notify = handle_surface_destroy;
        wl_resource_add_destroy_listener(surface_resource, &item->surface_destroy_listener);
        item->listening = 1;

        if (g_on_created)
            g_on_created(surface_resource, g_callback_userdata);
    }
}

static const struct hyprglass_item_manager_v1_interface manager_impl = {
    .destroy  = manager_handle_destroy,
    .get_item = manager_handle_get_item,
};

static void manager_resource_destroy(struct wl_resource* resource) {
    struct manager_object* manager = wl_resource_get_user_data(resource);
    wl_list_remove(&manager->link);
    free(manager);
}

static void bind_manager(struct wl_client* client, void* data, uint32_t version, uint32_t id) {
    // data is the wl_global* this bind landed on (see start()'s
    // wl_global_set_user_data self-reference) — comparing it against the
    // currently active global is how a bind that raced a removal (the global
    // is kept allocated, not destroyed, precisely so this comparison is safe)
    // is told apart from an ordinary one.
    struct wl_global* bound_global = data;

    struct wl_resource* resource = wl_resource_create(client, &hyprglass_item_manager_v1_interface, (int)version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    struct manager_object* manager = calloc(1, sizeof(*manager));
    if (!manager) {
        wl_resource_destroy(resource);
        wl_client_post_no_memory(client);
        return;
    }

    manager->resource = resource;
    manager->inert    = bound_global != g_active_global;

    wl_list_insert(&g_managers, &manager->link);
    wl_resource_set_implementation(resource, &manager_impl, manager, manager_resource_destroy);
}

// ── Public API ───────────────────────────────────────────────────────────────

static int api_start(struct wl_display* display) {
    ensure_lists_initialized();

    if (g_active_global)
        return 0;

    // Safe to reclaim now: reaching a start() after a stop() means a full
    // plugin unload+reload has happened, so no client bind for a global
    // retired by that stop() can still be in flight.
    struct retired_global *retired, *retired_tmp;
    wl_list_for_each_safe(retired, retired_tmp, &g_retired, link) {
        wl_global_destroy(retired->global);
        wl_list_remove(&retired->link);
        free(retired);
    }

    struct wl_global* global = wl_global_create(display, &hyprglass_item_manager_v1_interface, 1, NULL, bind_manager);
    if (!global)
        return -1;

    wl_global_set_user_data(global, global);
    g_active_global = global;
    return 0;
}

static void api_stop(void) {
    ensure_lists_initialized();

    if (!g_active_global)
        return;

    struct manager_object* manager;
    wl_list_for_each(manager, &g_managers, link) {
        if (manager->inert)
            continue;
        manager->inert = 1;
        hyprglass_item_manager_v1_send_finished(manager->resource);
    }

    struct item_object* item;
    // An inert item no longer needs to hear about its surface, and leaving the
    // listener linked would outlive the item if the client releases it first.
    wl_list_for_each(item, &g_items, link) {
        if (item->listening) {
            wl_list_remove(&item->surface_destroy_listener.link);
            item->listening = 0;
        }
        item->inert   = 1;
        item->surface = NULL;
    }

    wl_global_remove(g_active_global);

    // Kept allocated rather than destroyed, in case a bind already in flight
    // still targets it (see bind_manager's comment); a later start() reclaims it.
    struct retired_global* retired = calloc(1, sizeof(*retired));
    if (!retired) {
        // Extremely unlikely (single small allocation); accept the tiny
        // in-flight-bind race rather than leak trying to retry indefinitely.
        wl_global_destroy(g_active_global);
    } else {
        retired->global = g_active_global;
        wl_list_insert(&g_retired, &retired->link);
    }

    g_active_global = NULL;
}

static void api_set_callbacks(void* userdata, hyprglass_item_created_fn on_created, hyprglass_item_destroyed_fn on_destroyed) {
    g_callback_userdata = userdata;
    g_on_created        = on_created;
    g_on_destroyed       = on_destroyed;
}

static void api_snapshot(struct wl_resource* surface) {
    ensure_lists_initialized();
    struct item_object* item = find_active_item_for_surface(surface);
    if (item)
        item->cached = item->pending;
}

static void api_apply(struct wl_resource* surface) {
    ensure_lists_initialized();
    struct item_object* item = find_active_item_for_surface(surface);
    if (item)
        item->current = item->cached;
}

static int api_get(struct wl_resource* surface, struct hyprglass_item_state* out_state) {
    ensure_lists_initialized();
    struct item_object* item = find_active_item_for_surface(surface);
    if (!item)
        return 0;
    *out_state = item->current;
    return 1;
}

static void api_for_each_active_item(hyprglass_item_foreach_fn callback, void* userdata) {
    ensure_lists_initialized();
    struct item_object* item;
    wl_list_for_each(item, &g_items, link) {
        if (!item->inert && item->surface)
            callback(item->surface, userdata);
    }
}

const struct hyprglass_item_helper_v1_api hyprglass_item_helper_v1_api = {
    .abi_version           = HYPRGLASS_ITEM_HELPER_V1_ABI_VERSION,
    .start                 = api_start,
    .stop                  = api_stop,
    .set_callbacks         = api_set_callbacks,
    .snapshot              = api_snapshot,
    .apply                 = api_apply,
    .get                   = api_get,
    .for_each_active_item  = api_for_each_active_item,
};
