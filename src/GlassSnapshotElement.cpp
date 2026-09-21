#include "GlassSnapshotElement.hpp"
#include "Globals.hpp"

#include <optional>
#include <GLES3/gl32.h>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>
#include <hyprland/src/render/OpenGL.hpp>

// The blit needs raw GL framebuffer ids. A framebuffer that is not GL-backed
// skips the snapshot rather than being dereferenced through a failed cast.
static std::optional<GLuint> fbId(const SP<Render::IFramebuffer>& framebuffer) {
    auto* gl = dynamic_cast<Render::GL::CGLFramebuffer*>(framebuffer.get());
    if (!gl)
        return std::nullopt;
    return gl->getFBID();
}

void requestXraySnapshot(PHLMONITOR monitor) {
    if (!g_pGlobalState || !monitor)
        return;

    g_pGlobalState->backgroundSnapshots[monitor->m_id].requestedFrame = g_pGlobalState->frameSerial;
}

bool prepareXraySnapshot(PHLMONITOR monitor) {
    const auto source = g_pHyprRenderer->m_renderData.currentFB;
    if (!g_pGlobalState || !monitor || !source || source->m_size.x <= 0 || source->m_size.y <= 0)
        return false;

    const auto it = g_pGlobalState->backgroundSnapshots.find(monitor->m_id);
    if (it == g_pGlobalState->backgroundSnapshots.end())
        return false;
    auto& snapshot = it->second;

    // Format as well as size: an FP16 frame (10-bit, HDR) blitted into an
    // 8-bit snapshot would band the glass, and the blit itself would convert
    // on every frame.
    if (snapshot.fb && snapshot.fb->m_size == source->m_size && snapshot.fb->m_drmFormat == source->m_drmFormat)
        return true;

    if (!snapshot.fb)
        snapshot.fb = g_pHyprRenderer->createFB("hyprglass-xray");

    if (!snapshot.fb->alloc(static_cast<int>(source->m_size.x), static_cast<int>(source->m_size.y), source->m_drmFormat))
        return false;

    // A framebuffer holding one frame's damage is not a background yet, so ask
    // for one full redraw to fill it and keep samplers on the live frame until
    // it lands. Deferred, since this frame's damage is already fixed; the lock
    // cancels the call if the plugin unloads before it runs.
    snapshot.complete          = false;
    snapshot.pendingFullRedraw = g_pEventLoopManager->doLaterLock([weak = PHLMONITORREF{monitor}] {
        if (const auto m = weak.lock())
            g_pHyprRenderer->damageMonitor(m);
    });

    return true;
}

SP<Render::IFramebuffer> xraySnapshotFor(PHLMONITOR monitor, const SP<Render::IFramebuffer>& frame) {
    if (!g_pGlobalState || !monitor || !frame)
        return nullptr;

    const auto it = g_pGlobalState->backgroundSnapshots.find(monitor->m_id);
    if (it == g_pGlobalState->backgroundSnapshots.end())
        return nullptr;

    const auto& snapshot = it->second;
    if (!snapshot.complete || !snapshot.fb || snapshot.fb->m_size != frame->m_size || snapshot.fb->m_drmFormat != frame->m_drmFormat)
        return nullptr;

    return snapshot.fb;
}

std::vector<UP<IPassElement>> CGlassSnapshotElement::draw() {
    if (!g_pGlobalState)
        return {};

    const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock();
    const auto source  = g_pHyprRenderer->m_renderData.currentFB;
    if (!monitor || !source)
        return {};

    const auto it = g_pGlobalState->backgroundSnapshots.find(monitor->m_id);
    if (it == g_pGlobalState->backgroundSnapshots.end())
        return {};
    auto& snapshot = it->second;

    // prepareXraySnapshot() sized it against this same frame; anything else
    // means the frame changed under us and next frame's prepare will catch it.
    if (!snapshot.fb || snapshot.fb->m_size != source->m_size || snapshot.fb->m_drmFormat != source->m_drmFormat)
        return {};

    const auto sourceId   = fbId(source);
    const auto snapshotId = fbId(snapshot.fb);
    if (!sourceId || !snapshotId)
        return {};

    // Only the damaged region is guaranteed to hold freshly drawn background;
    // outside it the frame still carries the previous composite, windows
    // included. m_renderData.damage is in the monitor's transformed space and
    // the framebuffer in the output's native orientation, so rotate it the way
    // scissor() does or a 90/270 monitor copies the wrong pixels.
    CRegion damage = g_pHyprRenderer->m_renderData.damage.copy();
    damage.intersect(CBox{{}, monitor->m_transformedSize});
    damage.transform(Math::wlTransformToHyprutils(Math::invertTransform(monitor->m_transform)), monitor->m_transformedSize.x, monitor->m_transformedSize.y);

    const int  width        = static_cast<int>(source->m_size.x);
    const int  height       = static_cast<int>(source->m_size.y);
    const bool scissorWasOn = glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE;

    // The pass scissors each element to its damage, which would clip the blit.
    g_pHyprOpenGL->setCapStatus(GL_SCISSOR_TEST, false);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, *sourceId);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, *snapshotId);
    for (const auto& rect : damage.getRects()) {
        const int x1 = std::max(0, static_cast<int>(rect.x1));
        const int y1 = std::max(0, static_cast<int>(rect.y1));
        const int x2 = std::min(width, static_cast<int>(rect.x2));
        const int y2 = std::min(height, static_cast<int>(rect.y2));
        if (x2 <= x1 || y2 <= y1)
            continue;
        glBlitFramebuffer(x1, y1, x2, y2, x1, y1, x2, y2, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    g_pHyprOpenGL->setCapStatus(GL_SCISSOR_TEST, scissorWasOn);

    // Usable from the frame whose damage has covered the whole monitor: from
    // then on, whatever the damage leaves untouched was copied earlier.
    if (!snapshot.complete) {
        CRegion missing{0, 0, static_cast<double>(width), static_cast<double>(height)};
        missing.subtract(damage);
        snapshot.complete = missing.empty();
    }

    // Leave the renderer's target bound the way it expects it.
    source->bind();
    return {};
}
