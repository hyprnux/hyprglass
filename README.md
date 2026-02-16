# HyprGlass – Liquid Glass inspired plugin for Hyprland

Liquid Glass for [Hyprland](https://hyprland.org/).

Frosted blur, edge refraction, chromatic aberration, specular highlights — fully customizable, per-theme, on every window.

| Dark | Light |
|:---:|:---:|
| ![Dark theme](assets/dark-theme.png) | ![Light theme](assets/light-theme.png) |

## Installation

### hyprpm (recommended)

Builds against your exact Hyprland version, no ABI mismatch headaches:

```bash
hyprpm add https://github.com/hyprnux/hyprglass
hyprpm enable HyprGlass
```

### Pre-built release

Grab `hyprglass.so` from [Releases](https://github.com/hyprnux/hyprglass/releases/latest). Each release targets a specific Hyprland API version — check the release notes to confirm it matches yours.

```bash
hyprctl plugin load /path/to/hyprglass.so
```

Or persist it in your config:

```ini
plugin = /path/to/hyprglass.so
```

### Manual build

```bash
make
hyprctl plugin load $(pwd)/hyprglass.so
```

## Configuration

Everything goes under `plugin:hyprglass:` in your Hyprland config.

### Layered config

Settings resolve in three tiers:

1. **Theme override** (`dark:` / `light:` prefix) — highest priority
2. **Global value** — applies to both themes
3. **Built-in default** — per-theme fallback

```ini
plugin:hyprglass {
    brightness = 0.9              # both themes
    dark:brightness = 0.82        # dark only override
    light:adaptive_correction = 0.5
}
```

### Global-only settings

| Option | Type | Default | Description |
|---|---|---|---|
| `enabled` | int | `1` | Enable/disable the effect (0 or 1) |
| `default_theme` | int | `0` | Default theme: 0 = dark, 1 = light |

### Overridable settings

Set globally, or per theme with `dark:` / `light:` prefix.

| Option | Type | Global Default | Dark Default | Light Default | Description |
|---|---|---|---|---|---|
| `blur_strength` | float | `2.0` | — | — | Blur radius scale (`value × 12.0` px) |
| `blur_iterations` | int | `3` | — | — | Gaussian blur passes (1–5) |
| `refraction_strength` | float | `0.6` | — | — | Edge refraction intensity (0.0–1.0) |
| `chromatic_aberration` | float | `0.5` | — | — | Spectral dispersion at edges (0.0–1.0) |
| `fresnel_strength` | float | `0.6` | — | — | Edge glow intensity (0.0–1.0) |
| `specular_strength` | float | `0.8` | — | — | Specular highlight brightness (0.0–1.0) |
| `glass_opacity` | float | `1.0` | — | — | Overall glass opacity (0.0–1.0) |
| `edge_thickness` | float | `0.06` | — | — | Bezel width, fraction of smallest dimension (0.0–0.15) |
| `tint_color` | color | `0x8899aa22` | — | — | Glass tint RRGGBBAA hex. Alpha = tint strength |
| `lens_distortion` | float | `0.5` | — | — | Center dome magnification (0.0–1.0) |
| `brightness` | float | — | `0.82` | `1.12` | Brightness multiplier |
| `contrast` | float | — | `0.90` | `0.92` | Contrast around midpoint |
| `saturation` | float | — | `0.80` | `0.85` | Desaturation (0 = grayscale, 1 = full) |
| `vibrancy` | float | — | `0.15` | `0.12` | Selective saturation boost |
| `vibrancy_darkness` | float | — | `0.0` | `0.0` | Vibrancy influence on dark areas (0–1) |
| `adaptive_correction` | float | — | `0.4` | `0.4` | Luminance correction (0–1). Dark: dims bright areas. Light: boosts dark areas |

`—` in Global Default = falls through to per-theme default. `—` in Dark/Light = inherits global value.

### Theme detection

Each window's theme is resolved as:
1. **Window tag** `hyprnux_theme_light` or `hyprnux_theme_dark`
2. **Fallback** to `default_theme`

Set via window rules:
```ini
windowrule = tag +hyprnux_theme_light, match:class firefox
```

Or on the fly:
```bash
hyprctl dispatch tagwindow +hyprnux_theme_dark
```

### Presets

**High contrast** — punchy colors, strong tinting, good contrast between dark and light themes:

```ini
plugin:hyprglass {
    enabled = 1
    blur_strength = 1.4
    blur_iterations = 2
    lens_distortion = 0.5
    refraction_strength = 1.2
    chromatic_aberration = 0.25
    fresnel_strength = 0.3
    specular_strength = 0.8
    glass_opacity = 1
    edge_thickness = 0.06
    default_theme = 0

    dark:brightness = 0.92
    dark:contrast = 1.14
    dark:saturation = 0.92
    dark:vibrancy = 0.5
    dark:vibrancy_darkness = 0.2
    dark:adaptive_correction = 0.15
    dark:tint_color = 0x021d3c66

    light:brightness = 1.03
    light:contrast = 0.92
    light:saturation = 0.8
    light:vibrancy = 0.12
    light:vibrancy_darkness = 5.0
    light:adaptive_correction = 0.35
    light:tint_color = 0xc2cddb33
}
```

*Increase two last digits of tint colors for more opacity of teint color and more contrast with background*


**Defaults** — softer frosted look, lighter blur:

```ini
plugin:hyprglass {
    enabled = 1
    default_theme = 0

    blur_strength = 2.0
    blur_iterations = 3
    refraction_strength = 0.6
    chromatic_aberration = 0.5
    fresnel_strength = 0.6
    specular_strength = 0.8
    glass_opacity = 1.0
    edge_thickness = 0.06
    tint_color = 0x8899aa22
    lens_distortion = 0.5

    dark:brightness = 0.82
    dark:contrast = 0.90
    dark:saturation = 0.80
    dark:vibrancy = 0.15
    dark:adaptive_correction = 0.4

    light:brightness = 1.12
    light:contrast = 0.92
    light:saturation = 0.85
    light:vibrancy = 0.12
    light:adaptive_correction = 0.4
}
```

## How It Works

The window is modeled as a **thick convex glass slab**. The rendering pipeline per window:

1. **Background sampling** — The framebuffer behind the window is captured with padding (content beyond the window boundary is included).
2. **Gaussian blur** — Multi-pass two-pass (horizontal + vertical) Gaussian blur for the frosted look.
3. **Glass height field** — An SDF-based height profile: 1.0 deep inside the window, smooth S-curve to 0.0 at the edge. The transition width is `edge_thickness`.
4. **Edge refraction** — The height field gradient drives UV displacement. At the center the gradient is near-zero (no distortion). At the edges the gradient is steep, pushing sample UVs outward — pulling in content from beyond the window boundary. This creates natural color bleeding.
5. **Chromatic aberration** — R, G, B channels are sampled with slightly different refraction scales (blue bends more), creating spectral fringing at edges.
6. **Center dome lens** — Subtle barrel magnification in the flat interior, controlled by `lens_distortion`.
7. **Frosted tint** — Per-theme tone mapping: adaptive luminance-dependent brightness, contrast, desaturation, and vibrancy applied to the blurred background.
8. **Color tint overlay** — Configurable color tint.
9. **Fresnel edge glow** — Schlick-based fresnel approximation at the glass edge.
10. **Specular highlight + inner shadow** — Top-biased highlight and bottom-rim shadow for depth.

The plugin integrates with Hyprland's render pass system as a `DECORATION_LAYER_BOTTOM` decoration, drawing before the window surface so the glass shows through transparent windows.

## Unloading

```bash
hyprctl plugin unload /path/to/hyprglass.so
```

## Notes

- The plugin requires Hyprland shadows to be present in the render pipeline. It **auto-enables them** at load time if disabled — shadow visual values (range, color…) can be zero, only the decoration's presence matters.

## License

See repository for license details.
