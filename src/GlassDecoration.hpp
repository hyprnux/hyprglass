#pragma once

#include "PluginConfig.hpp"

#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>
#include <hyprland/src/render/Framebuffer.hpp>

class CGlassDecoration : public IHyprWindowDecoration {
  public:
    explicit CGlassDecoration(PHLWINDOW window);
    ~CGlassDecoration() override = default;

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

    WP<CGlassDecoration> m_self;

    static constexpr int SAMPLE_PADDING_PX = 60;

  private:
    PHLWINDOWREF m_window;
    SP<IFramebuffer> m_sampleFramebuffer;
    Vector2D     m_samplePaddingRatio;

    // Track last rendered position/size to detect actual changes and seed damage
    Vector2D m_lastPosition;
    Vector2D m_lastSize;

    [[nodiscard]] bool        resolveThemeIsDark() const;
    [[nodiscard]] std::string resolvePresetName() const;

    void sampleBackground(SP<IFramebuffer> sourceFramebuffer, CBox box);
    void blurBackground(float radius, int iterations, GLuint callerFramebufferID, int viewportWidth, int viewportHeight);

    void applyGlassEffect(SP<IFramebuffer> sourceFramebuffer, SP<IFramebuffer> targetFramebuffer,
                          CBox& rawBox, CBox& transformedBox, float windowAlpha);
    void uploadThemeUniforms(const SResolveContext& resolveContext) const;

    friend class CGlassPassElement;
};
