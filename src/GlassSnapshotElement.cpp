#include "GlassSnapshotElement.hpp"
#include "Globals.hpp"

#include <algorithm>
#include <optional>
#include <GLES3/gl32.h>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>
#include <hyprland/src/render/OpenGL.hpp>

// The blit needs raw GL framebuffer ids. A framebuffer that is not GL-backed
// (nothing else in the plugin renders to one either) just skips the snapshot.
static std::optional<GLuint> fbId(const SP<Render::IFramebuffer>& framebuffer) {
    auto* gl = dynamic_cast<Render::GL::CGLFramebuffer*>(framebuffer.get());
    if (!gl)
        return std::nullopt;
    return gl->getFBID();
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

    const int width  = static_cast<int>(source->m_size.x);
    const int height = static_cast<int>(source->m_size.y);
    if (width <= 0 || height <= 0)
        return {};

    const auto sourceId = fbId(source);
    if (!sourceId)
        return {};

    if (!snapshot.fb)
        snapshot.fb = g_pHyprRenderer->createFB("hyprglass-background");

    if (static_cast<int>(snapshot.fb->m_size.x) != width || static_cast<int>(snapshot.fb->m_size.y) != height) {
        if (!snapshot.fb->alloc(width, height, source->m_drmFormat))
            return {};
        const auto id = fbId(snapshot.fb);
        if (!id)
            return {};
        // Fresh (or resized) snapshot: start from transparent black so the
        // undamaged remainder never shows a stale frame, and have the whole
        // monitor redrawn next frame so that remainder fills in at once.
        glBindFramebuffer(GL_FRAMEBUFFER, *id);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        snapshot.complete = false;
        requestFullRedraw(monitor);
    }

    const auto snapshotId = fbId(snapshot.fb);
    if (!snapshotId)
        return {};

    // Only the damaged region is guaranteed to hold freshly drawn background
    // in the main framebuffer; outside it, the FB still carries the previous
    // frame's final composite — windows included. Copy exactly the damage.
    // The render pass scissors each element to its damage; that state would
    // clip glBlitFramebuffer on the draw side, so switch it off for the copy
    // and put it back the way it was.
    //
    // m_renderData.damage lives in the monitor's transformed (logical-pixel)
    // space — the render pass clips it to m_transformedSize and scissor()
    // rotates it into the framebuffer, whose orientation is the output's
    // native one. Rotate it the same way here: on a 90°/270° monitor the raw
    // rects address the wrong pixels and never cover the framebuffer, so the
    // snapshot stayed transparent black where the copy missed and the glass
    // rendered flat gray there.
    CRegion damage = g_pHyprRenderer->m_renderData.damage.copy();
    damage.intersect(CBox{{}, monitor->m_transformedSize});
    damage.transform(Math::wlTransformToHyprutils(Math::invertTransform(monitor->m_transform)),
                     monitor->m_transformedSize.x, monitor->m_transformedSize.y);
    const bool  scissorWasOn = glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE;
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

    // Complete once a frame's damage has covered the whole monitor: from then
    // on, whatever the damage leaves untouched is background copied earlier.
    if (!snapshot.complete) {
        CRegion missing{0, 0, static_cast<double>(width), static_cast<double>(height)};
        missing.subtract(damage);
        snapshot.complete = missing.empty();
    }

    // Leave the renderer's target bound the way it expects it.
    source->bind();
    return {};
}
