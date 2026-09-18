#include "GlassLayerPassElement.hpp"
#include "GlassLayerSurface.hpp"
#include "GlassRenderer.hpp"
#include "Globals.hpp"
#include "LayerGeometry.hpp"
#include "PluginConfig.hpp"

CGlassLayerPassElement::CGlassLayerPassElement(const SGlassLayerPassData& data)
    : m_data(data) {}

std::vector<UP<IPassElement>> CGlassLayerPassElement::draw() {
    // debug:mode = hints_only: keep every hint below but do none of the GL
    // work, to isolate the render pass's own cost from the glass pipeline's.
    if (currentDebugMode() == EDebugMode::HINTS_ONLY)
        return {};

    if (m_data.layerState && m_data.layerState->getLayerSurface())
        m_data.layerState->sampleAndRedirect(g_pHyprRenderer->m_renderData.pMonitor.lock(), m_data.alpha);

    return {};
}

std::optional<CBox> CGlassLayerPassElement::paddedLogicalBox() const {
    if (!m_data.layerState)
        return std::nullopt;

    auto layerSurface = m_data.layerState->getLayerSurface();
    if (!layerSurface)
        return std::nullopt;

    const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock();
    return LayerGeometry::computePaddedLogicalLayerBox(layerSurface, monitor, GlassRenderer::SAMPLE_PADDING_PX);
}

std::optional<CBox> CGlassLayerPassElement::boundingBox() {
    return paddedLogicalBox();
}

bool CGlassLayerPassElement::needsLiveBlur() {
    // debug:mode = gl_work_only: run the GL pipeline but withhold these
    // hints, isolating their render-pass cost from the pipeline's own GL cost.
    if (currentDebugMode() == EDebugMode::GL_WORK_ONLY)
        return false;

    // Ensure the render pass fully re-renders the background behind this
    // element before we sample it. Without this, partial damage causes the
    // glass to sample a mix of fresh wallpaper and its own stale output.
    // Per-monitor sceneGeneration prevents non-focused monitors from
    // re-sampling, so the continuous damage cost is limited.
    //
    // Must agree with boundingBox() on whether a box exists: Hyprland's
    // CRenderPass::render() asserts a bounding box for any element reporting
    // live blur ("No bounding box for an element with live blur is illegal",
    // Pass.cpp) and aborts the compositor if it's absent.
    return paddedLogicalBox().has_value();
}

bool CGlassLayerPassElement::needsPrecomputeBlur() {
    return false;
}

bool CGlassLayerPassElement::disableSimplification() {
    // Left enabled, including under debug:mode = gl_work_only: an element whose
    // padded box misses the render pass's damage is safely discarded here — no
    // partial-box artifact, same reasoning as CGlassPassElement. The post-surface
    // composite element is evaluated first in CRenderPass::simplify()'s
    // back-to-front walk, against a larger remaining-damage region than this
    // element sees, so in practice it is discarded whenever this one is.
    // CGlassLayerSurface::m_redirectedThisFrame is the safety net for the
    // remaining case: it makes compositeAndRestore() bail out instead of
    // compositing against a temp FBO this frame never redirected into.
    return false;
}
