#include "GlassSnapshotElement.hpp"
#include "Diagnostics.hpp"
#include "GlassRenderer.hpp"
#include "Globals.hpp"
#include "RenderGuards.hpp"

#include <optional>
#include <GLES3/gl32.h>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>
#include <hyprland/src/render/OpenGL.hpp>

// About 10 s at 60 Hz of this monitor's own frames without a request.
static constexpr uint64_t IDLE_FRAMES = 600;

// The blit needs raw GL framebuffer ids. A framebuffer that is not GL-backed
// skips the snapshot rather than being dereferenced through a failed cast.
static std::optional<GLuint> glFramebufferId(const SP<Render::IFramebuffer>& framebuffer) {
    auto* glFramebuffer = dynamic_cast<Render::GL::CGLFramebuffer*>(framebuffer.get());
    if (!glFramebuffer)
        return std::nullopt;
    return glFramebuffer->getFBID();
}

static bool matchesFrame(const SP<Render::IFramebuffer>& snapshot, const SP<Render::IFramebuffer>& frame) {
    return snapshot && frame && snapshot->m_size == frame->m_size && snapshot->m_drmFormat == frame->m_drmFormat;
}

// The render damage is in the monitor's transformed space, the framebuffer in
// the output's native orientation: rotate it like Hyprland's own frame damage.
static CRegion framebufferDamage(const PHLMONITOR& monitor, const CRegion& damage) {
    CRegion region = damage.copy();
    region.intersect(CBox{{}, monitor->m_transformedSize});
    region.transform(Math::wlTransformToHyprutils(Math::invertTransform(monitor->m_transform)), monitor->m_transformedSize.x,
                     monitor->m_transformedSize.y);
    return region;
}

bool xrayHasBackground() {
    const auto& config = g_pGlobalState->config;
    return readIntegerConfig(config.xpMode) == 0;
}

void requestXraySnapshot(PHLMONITOR monitor) {
    if (!g_pGlobalState || !monitor)
        return;

    auto& snapshot            = g_pGlobalState->backgroundSnapshots[monitor->m_id];
    snapshot.lastRequestFrame = snapshot.monitorFrames;
}

void beginXraySnapshotFrame(Render::CRenderContext& ctx) {
    const auto monitor = ctx.m_data.pMonitor.lock();
    if (!g_pGlobalState || !monitor)
        return;

    auto&      snapshots = g_pGlobalState->backgroundSnapshots;
    const auto it        = snapshots.find(monitor->m_id);
    if (it == snapshots.end())
        return;
    auto& snapshot = it->second;

    // Here rather than at PRE_WINDOWS, which a solitary fullscreen client skips:
    // its frames still age the snapshot out.
    if (++snapshot.monitorFrames > snapshot.lastRequestFrame + IDLE_FRAMES) {
        snapshots.erase(it);
        Diagnostics::recordXrayEvict(monitor->m_id);
        return;
    }

    // Fires for every frame, solitary and mirror frames included, so a frame
    // that never reaches PRE_WINDOWS still leaves its damage invalid.
    if (!snapshot.valid.empty())
        snapshot.valid.subtract(framebufferDamage(monitor, ctx.m_data.damage));
}

void queueXraySnapshot(Render::CRenderContext& ctx) {
    // Not the monitor's own frame: an overview plugin emits this stage while
    // rendering its own framebuffer, which has the windows in it already.
    if (!g_pGlobalState || RenderGuards::isForeignRender(ctx) || ctx.m_renderingSnapshot || ctx.m_data.projectionType != Render::RPT_MONITOR)
        return;

    const auto monitor = ctx.m_data.pMonitor.lock();
    const auto source  = ctx.m_data.currentFB;
    if (!monitor || !source || source->m_size.x <= 0 || source->m_size.y <= 0)
        return;

    const auto it = g_pGlobalState->backgroundSnapshots.find(monitor->m_id);
    if (it == g_pGlobalState->backgroundSnapshots.end())
        return;
    auto& snapshot = it->second;

    // A later PRE_WINDOWS of the same frame (special workspace) has windows under it.
    if (snapshot.queuedFrame == snapshot.monitorFrames)
        return;
    snapshot.queuedFrame = snapshot.monitorFrames;

    // Format as well as size: an FP16 frame blitted into an 8-bit snapshot would
    // band the glass and convert on every copy.
    if (!matchesFrame(snapshot.framebuffer, source)) {
        snapshot.valid.clear();
        if (!snapshot.framebuffer)
            snapshot.framebuffer = g_pHyprRenderer->createFB("hyprglass-xray");
        if (!snapshot.framebuffer->alloc(static_cast<int>(source->m_size.x), static_cast<int>(source->m_size.y), source->m_drmFormat))
            return;
    }

    g_pHyprRenderer->currentPass(ctx).add(makeUnique<CGlassSnapshotElement>());
}

SP<Render::IFramebuffer> xraySnapshotCovering(PHLMONITOR monitor, const SP<Render::IFramebuffer>& frame, const CBox& box) {
    if (!g_pGlobalState || !monitor)
        return nullptr;

    const auto it = g_pGlobalState->backgroundSnapshots.find(monitor->m_id);
    if (it == g_pGlobalState->backgroundSnapshots.end())
        return nullptr;

    const auto& snapshot = it->second;
    if (!matchesFrame(snapshot.framebuffer, frame) || !GlassRenderer::sampleRegionCovered(box, snapshot.framebuffer, snapshot.valid, nullptr))
        return nullptr;

    return snapshot.framebuffer;
}

std::vector<UP<IPassElement>> CGlassSnapshotElement::draw(Render::CRenderContext& ctx) {
    if (!g_pGlobalState)
        return {};

    const auto monitor = ctx.m_data.pMonitor.lock();
    const auto source  = ctx.m_data.currentFB;
    if (!monitor || !source)
        return {};

    const auto it = g_pGlobalState->backgroundSnapshots.find(monitor->m_id);
    if (it == g_pGlobalState->backgroundSnapshots.end())
        return {};
    auto& snapshot = it->second;

    if (!matchesFrame(snapshot.framebuffer, source))
        return {};

    const auto sourceId   = glFramebufferId(source);
    const auto snapshotId = glFramebufferId(snapshot.framebuffer);
    if (!sourceId || !snapshotId)
        return {};

    // Only this element's damage holds freshly drawn background: the work buffer
    // is cleared elsewhere, and occluded damage was never drawn.
    CRegion copied = framebufferDamage(monitor, ctx.m_data.damage);
    copied.intersect(CBox{{}, source->m_size});
    if (copied.empty())
        return {};

    // The pass scissors each element to its damage, which would clip the blit.
    g_pHyprOpenGL->setCapStatus(GL_SCISSOR_TEST, false);
    // the tracker skips glDisable when it believes the test is already off
    if (glIsEnabled(GL_SCISSOR_TEST)) {
        glDisable(GL_SCISSOR_TEST);
        Diagnostics::recordStateDesync("scissor on before the x-ray copy");
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, *sourceId);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, *snapshotId);
    for (const auto& rect : copied.getRects())
        glBlitFramebuffer(rect.x1, rect.y1, rect.x2, rect.y2, rect.x1, rect.y1, rect.x2, rect.y2, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    snapshot.valid.add(copied);
    Diagnostics::recordXrayCopy(monitor->m_id);

    // Leave the renderer's target bound the way it expects it.
    source->bind();
    return {};
}
