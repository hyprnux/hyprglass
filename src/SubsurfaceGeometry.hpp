#pragma once

#include <cmath>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Region.hpp>
#include <optional>

// Geometry helpers for "subsurface item glass": a glass item is a wl_subsurface
// of an app window (e.g. a toolbar capsule), enqueued into the render pass as a
// CSurfacePassElement. CSurfacePassElement::getTexBox() already returns the
// item's box in monitor-local LOGICAL coordinates — the exact contract
// IPassElement::boundingBox() expects (see SurfacePassElement.cpp) — so unlike
// CGlassLayerSurface (which recomputes its box from a long-lived PHLLS on every
// call), all geometry here is derived once from a box handed in by the caller,
// captured at CRenderPass::add() hook time while the CSurfacePassElement is
// still in hand.
namespace SubsurfaceGeometry {

// itemLogicalBox: CSurfacePassElement::getTexBox() for the item's subsurface,
// unmodified. Converts to a monitor-local PIXEL box in render space, which is
// also the pixel space of the framebuffers (Hyprland draws the pass unrotated,
// so a rotated monitor needs no further conversion) — the box
// GlassRenderer::sampleBackground()/applyGlassEffect() use for the desktop,
// matching CGlassLayerSurface's computeLayerBox() output.
[[nodiscard]] inline std::optional<CBox> toPixelBox(CBox itemLogicalBox, PHLMONITOR monitor) {
    if (!monitor)
        return std::nullopt;

    itemLogicalBox.scale(monitor->m_scale).round().noNegativeSize();

    if (!std::isfinite(itemLogicalBox.x) || !std::isfinite(itemLogicalBox.y) ||
        !std::isfinite(itemLogicalBox.w) || !std::isfinite(itemLogicalBox.h) ||
        itemLogicalBox.w <= 0.0 || itemLogicalBox.h <= 0.0)
        return std::nullopt;

    return itemLogicalBox;
}

// itemLogicalBox: CSurfacePassElement::getTexBox(), unmodified. Padded for the
// IPassElement::boundingBox() contract (monitor-local logical, pre-scale) so the
// render pass's damage expansion (needsLiveBlur) re-renders the desktop under
// the item before we sample it — mirrors LayerGeometry::computePaddedLogicalLayerBox().
[[nodiscard]] inline std::optional<CBox> toPaddedLogicalBox(CBox itemLogicalBox, PHLMONITOR monitor, float paddingPx) {
    if (!monitor)
        return std::nullopt;

    const float scale = monitor->m_scale > 0.0f ? monitor->m_scale : 1.0f;
    itemLogicalBox.expand(paddingPx / scale).noNegativeSize().round();

    if (!std::isfinite(itemLogicalBox.x) || !std::isfinite(itemLogicalBox.y) ||
        !std::isfinite(itemLogicalBox.w) || !std::isfinite(itemLogicalBox.h) ||
        itemLogicalBox.w <= 0.0 || itemLogicalBox.h <= 0.0)
        return std::nullopt;

    return itemLogicalBox;
}

// ext-background-effect-v1's blur region is surface-local logical coordinates,
// clipped to the subsurface's own committed size. Maps it into pixelBox's pixel
// space (same scale/translate sequence as
// CGlassLayerSurface::transformedBlurRegion()), ready for the shader's
// regionRects mask.
[[nodiscard]] inline CRegion transformedItemBlurRegion(const SP<CWLSurfaceResource>& surface, const CBox& pixelBox, PHLMONITOR monitor) {
    if (!surface || !monitor)
        return {};

    const auto wlSurface = Desktop::View::CWLSurface::fromResource(surface);
    if (!wlSurface)
        return {};

    const auto surfaceSize = surface->m_current.size;

    CRegion region = wlSurface->m_blurRegion.copy();
    region.intersect(0, 0, surfaceSize.x, surfaceSize.y);
    region.scale(static_cast<float>(monitor->m_scale));
    region.translate(pixelBox.pos());
    region.intersect(pixelBox.x, pixelBox.y, pixelBox.w, pixelBox.h);
    return region;
}

} // namespace SubsurfaceGeometry
