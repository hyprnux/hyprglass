# HyprGlass — Liquid Glass Plugin for Hyprland

An Apple-style Liquid Glass effect plugin for [Hyprland](https://hyprland.org/). Models each window as a thick convex glass slab — content behind refracts naturally through the curved edges, creating color bleeding and chromatic fringing, while the flat center shows clean frosted blur.

## Requirements

- Hyprland (matching API version)
- Hyprland shadows must be enabled for the glass effect to work correctly. The plugin **auto-enables shadows** at load time if they are disabled. Shadow visual values (range, color, etc.) can be set to 0 — only the decoration's presence in the render pipeline is required.

## Building

```bash
make
```

The plugin compiles to `hyprglass.so`.

## Installation

```bash
hyprctl plugin load /path/to/hyprglass.so
```

Or add to your Hyprland config:

```ini
plugin = /path/to/hyprglass.so
```

For [hyprpm](https://wiki.hyprland.org/Plugins/Using-Plugins/#hyprpm) users, install via the `hyprpm.toml` manifest.

## Configuration

All options live under the `plugin:hyprglass:` namespace in your Hyprland config.

| Option | Type | Default | Description |
|---|---|---|---|
| `enabled` | int | `1` | Enable/disable the effect (0 or 1) |
| `blur_strength` | float | `2.0` | Blur radius scale. Controls the frosted glass intensity. Applied as `value * 12.0` px radius. |
| `blur_iterations` | int | `3` | Number of Gaussian blur passes (1–5). Lower values improve performance at the cost of blur quality. |
| `lens_distortion` | float | `0.5` | Center dome lens magnification (0.0–1.0). Subtle barrel distortion in the flat interior of the glass. |
| `refraction_strength` | float | `0.6` | Edge refraction intensity (0.0–1.0). How strongly the curved glass edge pulls in content from beyond the window boundary. |
| `chromatic_aberration` | float | `0.5` | Spectral dispersion at edges (0.0–1.0). Blue refracts more than red, creating natural rainbow fringing. |
| `fresnel_strength` | float | `0.6` | Edge glow intensity (0.0–1.0). Schlick-based fresnel simulating light hitting the glass surface. |
| `specular_strength` | float | `0.8` | Specular highlight brightness (0.0–1.0). Top-biased highlight adding depth. |
| `glass_opacity` | float | `1.0` | Overall glass opacity (0.0–1.0). |
| `edge_thickness` | float | `0.06` | Glass bezel width as a fraction of the window's smallest dimension (0.0–0.15). Controls how wide the refraction zone is. |
| `tint_color` | color | `0x8899aa22` | Glass tint color in RRGGBBAA hex or `rgba(...)`. The alpha channel controls tint strength (0 = off). |
| `default_theme` | int | `0` | System default theme: 0 = dark, 1 = light. Used when a window has no theme tag. |

### Dark Theme Settings (`dark:*`)

Applied when the window's resolved theme is dark.

| Option | Type | Default | Description |
|---|---|---|---|
| `dark:brightness` | float | `0.82` | Brightness multiplier. Below 1.0 for a softer frosted look. |
| `dark:contrast` | float | `0.90` | Contrast around midpoint. Below 1.0 softens harsh edges. |
| `dark:saturation` | float | `0.80` | Frosted desaturation (0 = grayscale, 1 = full color). |
| `dark:vibrancy` | float | `0.15` | Selective saturation boost scaled by existing saturation. |
| `dark:vibrancy_darkness` | float | `0.0` | How much vibrancy affects dark background areas (0–1). |
| `dark:adaptive_dim` | float | `0.4` | Per-pixel dimming of bright background areas (0–1). Improves text readability on dark-themed windows over bright content. |

### Light Theme Settings (`light:*`)

Applied when the window's resolved theme is light.

| Option | Type | Default | Description |
|---|---|---|---|
| `light:brightness` | float | `1.12` | Brightness multiplier. Above 1.0 to lift the frosted background. |
| `light:contrast` | float | `0.92` | Contrast around midpoint. |
| `light:saturation` | float | `0.85` | Frosted desaturation (0 = grayscale, 1 = full color). |
| `light:vibrancy` | float | `0.12` | Selective saturation boost scaled by existing saturation. |
| `light:vibrancy_darkness` | float | `0.0` | How much vibrancy affects dark background areas (0–1). |
| `light:adaptive_boost` | float | `0.4` | Per-pixel brightening of dark background areas (0–1). Improves text readability on light-themed windows over dark content. |

### Theme Detection

The plugin resolves each window's theme in this order:
1. **Window tag** — If the window has the tag `hyprnux_theme_light`, light theme is used. If `hyprnux_theme_dark`, dark theme.
2. **Fallback** — The `default_theme` config value (0 = dark, 1 = light).

Set tags via window rules:
```ini
windowrule = tag +hyprnux_theme_light, match:class firefox
```

Or dynamically:
```bash
hyprctl dispatch tagwindow +hyprnux_theme_dark
```

### Example

```ini
plugin:hyprglass {
    enabled = 1
    blur_strength = 2.0
    blur_iterations = 3
    lens_distortion = 0.5
    refraction_strength = 0.6
    chromatic_aberration = 0.5
    fresnel_strength = 0.6
    specular_strength = 0.8
    glass_opacity = 1.0
    edge_thickness = 0.06
    tint_color = 0x8899aa22
    default_theme = 0

    dark:brightness = 0.82
    dark:contrast = 0.90
    dark:saturation = 0.80
    dark:vibrancy = 0.15
    dark:vibrancy_darkness = 0.0
    dark:adaptive_dim = 0.4

    light:brightness = 1.12
    light:contrast = 0.92
    light:saturation = 0.85
    light:vibrancy = 0.12
    light:vibrancy_darkness = 0.0
    light:adaptive_boost = 0.4
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

## License

See repository for license details.
