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
 * Rendering layers:
 * 1. Full-surface convex lens distortion (height-map from SDF)
 * 2. Bezel refraction with spectral dispersion (5-tap rainbow)
 * 3. Frosted tint (brightness boost + desaturation)
 * 4. Configurable color tint overlay
 * 5. Environment reflection (top-down gradient)
 * 6. Fresnel edge glow (Schlick approximation)
 * 7. Specular highlights (two light sources)
 * 8. Inner shadow (bottom/right depth)
 * 9. Outer glow / soft drop shadow
 */

uniform sampler2D tex;
uniform vec2 fullSize;
uniform float radius;
uniform vec2 uvPadding;

uniform float refractionStrength;
uniform float chromaticAberration;
uniform float fresnelStrength;
uniform float specularStrength;
uniform float glassOpacity;
uniform float edgeThickness;
uniform vec3 tintColor;
uniform float tintAlpha;
uniform float lensDistortion;
uniform float backgroundBrightness;
uniform float backgroundSaturation;
uniform float environmentStrength;
uniform float shadowStrength;

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
// SDF
// ============================================================================

float getRoundedBoxSDF(vec2 uv, float r) {
    vec2 p = (uv - 0.5) * fullSize;
    vec2 halfSize = fullSize * 0.5;
    float clampedR = min(r, min(halfSize.x, halfSize.y));
    vec2 q = abs(p) - halfSize + clampedR;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - clampedR;
}

float getCornerSDF(vec2 uv) {
    return getRoundedBoxSDF(uv, radius);
}

// Bezel SDF: smoothed radius for continuous normals at corners
float getBezelSDF(vec2 uv) {
    float minDim = min(fullSize.x, fullSize.y) * 0.5;
    float bezelW = edgeThickness * minDim * 2.0;
    float r = min(max(radius, bezelW), minDim);
    return getRoundedBoxSDF(uv, r);
}

// SDF gradient (normalized) from bezel SDF
vec2 sdfNormal(vec2 uv) {
    vec2 eps = 2.0 / fullSize;
    float dx = getBezelSDF(uv + vec2(eps.x, 0.0)) - getBezelSDF(uv - vec2(eps.x, 0.0));
    float dy = getBezelSDF(uv + vec2(0.0, eps.y)) - getBezelSDF(uv - vec2(0.0, eps.y));
    vec2 n = vec2(dx, dy);
    float len = length(n);
    return len > 0.001 ? n / len : vec2(0.0);
}

// Analytical convex dome: (1-x²)(1-y²) in normalized [-1,1] UV space.
// Perfectly smooth everywhere — no SDF quadrant artifacts.
float domeHeight(vec2 uv) {
    vec2 c = (uv - 0.5) * 2.0;
    float cx2 = c.x * c.x;
    float cy2 = c.y * c.y;
    return max((1.0 - cx2) * (1.0 - cy2), 0.0);
}

// Analytical gradient of dome — smooth polynomial, no diagonal seams.
// Returns gradient in UV space (chain rule: dc/duv = 2).
vec2 domeGradient(vec2 uv) {
    vec2 c = (uv - 0.5) * 2.0;
    float cx2 = c.x * c.x;
    float cy2 = c.y * c.y;
    return vec2(
        -4.0 * c.x * (1.0 - cy2),
        -4.0 * c.y * (1.0 - cx2)
    );
}

// ============================================================================
// SPECTRAL DISPERSION — 5-tap wavelength sampling
// ============================================================================

vec3 spectralSample(vec2 baseUV, vec2 normal, float maxOffset) {
    // 5 wavelength taps from red (outer) to blue (inner)
    const int TAPS = 5;
    const float offsets[5] = float[5](-1.0, -0.5, 0.0, 0.5, 1.0);
    // Spectral weights: approximate visible spectrum RGB per tap
    const vec3 weights[5] = vec3[5](
        vec3(1.0, 0.0, 0.0),   // red
        vec3(0.5, 0.5, 0.0),   // yellow
        vec3(0.0, 1.0, 0.0),   // green
        vec3(0.0, 0.5, 0.5),   // cyan
        vec3(0.0, 0.0, 1.0)    // blue
    );

    vec3 result = vec3(0.0);
    vec3 totalWeight = vec3(0.0);

    for (int i = 0; i < TAPS; i++) {
        vec2 offset = normal * offsets[i] * maxOffset / fullSize;
        vec3 s = sampleTex(baseUV + offset).rgb;
        result += s * weights[i];
        totalWeight += weights[i];
    }

    return result / totalWeight;
}

// ============================================================================
// MAIN
// ============================================================================

