#pragma once

#include "GlassRenderer.hpp"
#include "PluginConfig.hpp"

#include <chrono>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/SharedDefs.hpp>

class CGlassDecoration : public IHyprWindowDecoration {
  public:
    explicit CGlassDecoration(PHLWINDOW window);
    ~CGlassDecoration() override;

    [[nodiscard]] SDecorationPositioningInfo getPositioningInfo() override;
    void                                     onPositioningReply(const SDecorationPositioningReply& reply) override;
    void                                     draw(PHLMONITOR monitor, float const& alpha) override;
    [[nodiscard]] eDecorationType            getDecorationType() override;
    void                                     updateWindow(PHLWINDOW window) override;
    void                                     damageEntire() override;
    [[nodiscard]] eDecorationLayer           getDecorationLayer() override;
    [[nodiscard]] uint64_t                   getDecorationFlags() override;
    [[nodiscard]] std::string                getDisplayName() override;

    [[nodiscard]] PHLWINDOW getOwner();
    void                    renderPass(PHLMONITOR monitor, const float& alpha);
    void                    onFullscreenStateChanged();

    // Content below committed damage in our sample region — resample next frame.
    // Public: a future commit-driven invalidation pass calls this on any
    // decoration whose padded box overlaps a commit from outside the
    // window's own surface tree.
    void markBackgroundDirty();

    // Pure predicate: true when the cached sample must not be reused as-is
    // this frame. Called identically from CGlassPassElement::needsLiveBlur()
    // (draw()-time) and from renderPass() itself — safe to call twice, no GL
    // calls. transformBox is monitor-local physical pixels (see callers).
    [[nodiscard]] bool wantsBackgroundResample(PHLMONITOR monitor, const CBox& transformBox) const;

    // Owner test without the shared_ptr copy getOwner() hands out.
    [[nodiscard]] bool  ownsWindow(const PHLWINDOW& window) const { return m_window == window; }
    // self_sample as of the last rendered frame, 0 when the glass is off. Read on
    // every watched surface commit, so it must not walk the preset chain.
    [[nodiscard]] float lastSelfSample() const { return m_lastSelfSample; }

    // Weak over the UP Hyprland owns: use .get()/-> only, never .lock().
    WP<CGlassDecoration> m_self;

  private:
    PHLWINDOWREF m_window;
    SP<Render::IFramebuffer> m_sampleFramebuffer;
    Vector2D     m_samplePaddingRatio;

    // Last geometry seen in updateWindow(), to detect real moves/resizes
    Vector2D m_lastPosition;
    Vector2D m_lastSize;

    // Window background cache state.
    bool m_hasCachedSample = false;
    bool m_backgroundDirty = false;
    std::chrono::steady_clock::time_point m_lastDirtyMark{};

    // Scene generation at the last real sample, plus the monitor it was
    // captured against. SGlobalState::sceneGeneration is deliberately keyed
    // per-MONITORID (see Globals.hpp), so a bare generation compare could
    // alias a value from a different monitor's independent counter sequence
    // when this window is dragged across monitors — storing the monitor
    // alongside the generation and treating any mismatch as "wants resample"
    // closes that gap.
    uint64_t  m_lastSceneGeneration   = 0;
    MONITORID m_lastGenerationMonitor = 0;

    // Whether we currently hold the noblur window property on our window.
    // Glass replaces Hyprland's blur, and Hyprland's cached-blur optimization
    // (blur:new_optimizations) composites translucent windows against a
    // snapshot taken before plugin decorations render — without noblur the
    // glass is invisible on static windows (#46).
    bool m_noBlurApplied = false;

    float m_lastSelfSample = 0.0f;

    // Frame serial the last glass element was queued for, and its index in that
    // frame. Hyprland renders a floating window over fullscreen more than once
    // per frame, and only the last glass may sample and blur.
    uint64_t m_glassFrameSerial = 0;
    uint32_t m_glassQueueIndex  = 0;

    // Frame serial this window last folded into the monitor's render-order
    // fingerprint. draw() itself can run more than once per monitor frame for
    // the same window (floating over fullscreen), and only the first copy may
    // fold — a second fold in the same frame would make the hash depend on how
    // many duplicate copies happened to render, not on the scene.
    uint64_t m_lastFoldedFrameSerial = 0;

    void               queueGlassPass(float alpha);
    [[nodiscard]] bool isCurrentGlassPass(uint64_t serial, uint32_t index) const {
        return serial == 0 || (serial == m_glassFrameSerial && index == m_glassQueueIndex);
    }

    void updateNoBlurProp(bool glassEnabled);
    void withdrawNoBlur();

    // The reason resolveEnabled() reached its answer, so callers can
    // attribute a Diagnostics counter to the actual cause instead of
    // re-deriving "is it opaque" independently — a window can be opaque and
    // still disabled for another reason (tag, global config) that took
    // precedence, and re-deriving would mis-attribute those to opaque-skip.
    enum class EEnabledResolution {
        Enabled,
        Disabled,              // hyprglass_disabled tag, or plugin:hyprglass:enabled = 0
        DisabledBecauseOpaque, // skip_opaque_windows and the window is opaque
    };

    [[nodiscard]] EEnabledResolution resolveEnabled() const;
    [[nodiscard]] bool               resolveThemeIsDark() const;
    [[nodiscard]] std::string        resolvePresetName() const;

    friend class CGlassPassElement;
};
