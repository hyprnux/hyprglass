#include "GlassLayerPassElement.hpp"
#include "GlassLayerSurface.hpp"
#include "GlassRenderer.hpp"
#include "Globals.hpp"
#include "LayerGeometry.hpp"

#include <cmath>

CGlassLayerPassElement::CGlassLayerPassElement(const SGlassLayerPassData& data)
    : m_data(data) {}

std::vector<UP<IPassElement>> CGlassLayerPassElement::draw() {
    if (m_data.layerState && m_data.layerState->getLayerSurface())
        m_data.layerState->sampleAndRedirect(g_pHyprRenderer->m_renderData.pMonitor.lock(), m_data.alpha);

    return {};
}

std::optional<CBox> CGlassLayerPassElement::boundingBox() {
    if (!m_data.layerState)
        return std::nullopt;

    auto layerSurface = m_data.layerState->getLayerSurface();
    if (!layerSurface)
        return std::nullopt;

    const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock();
    auto box = LayerGeometry::computeLayerBox(layerSurface, monitor);
    if (!box)
        return std::nullopt;

    // Include the previous box position in boundingBox so Hyprland's needsLiveBlur()
    // forces damage re-rendering of background windows (like Kitty) across the ENTIRE
    // movement path, preventing stale glass feedback artifacts.
    Vector2D lastPos = m_data.layerState->getLastPosition();
    Vector2D lastSize = m_data.layerState->getLastSize();
    if (lastSize.x > 0.0 && lastSize.y > 0.0 && std::isfinite(lastPos.x) && std::isfinite(lastPos.y)) {
        CBox lastBox{lastPos, lastSize};
        lastBox.translate(-monitor->m_position);
        lastBox.scale(monitor->m_scale).round().noNegativeSize();
        double minX = std::min(box->x, lastBox.x);
        double minY = std::min(box->y, lastBox.y);
        double maxX = std::max(box->x + box->w, lastBox.x + lastBox.w);
        double maxY = std::max(box->y + box->h, lastBox.y + lastBox.h);
        box->x = minX;
        box->y = minY;
        box->w = maxX - minX;
        box->h = maxY - minY;
    }

    const float scale = monitor->m_scale > 0.0f ? monitor->m_scale : 1.0f;
    box->scale(1.0 / scale).expand(GlassRenderer::SAMPLE_PADDING_PX / scale).noNegativeSize().round();
    if (!std::isfinite(box->x) || !std::isfinite(box->y) || !std::isfinite(box->w) || !std::isfinite(box->h) || box->w <= 0.0 || box->h <= 0.0)
        return std::nullopt;

    return box;
}

bool CGlassLayerPassElement::needsLiveBlur() {
    // Ensure the render pass fully re-renders the background behind this
    // element before we sample it. Without this, partial damage causes the
    // glass to sample a mix of fresh wallpaper and its own stale output.
    // Per-monitor sceneGeneration prevents non-focused monitors from
    // re-sampling, so the continuous damage cost is limited.
    return m_data.layerState && m_data.layerState->getLayerSurface();
}

bool CGlassLayerPassElement::needsPrecomputeBlur() {
    return false;
}

bool CGlassLayerPassElement::disableSimplification() {
    return m_data.layerState && m_data.layerState->getLayerSurface();
}
