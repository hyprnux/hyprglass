# Liquid Glass Plugin for Hyprland

An Apple-style Liquid Glass effect plugin for [Hyprland](https://hyprland.org/). Models each window as a thick convex glass slab — content behind refracts naturally through the curved edges, creating color bleeding and chromatic fringing, while the flat center shows clean frosted blur.

## Requirements

- Hyprland (matching API version)
- Hyprland shadows must be enabled for the glass effect to work correctly. The plugin **auto-enables shadows** at load time if they are disabled. Shadow visual values (range, color, etc.) can be set to 0 — only the decoration's presence in the render pipeline is required.

## Building

```bash
make
```

The plugin compiles to `hyprnux-liquid.so`.

## Installation

```bash
hyprctl plugin load /path/to/hyprnux-liquid.so
```

Or add to your Hyprland config:

```ini
plugin = /path/to/hyprnux-liquid.so
```

For [hyprpm](https://wiki.hyprland.org/Plugins/Using-Plugins/#hyprpm) users, install via the `hyprpm.toml` manifest.

## Configuration

All options live under the `plugin:liquid-glass:` namespace in your Hyprland config.

| Option | Type | Default | Description |
|---|---|---|---|
| `enabled` | int | `1` | Enable/disable the effect (0 or 1) |
| `blur_strength` | float | `2.0` | Blur radius scale. Controls the frosted glass intensity. Applied as `value * 12.0` px radius. |
| `blur_iterations` | int | `4` | Number of Gaussian blur passes (1–5). Lower values improve performance at the cost of blur quality. |
| `lens_distortion` | float | `0.5` | Center dome lens magnification (0.0–1.0). Subtle barrel distortion in the flat interior of the glass. |
| `refraction_strength` | float | `0.6` | Edge refraction intensity (0.0–1.0). How strongly the curved glass edge pulls in content from beyond the window boundary. |
| `chromatic_aberration` | float | `0.5` | Spectral dispersion at edges (0.0–1.0). Blue refracts more than red, creating natural rainbow fringing. |
| `fresnel_strength` | float | `0.6` | Edge glow intensity (0.0–1.0). Schlick-based fresnel simulating light hitting the glass surface. |
| `specular_strength` | float | `0.8` | Specular highlight brightness (0.0–1.0). Top-biased highlight adding depth. |
| `glass_opacity` | float | `1.0` | Overall glass opacity (0.0–1.0). |
| `edge_thickness` | float | `0.06` | Glass bezel width as a fraction of the window's smallest dimension (0.0–0.15). Controls how wide the refraction zone is. |
| `tint_color` | color | `0x8899aa22` | Glass tint color in RRGGBBAA hex or `rgba(...)`. The alpha channel controls tint strength (0 = off). |
| `background_brightness` | float | `1.08` | Frosted glass brightness boost (0.5–2.0). Values > 1.0 brighten the blurred background. |
| `background_saturation` | float | `0.82` | Frosted glass desaturation (0.0–1.0). Values < 1.0 desaturate for a milky frosted look. |
| `environment_strength` | float | `0.12` | *(Deprecated, no-op)* Kept for config compatibility. |
| `shadow_strength` | float | `0.15` | *(Deprecated, no-op)* Kept for config compatibility. |
| `light_angle` | float | `225.0` | *(Deprecated, no-op)* Kept for config compatibility. |

### Example

```ini
plugin:liquid-glass {
    enabled = 1
    blur_strength = 2.0
    blur_iterations = 4
    lens_distortion = 0.5
    refraction_strength = 0.6
    chromatic_aberration = 0.5
    fresnel_strength = 0.6
    specular_strength = 0.8
    glass_opacity = 1.0
    edge_thickness = 0.06
    tint_color = rgba(88, 99, aa, 0.15)
    background_brightness = 1.08
    background_saturation = 0.82
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
7. **Frosted tint** — Brightness boost and desaturation applied to the blurred background.
8. **Color tint overlay** — Configurable color tint.
9. **Fresnel edge glow** — Schlick-based fresnel approximation at the glass edge.
10. **Specular highlight + inner shadow** — Top-biased highlight and bottom-rim shadow for depth.

The plugin integrates with Hyprland's render pass system as a `DECORATION_LAYER_BOTTOM` decoration, drawing before the window surface so the glass shows through transparent windows.

## Unloading

```bash
hyprctl plugin unload /path/to/hyprnux-liquid.so
```

## License

See repository for license details.
