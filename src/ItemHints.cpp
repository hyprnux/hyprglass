#include "ItemHints.hpp"
#include "Globals.hpp"
#include "item-helper/hyprglass_item_helper_api.h"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>

#include <hyprutils/signal/Listener.hpp>

#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>

#include <format>
#include <unordered_map>

// The blob is a separate translation unit (see ItemHelperBlob.S) so the
// Makefile can rebuild it whenever the helper's own sources change without
// recompiling this file.
extern "C" {
extern const unsigned char hyprglassItemHelperBlobStart[];
extern const unsigned char hyprglassItemHelperBlobEnd[];
}

namespace {
    // "struct" is required here: the extern variable declared by the header
    // shares this identifier, and in C++ that hides the struct tag for plain
    // unqualified lookup.
    const struct hyprglass_item_helper_v1_api* g_api = nullptr;

    struct SItemSurfaceListeners {
        Hyprutils::Signal::CHyprSignalListener precommit; // pending -> cached
        Hyprutils::Signal::CHyprSignalListener commit;    // cached -> current
    };

    // One entry per surface with an active item, keyed by the raw wl_resource:
    // on_item_destroyed can fire from the surface's own destroy signal, after
    // Hyprland's earlier destroy listener has already freed CWLSurfaceResource,
    // so that path must never resolve the resource back into Hyprland objects.
    std::unordered_map<wl_resource*, SItemSurfaceListeners> g_surfaceListeners;

    void warnProtocolUnavailable(std::string_view reason) {
        HyprlandAPI::addNotificationV2(PHANDLE, {
            {"text", std::format("[{}] hyprglass_item_v1 protocol unavailable ({}) — per-item preset/shape hints disabled", PLUGIN_NAME, reason)},
            {"time", (uint64_t)6000},
            {"color", CHyprColor{1.0, 0.8, 0.2, 1.0}},
        });
    }

    wl_resource* wireResource(CWLSurfaceResource* surface) {
        if (!surface)
            return nullptr;
        auto wrapper = surface->getResource();
        return wrapper ? wrapper->resource() : nullptr;
    }

    // Also used to enumerate items surviving a previous load's crash/forced
    // eject (see ItemHints::init()): same signature as the helper's
    // hyprglass_item_created_fn/hyprglass_item_foreach_fn.
    void onItemCreated(wl_resource* surfaceResource, void*) {
        auto surface = CWLSurfaceResource::fromResource(surfaceResource);
        if (!surface)
            return;

        if (g_surfaceListeners.contains(surfaceResource))
            return; // the helper guarantees at most one active item per surface

        SItemSurfaceListeners listeners;
        listeners.precommit = surface->m_events.precommit.listen([surfaceResource] {
            if (g_api)
                g_api->snapshot(surfaceResource);
        });
        listeners.commit = surface->m_events.commit.listen([surfaceResource] {
            if (g_api)
                g_api->apply(surfaceResource);
        });
        g_surfaceListeners.emplace(surfaceResource, std::move(listeners));
    }

    void onItemDestroyed(wl_resource* surfaceResource, void*) {
        g_surfaceListeners.erase(surfaceResource);
    }

    bool loadHelper() {
        if (void* adopted = dlsym(RTLD_DEFAULT, "hyprglass_item_helper_v1_api")) {
            g_api = static_cast<const struct hyprglass_item_helper_v1_api*>(adopted);
            return true;
        }

        const auto blobSize = static_cast<size_t>(hyprglassItemHelperBlobEnd - hyprglassItemHelperBlobStart);

        int fd = memfd_create("hyprglass-item-helper", MFD_CLOEXEC);
        if (fd < 0) {
            warnProtocolUnavailable("memfd_create failed");
            return false;
        }

        auto* data = reinterpret_cast<const char*>(hyprglassItemHelperBlobStart);
        size_t written = 0;
        while (written < blobSize) {
            ssize_t n = write(fd, data + written, blobSize - written);
            if (n <= 0) {
                close(fd);
                warnProtocolUnavailable("failed to write helper blob to memfd");
                return false;
            }
            written += static_cast<size_t>(n);
        }

        const auto fdPath = std::format("/proc/self/fd/{}", fd);
        void* handle = dlopen(fdPath.c_str(), RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE);
        close(fd); // dlopen has its own reference to the file by this point

        if (!handle) {
            warnProtocolUnavailable(std::format("dlopen failed: {}", dlerror()));
            return false;
        }

        void* symbol = dlsym(handle, "hyprglass_item_helper_v1_api");
        if (!symbol) {
            warnProtocolUnavailable("helper is missing its API symbol");
            return false;
        }

        g_api = static_cast<const struct hyprglass_item_helper_v1_api*>(symbol);
        return true;
    }
}

bool ItemHints::init() {
    if (!loadHelper())
        return false;

    if (g_api->abi_version != HYPRGLASS_ITEM_HELPER_V1_ABI_VERSION) {
        warnProtocolUnavailable(std::format("ABI version mismatch, helper {} != plugin {}", g_api->abi_version, HYPRGLASS_ITEM_HELPER_V1_ABI_VERSION));
        g_api = nullptr;
        return false;
    }

    if (g_api->start(g_pCompositor->m_wlDisplay) != 0) {
        warnProtocolUnavailable("failed to create the wayland global");
        g_api = nullptr;
        return false;
    }

    g_api->set_callbacks(nullptr, &onItemCreated, &onItemDestroyed);

    // Recovers listeners for items left active by a previous plugin load that
    // never ran ItemHints::exit() (a crash or forced eject skips PLUGIN_EXIT —
    // see PluginSystem.cpp's `eject` path). A clean unload already went
    // through stop(), which leaves nothing for this to find.
    g_api->for_each_active_item(&onItemCreated, nullptr);

    return true;
}

void ItemHints::exit() {
    if (!g_api)
        return;

    // Every function pointer into this plugin must be cleared before
    // Hyprland dlcloses it — the helper itself is never unloaded and would
    // otherwise keep calling back into unmapped code.
    g_api->set_callbacks(nullptr, nullptr, nullptr);
    g_api->stop();
    g_surfaceListeners.clear();
    g_api = nullptr;
}

bool ItemHints::active() {
    return g_api != nullptr;
}

std::optional<SItemHints> ItemHints::forSurface(CWLSurfaceResource* surface) {
    if (!g_api)
        return std::nullopt;

    auto* resource = wireResource(surface);
    if (!resource)
        return std::nullopt;

    hyprglass_item_state state{};
    if (!g_api->get(resource, &state))
        return std::nullopt;

    SItemHints hints;
    hints.preset = state.preset;
    switch (state.shape_mode) {
        case HYPRGLASS_ITEM_SHAPE_EXPLICIT: hints.shapeMode = eItemShapeMode::EXPLICIT; break;
        case HYPRGLASS_ITEM_SHAPE_INHERIT_WINDOW: hints.shapeMode = eItemShapeMode::INHERIT_WINDOW; break;
        default: hints.shapeMode = eItemShapeMode::NONE; break;
    }
    hints.x      = state.x;
    hints.y      = state.y;
    hints.width  = state.width;
    hints.height = state.height;
    hints.radii  = {state.radii[0], state.radii[1], state.radii[2], state.radii[3]};

    return hints;
}
