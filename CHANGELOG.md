
## v0.1.0 - 2026-02-16

### Bug Fixes

* area was not damaged correctly  when moving while tiled layout
* remove some optimization causing noise when rendering blur and moving window (not worth it)
* gpu infinite render
* border radius
* working with shadow enable, not yet with shadow disabled

### CI/CD

* pipelines + docs

### Code Refactoring

* better file separation, renaming
* remove unused code and fix bounding box
* attempt with glass shader using SDF bezel refraction and poisson blur

### Features

* improve settings
* almost perfect
* more like apple variant, magnifying variant, seemd to be more beautiful
* add meniscus dispersion, color, and border rim to glasss shader (attempt to make it more apple-like, cool effects but not a big usable success)"
* tweaking little bit different approach
* add color_tint effect
* split corner/bezel SDF, add multi-pass blur and inner shadow (in order to avoid weird corners, but not really a success yet)
* add UV padding and remove wave distorsion
* draft working version of shader for transparent windows

### Performance Improvements

* massive gpu usage improvments, resample only when really needed
* shared blur framebuffers, remove raw texture sampler
* half-res blur pipeline, linear sampling, shadows

