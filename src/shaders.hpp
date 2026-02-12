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
uniform vec2 topLeft;
uniform vec2 fullSize;
uniform vec2 fullSizeUntransformed;
uniform float radius;
uniform float time;
uniform vec2 uvPadding;

uniform float blurStrength;        // Blur radius scale (0.0 - 3.0)
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
// SDF: Signed distance to rounded rectangle (pixels, negative inside)
// ============================================================================

float getSDF(vec2 uv) {
    vec2 p = (uv - 0.5) * fullSize;
    vec2 halfSize = fullSize * 0.5;
    float r = min(radius, min(halfSize.x, halfSize.y));
    vec2 q = abs(p) - halfSize + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// SDF gradient = outward-pointing normal (perpendicular to border)
vec2 sdfNormal(vec2 uv) {
    vec2 epsUV = 1.0 / fullSize;
    float dx = getSDF(uv + vec2(epsUV.x, 0.0)) - getSDF(uv - vec2(epsUV.x, 0.0));
    float dy = getSDF(uv + vec2(0.0, epsUV.y)) - getSDF(uv - vec2(0.0, epsUV.y));
    vec2 n = vec2(dx, dy);
    float len = length(n);
    return len > 0.001 ? n / len : vec2(0.0);
}

// ============================================================================
// BLUR: 16-sample Poisson disk for smooth background blur
// ============================================================================

vec3 poissonBlur(vec2 uv, float radiusPx) {
    const vec2 disk[16] = vec2[16](
        vec2(-0.94201624, -0.39906216),
        vec2( 0.94558609, -0.76890725),
        vec2(-0.09418410, -0.92938870),
        vec2( 0.34495938,  0.29387760),
        vec2(-0.91588581,  0.45771432),
        vec2(-0.81544232, -0.87912464),
        vec2(-0.38277543,  0.27676845),
        vec2( 0.97484398,  0.75648379),
        vec2( 0.44323325, -0.97511554),
        vec2( 0.53742981, -0.47373420),
        vec2(-0.26496911, -0.41893023),
        vec2( 0.79197514,  0.19090188),
        vec2(-0.24188840,  0.99706507),
        vec2(-0.81409955,  0.91437590),
        vec2( 0.19984126,  0.78641367),
        vec2( 0.14383161, -0.14100790)
    );

    vec2 texelSize = 1.0 / fullSize;
    vec3 result = sampleTex(uv).rgb;
    float total = 1.0;

    for (int i = 0; i < 16; i++) {
        vec2 offset = disk[i] * texelSize * radiusPx;
        float w = 1.0 - length(disk[i]) * 0.3;
        result += sampleTex(uv + offset).rgb * w;
        total += w;
    }

    return result / total;
}

// ============================================================================
// MAIN
// ============================================================================

void main() {
    vec2 uv = v_texcoord;

    // Rounded corner mask (anti-aliased)
    float sdf = getSDF(uv);
    float cornerAlpha = 1.0 - smoothstep(-1.5, 0.5, sdf);
    if (cornerAlpha < 0.001) discard;

    // Bezel zone: thin strip at the border where refraction occurs
    // bezelFactor: 0 = interior (no refraction), 1 = at border (max refraction)
    float bezelWidthPx = edgeThickness * min(fullSize.x, fullSize.y);
    float bezelFactor = clamp(1.0 + sdf / bezelWidthPx, 0.0, 1.0);

    // Compute bezel displacement (only non-zero in the bezel strip)
    vec2 displacement = vec2(0.0);
    vec2 normal = vec2(0.0);

    if (bezelFactor > 0.001) {
        normal = sdfNormal(uv);

        // Smooth convex profile: peaks at border, zero at inner edge
        float dispAmount = smoothstep(0.0, 1.0, bezelFactor);

        // Displacement perpendicular to border, pointing inward (convex lens)
        float maxPx = refractionStrength * 15.0;
        displacement = -normal * dispAmount * maxPx / fullSize;
    }

    // Sample at (potentially displaced) position
    vec2 sampleUV = uv + displacement;

    // ========================================
    // Layer 1: BACKGROUND BLUR (the primary glass effect)
    // ========================================
    float blurRadius = blurStrength * 12.0;
    vec3 color = poissonBlur(sampleUV, blurRadius);

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
    float fresnel = pow(bezelFactor, 3.0) * fresnelStrength * 0.15;
    color += vec3(1.0) * fresnel;

    // ========================================
    // Layer 4: SPECULAR HIGHLIGHTS
    // ========================================
    if (bezelFactor > 0.001 && specularStrength > 0.001) {
        // Primary light from top-left
        vec2 lightDir = normalize(vec2(-0.7, -0.7));
        float specDot = max(dot(normal, lightDir), 0.0);
        float spec = pow(specDot, 24.0) * bezelFactor * specularStrength * 0.4;

        // Secondary light from bottom-right for depth
        vec2 lightDir2 = normalize(vec2(0.5, 0.8));
        float specDot2 = max(dot(normal, lightDir2), 0.0);
        spec += pow(specDot2, 32.0) * bezelFactor * specularStrength * 0.2;

        color += vec3(1.0, 0.98, 0.95) * spec;
    }

    // Subtle cool glass tint
    color *= vec3(0.97, 0.97, 1.0);

    fragColor = vec4(color, glassOpacity * cornerAlpha);
}
)GLSL"},
};
