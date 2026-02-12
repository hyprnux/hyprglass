#pragma once

#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>
#include <hyprland/src/render/Framebuffer.hpp>

class CLiquidGlassDecoration : public IHyprWindowDecoration {
  public:
    CLiquidGlassDecoration(PHLWINDOW pWindow);
    virtual ~CLiquidGlassDecoration() = default;

    virtual SDecorationPositioningInfo getPositioningInfo() override;
    virtual void                       onPositioningReply(const SDecorationPositioningReply& reply) override;
    virtual void                       draw(PHLMONITOR pMonitor, float const& a) override;
    virtual eDecorationType            getDecorationType() override;
    virtual void                       updateWindow(PHLWINDOW pWindow) override;
    virtual void                       damageEntire() override;
    virtual eDecorationLayer           getDecorationLayer() override;
    virtual uint64_t                   getDecorationFlags() override;
    virtual std::string                getDisplayName() override;

    PHLWINDOW                          getOwner();
    void                               renderPass(PHLMONITOR pMonitor, const float& a);

    WP<CLiquidGlassDecoration>         m_self;

  private:
    PHLWINDOWREF m_pWindow;
    CFramebuffer m_sampleFB;
    Vector2D     m_samplePaddingRatio;

    static constexpr int SAMPLE_PADDING_PX = 40;

    void sampleBackground(CFramebuffer& sourceFB, CBox box);
    void applyLiquidGlassEffect(CFramebuffer& sourceFB, CFramebuffer& targetFB,
                                 CBox& rawBox, CBox& transformedBox, float windowAlpha);

    friend class CLiquidGlassPassElement;
};
