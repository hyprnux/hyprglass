# HyprGlass - Liquid Glass inspired plugin for Hyprland

Liquid Glass for [Hyprland](https://hyprland.org/).

Frosted blur, edge refraction, chromatic aberration, specular highlights — fully customizable, per-theme, on every window and layer surface.

| Dark | Light |
|:---:|:---:|
| ![Dark theme](assets/dark-theme.png) | ![Light theme](assets/light-theme.png) |

## Installation

### hyprpm (recommended)

Builds against your exact Hyprland version, no ABI mismatch headaches:

```bash
hyprpm add https://github.com/hyprnux/hyprglass
hyprpm enable hyprglass
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

### Lua config

The plugin must be loaded before configuring it. Wrap everything in a guard:

```lua
if hl.plugin.hyprglass then
    local hg = hl.plugin.hyprglass

    hg.config({
        default_theme = "dark",
        default_preset = "clear",
        tint_color = 0x8899aa22,

        brightness = 0.9,
        dark = { brightness = 0.82 },
        light = { adaptive_boost = 0.5 },

        layers = { enabled = true },
    })

    -- Layer surfaces: each call whitelists the namespace and configures it
    hg.layer("waybar", { preset = "subtle", mask_threshold = 0.05 })
    hg.layer("swaync")
    hg.layer("quickshell:bezel", { preset = "ui", mask_threshold = 0.3 })
    hg.layer("debug-panel", { exclude = true })

    -- Presets
    hg.preset("clear", {
        glass_opacity = 0.8,
        blur_strength = 1.5,
        dark = { brightness = 0.7 },
        light = { brightness = 1.2 },
    })

    hg.preset("contrasted", {
        inherits = "high_contrast",
        contrast = 1.2,
        adaptive_dim = 1.5,
        dark = { tint_color = 0x02142aa9 },
    })
end
```

**Checking the config**

```sh
hyprctl configerrors                                # unknown options, with the file and line of the hg.config call
hyprctl getoption plugin:hyprglass:layers:enabled   # "set: true" once your value is applied
```

### Legacy .conf config

_Deprecated as of Hyprland 0.55, but still supported._

```ini
plugin:hyprglass {
    default_theme = dark
    default_preset = clear
    tint_color = 0x8899aa22

    brightness = 0.9
    dark:brightness = 0.82
    light:adaptive_boost = 0.5

    preset = name:clear, glass_opacity:0.8, blur_strength:1.5
    preset = name:clear:dark, brightness:0.7
    preset = name:clear:light, brightness:1.2

    preset = name:contrasted, inherits:high_contrast, contrast:1.2, adaptive_dim:1.5
    preset = name:contrasted:dark, tint_color:0x02142aa9

    layers {
        enabled = 1
        namespaces = waybar, swaync, quickshell:bezel
        exclude_namespaces = debug-panel
        preset = subtle
        namespace_presets = quickshell:bezel:ui
        namespace_mask_thresholds = waybar=0.05, quickshell:bezel=0.3
    }
}
```

### Global settings

| Option | Type | Default | Description |
|---|---|---|---|
| `enabled` | bool | `true` (`1` in .conf) | Enable/disable the effect globally. Per-window tags override this. |
| `manage_window_blur` | bool | `true` (`1` in .conf) | Automatically set the `noblur` property on glassed windows. Glass replaces Hyprland's blur; without `noblur`, Hyprland's cached-blur optimization (`blur:new_optimizations`) hides the glass on static windows. Set to `0` to manage `windowrule = noblur` yourself. |
| `skip_opaque_windows` | bool | `true` (`1` in .conf) | Skip glass under an opaque window — it would be invisible anyway, so skipping it saves GPU. Set to `0` to force glass everywhere. Windows using `self_sample` are never skipped, since their glass shows their own content. |
| `blur_fold` | bool | `true` (`1` in .conf) | Fewer blur passes with an identical look. Set to `0` to always run `blur_iterations` passes at the configured radius. |
| `default_theme` | string | `dark` | Default theme: `dark` or `light` |
| `default_preset` | string | `default` | Default preset name |

### Overridable settings

Set globally, per theme (`dark:` / `light:` prefix in .conf, or `dark = {}` / `light = {}` table in Lua), or in a preset.

Settings resolve through: **preset chain** (theme variant, shared, inherited) then **theme override** then **global value** then **hardcoded default**.

| Option | Type | Global Default | Dark Default | Light Default | Description |
|---|---|---|---|---|---|
| `blur_strength` | float | `2.0` | — | — | Blur radius scale (`value * 12.0` px) |
| `blur_iterations` | int | `3` | — | — | Gaussian blur passes (1-5) |
| `refraction_strength` | float | `0.6` | — | — | Edge refraction intensity (0.0-1.0) |
| `refraction_flow` | float | `0.0` | — | — | Where the edge distortion pulls: 0 toward the window center, 1 along the edges (0.0-1.0) |
| `refraction_spread` | float | `1.0` | — | — | How deep the distortion reaches: 1 across the whole window, 0 only a rim with a flat center (0.0-1.0) |
| `chromatic_aberration` | float | `0.5` | — | — | Spectral dispersion at edges (0.0-1.0) |
| `fresnel_strength` | float | `0.6` | — | — | Edge glow intensity (0.0-1.0) |
| `fresnel_tint` | float | `0.0` | — | — | Color of the fresnel rim light: 0 white, 1 the colors behind the glass (0.0-1.0) |
| `fresnel_color` | color | `0xffffff00` | — | — | Color of the fresnel rim light; alpha sets how much it replaces white (0xRRGGBBAA) |
| `specular_strength` | float | `0.8` | — | — | Specular highlight brightness (0.0-1.0) |
| `specular_angle` | float | `0.0` | — | — | Where the specular highlight comes from, in degrees: 0 from the top, 90 from the right, clockwise |
| `bevel_strength` | float | `0.0` | — | — | Thin lit line along the glass edge, a nicer border (0.0-1.0) |
| `bevel_size` | float | `6.0` | — | — | Width of the bevel line in logical pixels, uniform across monitor scales |
| `bevel_color` | color | `0xffffff00` | — | — | Color of the bevel line; alpha sets how much it replaces white, dark colors give a dark line (0xRRGGBBAA) |
| `bevel_tint` | float | `0.0` | — | — | Tint of the bevel line: 0 its own color, 1 the colors behind the glass (0.0-1.0) |
| `bevel_angle` | float | `315.0` | — | — | Where the bevel light comes from, in degrees: 0 from the top, 90 from the right, clockwise |
| `bevel_shadow` | float | `0.0` | — | — | Darkening of the bevel line on the side away from the light (0.0-1.0) |
| `glass_opacity` | float | `1.0` | — | — | Overall glass opacity (0.0-1.0) |
| `edge_thickness` | float | `0.06` | — | — | Bezel width, fraction of smallest dimension (0.0-0.15) |
| `tint_color` | color | `0x8899aa22` | — | — | Glass tint RRGGBBAA hex. Alpha = tint strength |
| `lens_distortion` | float | `0.5` | — | — | Center dome magnification (0.0-1.0) |
| `self_sample` | float | `0.0` | — | — | Mixes the window's own content into the glass behind it (0.0-1.0). Windows only |
| `brightness` | float | — | `0.82` | `1.12` | Brightness multiplier |
| `contrast` | float | — | `0.90` | `0.92` | Contrast around midpoint |
| `saturation` | float | — | `0.80` | `0.85` | Desaturation (0 = grayscale, 1 = full) |
| `vibrancy` | float | — | `0.15` | `0.12` | Selective saturation boost |
| `vibrancy_darkness` | float | — | `0.0` | `0.0` | Vibrancy influence on dark areas (0-1) |
| `adaptive_dim` | float | — | `0.4` | `0.0` | Dims bright areas behind the glass (white is white 0 -to- 1 white becomes black) |
| `adaptive_boost` | float | — | `0.0` | `0.4` | Boosts dark areas behind the glass (black is black 0 -to- 1 black becomes white) |

`—` in Global Default = falls through to per-theme default. `—` in Dark/Light = inherits global value.

#### Self sampling

`self_sample` needs a translucent window: its content is composited over the sampled desktop.

- At `1.0` the pane shows the window's own pixels. Where the client draws translucent ones (terminal transparency), the desktop still shows through
- A window made see-through by `windowrule = opacity` draws opaque pixels, so its pane is entirely its own content
- Raise `blur_strength` with it, or the window's own text stays readable in its glass
- A window showing the screen (screen-share preview, OBS) sees a one-frame-old copy of itself
- Transformed monitors (rotated or flipped) and layer surfaces ignore it

**Cost:** a self-sampling window re-blurs its whole pane whenever its content changes, so busy windows cost more than static ones.

**On the fly:**
```bash
hyprctl keyword plugin:hyprglass:self_sample 1.0
```

**Lua:**
```lua
hg.config({ self_sample = 0.6 })
hg.preset("aura", { inherits = "glass", self_sample = 1.0, blur_strength = 2.5 })
```

**Legacy .conf:**
```ini
self_sample = 0.6
preset = name:aura, inherits:glass, self_sample:1.0, blur_strength:2.5
```

### Layer surfaces

The glass effect can be applied to layer surfaces (bars, docks, widgets). **Disabled by default.**

Where the glass goes on a layer:
- Apps that request blur through the `ext-background-effect-v1` Wayland protocol get glass exactly where they ask for it
- Other layers get glass wherever their content is visible (alpha above `mask_threshold`), so partially transparent content (down to ~0.004 opacity) **triggers** the glass effect

`mask_mode` forces one behaviour: `auto` (default), `region` (only where the app requests blur; other layers get no glass) or `alpha` (visible content only).

**Caveat:** Layer shadows count as visible content. Use `mask_threshold` to set an alpha cutoff higher than your shadow opacity.

#### Lua config

```lua
hg.config({ layers = { enabled = true } })

-- Each call whitelists the namespace and optionally configures it
hg.layer("waybar", { preset = "subtle", mask_threshold = 0.05, live_resample = false })
hg.layer("swaync")
hg.layer("quickshell:bezel", { preset = "ui", mask_threshold = 0.3 })
hg.layer("quickshell:bar", { mask_mode = "region" })
hg.layer("debug-panel", { exclude = true })
```

| Field | Type | Description |
|---|---|---|
| `preset` | string | Preset override for this layer |
| `mask_threshold` | float | Alpha threshold (pixels below this are not glassed). Default `0.001` |
| `live_resample` | bool | Per-layer override of `layers:live_resample` |
| `mask_mode` | string | `"auto"`, `"region"` or `"alpha"`. See `layers:mask_mode` |
| `exclude` | bool | Blacklist this namespace instead of whitelisting it |

#### Legacy .conf config

| Option | Type | Default | Description |
|---|---|---|---|
| `layers:enabled` | bool | `false` (`0` in .conf) | Enable glass on layer surfaces |
| `layers:namespaces` | string | `""` | Comma-separated namespace whitelist. Empty = all layers |
| `layers:exclude_namespaces` | string | `""` | Comma-separated namespace blacklist (priority over whitelist) |
| `layers:preset` | string | `""` | Preset override for all layers |
| `layers:namespace_presets` | string | `""` | Per-namespace preset (`ns:preset` pairs, comma-separated) |
| `layers:namespace_mask_thresholds` | string | `""` | Per-namespace alpha threshold (`ns=value` pairs, comma-separated) |
| `layers:namespace_live_resample` | string | `""` | Per-namespace live resample override (`ns=0/1` pairs, comma-separated) |
| `layers:live_resample` | bool | `true` (`1` in .conf) | Re-render layer glass when content behind it changes (e.g. a playing video). GPU cost scales with background activity; static scenes stay free. Overridable per layer |
| `layers:live_resample_fps` | int | `30` | Max re-renders per second per layer for live resample. `0` = uncapped |
| `layers:force_live_resample` | bool | `false` (`0` in .conf) | Experimental: re-render layer glass every frame regardless of changes, ignoring `live_resample_fps`. Heavy GPU/battery cost |
| `layers:mask_mode` | string | `auto` | Where the glass goes: `auto` = where the app requests blur, else where content is visible; `region` = only where the app requests blur; `alpha` = only where content is visible |
| `layers:namespace_mask_modes` | string | `""` | Per-namespace `mask_mode` (`ns=mode` pairs, comma-separated) |
| `layers:manage_blur` | bool | `true` (`1` in .conf) | Replace Hyprland's own blur with glass on glassed layers (`layerrule = ignorealpha` then has no effect, use `mask_threshold`). Set to `0` to keep Hyprland's blur |

> Layer support hooks into Hyprland's internal render pipeline. This is version-sensitive and may break across Hyprland updates.

### Window background cache

Windows cache their sampled, blurred background and only re-sample it when something actually changed behind the window (it moved/resized, the window behind it changed, or the cache was just allocated) — the same idea as the layer `live_resample` cache above, always on.

| Option | Type | Default | Description |
|---|---|---|---|
| `windows:background_cache` | bool | `true` (`1` in .conf) | Reuse a window's last sampled+blurred background instead of re-sampling it every frame. Set to `0` to always re-sample (pre-cache behavior). |
| `windows:live_resample` | bool | `true` (`1` in .conf) | Re-render window glass when content behind it changes (e.g. a playing video, another window). GPU cost scales with background activity; static scenes stay free |
| `windows:live_resample_fps` | int | `30` | Max background-dirty marks per second for windows. `0` = uncapped |

> `hyprctl hyprglass stats` reports `win_hit`/`win_miss`/`win_defer`/`win_disc` per monitor to watch the cache in action.

### Per-window overrides

Control the effect, theme, and preset per window via tags.

#### Enable / disable

Override the global `enabled` setting per window via tags:

- `hyprglass_disabled` — force the effect off on this window (wins over `hyprglass_enabled` if both present).
- `hyprglass_enabled` — force the effect on this window. Useful with global `enabled = false` for a whitelist.

#### Theme

Each window's theme is resolved as:
1. **Window tag** `hyprglass_theme_light` or `hyprglass_theme_dark`
2. **Fallback** to `default_theme`

#### Preset

Assign via window rules:
- `hyprglass_preset_<name>` — override `default_preset` for this window

#### Examples

**Lua:**
```lua
hl.window_rule({ match = { class = "mpv" },       tag = "+hyprglass_disabled" })
hl.window_rule({ match = { fullscreen = true },    tag = "+hyprglass_disabled" })
hl.window_rule({ match = { class = "firefox" },    tag = "+hyprglass_theme_light" })
hl.window_rule({ match = { class = "myterminal" }, tag = "+hyprglass_preset_high_contrast" })
```

**Legacy .conf:**
```ini
windowrule = tag +hyprglass_disabled, class:mpv
windowrule = tag +hyprglass_disabled, fullscreen:1
windowrule = tag +hyprglass_theme_light, class:firefox
windowrule = tag +hyprglass_preset_high_contrast, class:myterminal
```

**On the fly:**
```bash
hyprctl dispatch tagwindow +hyprglass_disabled
hyprctl dispatch tagwindow +hyprglass_theme_dark
hyprctl dispatch tagwindow +hyprglass_preset_subtle
```

### Presets

Presets are named config overrides. They can be **built-in** or **user-defined**. User presets with the same name override built-in ones.

Each preset can have shared values (theme-agnostic), a dark variant, a light variant, and can inherit from another preset.

#### Built-in presets (Open to PR)

Always available. Activate via `default_preset` or per-window tags.

| Preset | Description |
|---|---|
| `high_contrast` | Punchy colors, strong tinting, good contrast between dark and light themes. Lower blur, stronger refraction. |
| `subtle` | Minimal glass effect. Light blur, reduced refraction and highlights. |
| `clear` | Minimal transparent effect. Like a transparent rounded border glass plate. |
| `glass` | Solid glass block effect with a lot of chromatic aberration. |
| `pomme` | Apple look-alike liquid glass, keep in mind it's an approximation, and apple does not apply it on big window with square corners on purpose (open to PR). |

**Note:** These presets are starting points. Submit improvements or your own presets through issues or PRs (with screenshots).

#### User-defined presets

**Lua (table syntax):**
```lua
hg.preset("clear", {
    glass_opacity = 0.8,
    blur_strength = 1.5,
    inherits = "subtle",
    dark = { brightness = 0.7 },
    light = { brightness = 1.2 },
})
```

**Lua (string syntax, backward compat):**
```lua
hg.preset("name:clear, glass_opacity:0.8, blur_strength:1.5")
hg.preset("name:clear:dark, brightness:0.7")
```

**Legacy .conf:**
```ini
preset = name:clear, glass_opacity:0.8, blur_strength:1.5
preset = name:clear:dark, brightness:0.7
preset = name:clear:light, brightness:1.2
preset = name:contrasted, inherits:high_contrast, contrast:1.2
```

*Tip: increase the last two hex digits of `tint_color` for more tint opacity.*

## How It Works

The window/layer is modeled as a **thick convex glass slab**. The rendering pipeline per window:

1. **Background sampling** — The framebuffer behind the window is captured with padding (content beyond the window boundary is included). `self_sample` mixes the window's own content into this capture.
2. **Gaussian blur** — Multi-pass two-pass (horizontal + vertical) Gaussian blur for the frosted look.
3. **Glass height field** — An SDF-based height profile: 1.0 deep inside the window, smooth S-curve to 0.0 at the edge. The transition width is `edge_thickness`.
4. **Edge refraction** — The height field gradient drives UV displacement. At the center the gradient is near-zero (no distortion). At the edges the gradient is steep, pushing sample UVs outward — pulling in content from beyond the window boundary. This creates natural color bleeding.
5. **Chromatic aberration** — R, G, B channels are sampled with slightly different refraction scales (blue bends more), creating spectral fringing at edges.
6. **Center dome lens** — Subtle barrel magnification in the flat interior, controlled by `lens_distortion`.
7. **Frosted tint** — Per-theme tone mapping: adaptive luminance-dependent brightness, contrast, desaturation, and vibrancy applied to the blurred background.
8. **Color tint overlay** — Configurable color tint.
9. **Fresnel edge glow** — Schlick-based fresnel approximation at the glass edge.
10. **Specular highlight + inner shadow** — Top-biased highlight and bottom-rim shadow for depth.

For windows, the plugin integrates with Hyprland's render pass system as a `DECORATION_LAYER_BOTTOM` decoration, drawing before the window surface so the glass shows through transparent windows. For layer surfaces, the plugin hooks `renderLayer` and uses a temp FBO redirect: the background is sampled and blurred, then Hyprland's surface rendering is redirected into a transparent temporary framebuffer to capture the surface's exact alpha. A post-surface pass then composites the glass effect (masked to visible content or to the app's requested blur region) and the surface content back onto the main framebuffer in a single shader pass.

## Unloading

```bash
hyprctl plugin unload /path/to/hyprglass.so
```

## Performance diagnostics

| Option | Type | Default | Description |
|---|---|---|---|
| `debug:mode` | string | `off` | `off`, `hints_only` (render pass hints only, no GL work — isolates render-pass cost), or `gl_work_only` (runs the GL pipeline but drops the live-blur hint — isolates pipeline cost from render-pass damage-expansion cost). For A/B GPU measurement; leave `off` for normal use. |
| `debug:timers` | bool | `false` (`0` in .conf) | Time each pipeline stage on the GPU (`GL_EXT_disjoint_timer_query`) and report per-stage averages in `hyprctl hyprglass stats`. No effect if the driver doesn't support the extension. |

```bash
hyprctl hyprglass stats          # per-monitor counters and (if enabled) stage timers
hyprctl hyprglass stats reset    # zero every counter and accumulated timer
hyprctl j/hyprglass stats        # same, as JSON
```

```
hyprglass stats
  stage timers: off (plugin:hyprglass:debug:timers = 0)

  monitor        frames  win_draws  opaque_skip  win_hit  win_miss  win_defer  win_disc  layer_draws  layer_hit  layer_miss  blur_pass  sampled_mpx  glass_mpx
  eDP-1            7212       3401         5122     3120       240         41         0         1560       1420          92       5520        41.30      18.77
  eDP-1          per frame: 0.47 win draws, 0.22 layer draws, 0.77 blur passes, 0.006 sampled mpx, 0.003 glass mpx
```

## Notes

- The plugin requires Hyprland shadows to be present in the render pipeline. It **auto-enables them** at load time if disabled — shadow visual values (range, color…) can be zero, only the decoration's presence matters.
- Glass **replaces Hyprland's blur** on glassed windows: the plugin sets the `noblur` window property on them so their translucency composites against the glass instead of Hyprland's blur (whose `new_optimizations` cache is captured before plugin decorations render, hiding the glass on static windows — the "effect only shows while dragging" symptom). Disable with `manage_window_blur = 0`. The property is withdrawn when glass is disabled for a window or the plugin unloads.
- Layer surface glass uses a function hook on `renderLayer`, which is a private Hyprland internal. The hook may break on Hyprland updates that change this function's signature.

## Troubleshooting

### "Version mismatch" on hyprland-git or a self-built Hyprland

The plugin compares its build-time Hyprland ABI signature against the running compositor. The comparison uses the dependency ABI suffix (`_aq_…_hu_…`), not the exact commit hash, so a plugin built against matching headers loads fine on git builds. If it still fails, the reported hashes (shown in the error notification) tell you which dependency versions differ — rebuild the plugin against the headers of the Hyprland you are actually running.

As a last resort, setting `HYPRGLASS_SKIP_VERSION_CHECK=1` downgrades the failure to a warning. The variable must be present in **Hyprland's own environment**: export it from your session manager (uwsm, greetd, …) or set it early in your Hyprland config via the `env` keyword. This is unsupported — a real ABI mismatch can crash Hyprland.

### Build fails inside Hyprland's own headers ("cannot convert 'PHLLS' … to 'bool' … explicit conversion function was not considered")

This happens when building against Hyprland **0.55.4 headers** with a hyprutils **newer than 0.13.1**: hyprutils made its smart-pointer `operator bool` explicit after 0.55.4 was released, and 0.55.4's headers still rely on the old implicit behavior. Every Hyprland plugin fails identically on such a system — it is not a hyprglass bug. Until the next Hyprland release, either downgrade/pin hyprutils to 0.13.1, or run hyprland-git (fixed upstream) and rebuild the plugin against its headers.

## License

See repository for license details.
