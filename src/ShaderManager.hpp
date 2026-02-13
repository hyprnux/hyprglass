#pragma once

#include <GLES3/gl32.h>
#include <hyprland/src/render/Shader.hpp>
#include <string>

struct SGlassUniforms {
    GLint refractionStrength = -1;
    GLint chromaticAberration = -1;
    GLint fresnelStrength = -1;
    GLint specularStrength = -1;
    GLint glassOpacity = -1;
    GLint edgeThickness = -1;
    GLint uvPadding = -1;
    GLint tintColor = -1;
    GLint tintAlpha = -1;
    GLint lensDistortion = -1;
    GLint brightness = -1;
    GLint contrast = -1;
    GLint saturation = -1;
    GLint vibrancy = -1;
    GLint vibrancyDarkness = -1;
    GLint adaptiveDim = -1;
    GLint adaptiveBoost = -1;
    GLint roundingPower = -1;
};

struct SBlurUniforms {
    GLint direction = -1;
    GLint radius    = -1;
};

class CShaderManager {
  public:
    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

    void initializeIfNeeded();
    void destroy() noexcept;

    SShader        glassShader;
    SGlassUniforms glassUniforms;

    SShader        blurShader;
    SBlurUniforms  blurUniforms;

  private:
    bool m_initialized = false;

    [[nodiscard]] static std::string loadShaderSource(const char* fileName);
    [[nodiscard]] bool compileGlassShader();
    [[nodiscard]] bool compileBlurShader();
};
