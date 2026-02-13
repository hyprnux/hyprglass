// Auto-generated shader header - Do not edit!
#pragma once

#include <unordered_map>
#include <string>

inline const std::unordered_map<std::string, const char*> SHADERS = {
    {"liquidglass.frag", R"GLSL(
#version 300 es
precision highp float;

/*
 * Apple-style Liquid Glass Fragment Shader
 *
 * Based on Apple's iOS 26 Liquid Glass design principles:
 * - Layer 1: Strong Gaussian blur (the primary glass look)
 * - Layer 2: SDF-based bezel refraction (thin edge strip only)
 * - Layer 3: Chromatic aberration in bezel zone
 * - Layer 4: Fresnel edge glow
 * - Layer 5: Specular highlights from surface normal
 */

uniform sampler2D tex;
uniform vec2 fullSize;
uniform float radius;
uniform vec2 uvPadding;

uniform float refractionStrength;  // Bezel refraction max displacement (0.0 - 1.0)
uniform float chromaticAberration; // RGB separation in bezel (0.0 - 1.0)
uniform float fresnelStrength;     // Edge glow intensity (0.0 - 1.0)
uniform float specularStrength;    // Highlight brightness (0.0 - 1.0)
uniform float glassOpacity;        // Overall glass opacity (0.0 - 1.0)
uniform float edgeThickness;       // Bezel width as fraction of min dimension (0.0 - 0.1)

in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

// ============================================================================
// TEXTURE SAMPLING (window UV -> padded texture UV)
// ============================================================================

vec2 toTexUV(vec2 wuv) {
    return wuv * (1.0 - 2.0 * uvPadding) + uvPadding;
}

vec4 sampleTex(vec2 wuv) {
    vec2 tuv = toTexUV(wuv);
    return texture(tex, clamp(tuv, 0.001, 0.999));
}

// ============================================================================
// SDF: Two variants — corner SDF (real shape) and bezel SDF (smoothed normals)
// ============================================================================

// Corner SDF: uses actual window radius for correct shape masking
float getCornerSDF(vec2 uv) {
    vec2 p = (uv - 0.5) * fullSize;
    vec2 halfSize = fullSize * 0.5;
    float r = min(radius, min(halfSize.x, halfSize.y));
    vec2 q = abs(p) - halfSize + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// Bezel SDF: uses smoothed radius so normals are continuous at corners
float getBezelSDF(vec2 uv) {
    vec2 p = (uv - 0.5) * fullSize;
    vec2 halfSize = fullSize * 0.5;
    float minDim = min(halfSize.x, halfSize.y);
    float bezelW = edgeThickness * minDim * 2.0;
    float r = max(radius, bezelW);
    r = min(r, minDim);
    vec2 q = abs(p) - halfSize + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// Normal from bezel SDF (smooth at corners)
vec2 bezelNormal(vec2 uv) {
    vec2 epsUV = 2.0 / fullSize;
    float dx = getBezelSDF(uv + vec2(epsUV.x, 0.0)) - getBezelSDF(uv - vec2(epsUV.x, 0.0));
    float dy = getBezelSDF(uv + vec2(0.0, epsUV.y)) - getBezelSDF(uv - vec2(0.0, epsUV.y));
    vec2 n = vec2(dx, dy);
    float len = length(n);
    return len > 0.001 ? n / len : vec2(0.0);
}

// ============================================================================
// MAIN
// ============================================================================

void main() {
    vec2 uv = v_texcoord;

    // Rounded corner mask using actual window radius
    float cornerSdf = getCornerSDF(uv);
    float cornerAlpha = 1.0 - smoothstep(-1.5, 0.5, cornerSdf);
    if (cornerAlpha < 0.001) discard;

    // Bezel zone follows actual window shape (cornerSdf), normals from smoothed SDF
    float bezelWidthPx = edgeThickness * min(fullSize.x, fullSize.y);
    float bezelFactor = clamp(1.0 + cornerSdf / bezelWidthPx, 0.0, 1.0);

    // Compute bezel displacement (only non-zero in the bezel strip)
    vec2 displacement = vec2(0.0);
    vec2 normal = vec2(0.0);

    if (bezelFactor > 0.001) {
        normal = bezelNormal(uv);

        // Convex circle-arc profile: strong at border, smooth falloff inward
        float dispAmount = sqrt(bezelFactor * (2.0 - bezelFactor));

        // Displacement perpendicular to border, pointing inward (convex lens)
        float maxPx = refractionStrength * 30.0;
        displacement = -normal * dispAmount * maxPx / fullSize;
    }

    // Sample at (potentially displaced) position
    vec2 sampleUV = uv + displacement;

    // ========================================
    // Layer 1: BACKGROUND (pre-blurred via two-pass Gaussian)
    // ========================================
    vec3 color = sampleTex(sampleUV).rgb;

    // ========================================
    // Layer 2: CHROMATIC ABERRATION (bezel only)
    // ========================================
    if (bezelFactor > 0.001 && chromaticAberration > 0.001) {
        float caPx = chromaticAberration * bezelFactor * 3.0;
        vec2 caOffset = normal * caPx / fullSize;
        color.r = sampleTex(sampleUV + caOffset).r;
        color.b = sampleTex(sampleUV - caOffset).b;
    }

    // ========================================
    // Layer 3: FRESNEL EDGE GLOW
    // ========================================
    float fresnel = pow(bezelFactor, 3.0) * fresnelStrength * 0.25;
    color += vec3(1.0) * fresnel;

    // ========================================
    // Layer 4: SPECULAR HIGHLIGHTS (depth cues)
    // ========================================
    if (bezelFactor > 0.001 && specularStrength > 0.001) {
        // Top highlight (light from above)
        vec2 lightDir = normalize(vec2(0.0, -1.0));
        float specDot = max(dot(normal, lightDir), 0.0);
        float spec = pow(specDot, 16.0) * bezelFactor * specularStrength * 0.5;

        // Side highlight for depth
        vec2 lightDir2 = normalize(vec2(-0.8, -0.4));
        float specDot2 = max(dot(normal, lightDir2), 0.0);
        spec += pow(specDot2, 24.0) * bezelFactor * specularStrength * 0.3;

        color += vec3(1.0, 0.99, 0.97) * spec;
    }

    // ========================================
    // Layer 5: INNER SHADOW (depth at bottom/right edges)
    // ========================================
    if (bezelFactor > 0.001) {
        vec2 shadowDir = normalize(vec2(0.3, 0.7));
        float shadowDot = max(dot(normal, shadowDir), 0.0);
        float shadow = pow(shadowDot, 8.0) * bezelFactor * 0.12;
        color *= 1.0 - shadow;
    }

    // Subtle cool glass tint
    color *= vec3(0.97, 0.97, 1.0);

    fragColor = vec4(color, glassOpacity * cornerAlpha);
}
)GLSL"},

    {"gaussianblur.frag", R"GLSL(
#version 300 es
precision highp float;

uniform sampler2D tex;
uniform vec2 direction; // (1.0/width, 0.0) for horizontal, (0.0, 1.0/height) for vertical
uniform float blurRadius; // kernel radius in pixels

in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

void main() {
    // Compute sigma from radius (covers ~3 sigma)
    float sigma = max(blurRadius / 3.0, 0.001);
    float invSigma2 = -0.5 / (sigma * sigma);

    vec4 result = vec4(0.0);
    float totalWeight = 0.0;

    int samples = int(ceil(blurRadius));
    for (int i = -samples; i <= samples; i++) {
        float x = float(i);
        float w = exp(x * x * invSigma2);
        result += texture(tex, v_texcoord + direction * x) * w;
        totalWeight += w;
    }

    fragColor = result / totalWeight;
}
)GLSL"},
};