void main() {
    vec2 uv = v_texcoord;

    float cornerSdf = getCornerSDF(uv);

    // Outer glow / drop shadow (rendered in the padding region outside the glass)
    if (cornerSdf > 0.0 && shadowStrength > 0.001) {
        float glowRadius = edgeThickness * min(fullSize.x, fullSize.y) * 1.5;
        float glow = exp(-cornerSdf * cornerSdf / (glowRadius * glowRadius * 0.5));
        float shadowAlpha = glow * shadowStrength * 0.6;
        fragColor = vec4(0.0, 0.0, 0.0, shadowAlpha);
        return;
    }

    // Rounded corner mask
    float cornerAlpha = 1.0 - smoothstep(-1.5, 0.5, cornerSdf);
    if (cornerAlpha < 0.001) discard;

    // ========================================
    // EDGE PROXIMITY — exponential falloff, no hard boundary
    // ========================================
    float bezelSdf = getBezelSDF(uv);
    float bezelWidthPx = edgeThickness * min(fullSize.x, fullSize.y);
    // 1.0 at outer edge, ~0.37 at 1 bezel width in, ~0.05 at 3 widths in — no cutoff
    float edgeProximity = exp(bezelSdf / bezelWidthPx);

    vec2 normal = vec2(0.0);
    if (edgeProximity > 0.01) {
        normal = sdfNormal(uv);
    }

    // ========================================
    // Layer 1: FULL-SURFACE LENS DISTORTION
    // ========================================
    vec2 dGrad = domeGradient(uv);
    float minDim = min(fullSize.x, fullSize.y);
    float lensMaxPx = lensDistortion * minDim * 0.005;
    vec2 lensDisplacement = -dGrad * lensMaxPx / fullSize;

    // ========================================
    // Layer 2: BEZEL REFRACTION
    // ========================================
    vec2 bezelDisplacement = vec2(0.0);
    if (edgeProximity > 0.01) {
        float maxPx = refractionStrength * 40.0;
        bezelDisplacement = -normal * edgeProximity * edgeProximity * maxPx / fullSize;
    }

    vec2 sampleUV = uv + lensDisplacement + bezelDisplacement;

    // ========================================
    // Layer 3: BACKGROUND SAMPLE + SPECTRAL DISPERSION
    // ========================================
    vec3 color;
    if (edgeProximity > 0.05 && chromaticAberration > 0.001) {
        float caMaxPx = chromaticAberration * edgeProximity * 6.0;
        color = spectralSample(sampleUV, normal, caMaxPx);
    } else {
        color = sampleTex(sampleUV).rgb;
    }

    // ========================================
    // Layer 4: FROSTED TINT (brightness + desaturation)
    // ========================================
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luminance), color, backgroundSaturation);
    color *= backgroundBrightness;

    // ========================================
    // Layer 5: COLOR TINT OVERLAY
    // ========================================
    color = mix(color, tintColor, tintAlpha);

    // ========================================
    // Layer 6: FRESNEL RIM GLOW
    // ========================================
    if (fresnelStrength > 0.001) {
        float fresnel = edgeProximity * edgeProximity * fresnelStrength * 0.12;
        color += vec3(1.0) * fresnel;
    }

    // ========================================
    // Layer 7: SPECULAR — subtle top highlight
    // ========================================
    if (specularStrength > 0.001) {
        float topBias = pow(max(1.0 - uv.y, 0.0), 2.0);
        float spec = topBias * edgeProximity * edgeProximity * specularStrength * 0.08;
        color += vec3(1.0, 0.99, 0.97) * spec;
    }

    // ========================================
    // Layer 8: INNER SHADOW (bottom rim darkening)
    // ========================================
    {
        float bottomBias = pow(uv.y, 2.0);
        float shadow = bottomBias * edgeProximity * edgeProximity * 0.06;
        color *= 1.0 - shadow;
    }

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

    int samples = int(ceil(blurRadius));

    // Center tap
    float w0 = 1.0;
    vec4 result = texture(tex, v_texcoord) * w0;
    float totalWeight = w0;

    // Linear sampling: pair adjacent taps (i, i+1) into a single bilinear fetch.
    // The interpolated offset between two texels yields their weighted average
    // in one texture() call, halving the total tap count.
    for (int i = 1; i <= samples; i += 2) {
        float x1 = float(i);
        float x2 = float(i + 1);
        float w1 = exp(x1 * x1 * invSigma2);
        float w2 = (i + 1 <= samples) ? exp(x2 * x2 * invSigma2) : 0.0;
        float wSum = w1 + w2;
        if (wSum < 0.0001) continue;

        // Offset biased toward the heavier weight
        float offset = (x1 * w1 + x2 * w2) / wSum;

        result += texture(tex, v_texcoord + direction * offset) * wSum;
        result += texture(tex, v_texcoord - direction * offset) * wSum;
        totalWeight += 2.0 * wSum;
    }

    fragColor = result / totalWeight;
}
)GLSL"},
};
