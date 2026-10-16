# NVR post-processing pipeline (audit, order, 2026-10-16)

## How the frame flows

`Hooks/Render.cpp` (`ProcessImageSpaceShaders` hook) runs three stages each frame:

1. **`ShaderManager::RenderEffectsPreTonemapping(GameSurface)`**, on the game's scene target before its
   image-space chain (with `RenderPreTonemapping = true`, the user's setting).
2. **The game's image-space chain**, where the HDR blend shaders `ISHDRBLENDINSHADERCIN(AM).pso` are
   NVR's replacements (`TonemappingShaders`). This is the only tone curve in the frame.
3. **`ShaderManager::RenderEffects(OutputSurface)`**, on the tonemapped LDR image, before the HUD.

Intermediate copies (`TESR_SourceBuffer`, `TESR_RenderedBuffer`) are `A16B16G16R16F`.

## Scene colour: what the engine actually gives us

Taken from the decompile (renderer init, `FalloutNV.exe.c` around line 246985):
- `BSShaderManager::bHDR = bDoHighDynamicRange && bFloatBlend`. With HDR on the scene target is
  `D3DFMT_A16B16G16R16F` (it is the format MSAA is checked against).
- The scene is **gamma-encoded, extended range**. Lighting shaders output display-style values
  (NVR's `LinearLighting` decodes with x² and re-encodes with √ before fog), and FP16 keeps values above
  1: the sky (`fSkyBrightness`), emissives (`fEmissiveHDRMult`), the sun and specular highlights.
- So the HDR is genuine in the sense that highlights above white survive to the tone curve. It is not
  physically scaled light: the engine's "units" are display-referred values that just aren't clipped.
- The HDR blend shader linearises with `pow(x, Linearization)` (2.2) and re-encodes with
  `pow(x, 1/Gamma)`. Bloom's first downsample linearises with the sRGB curve. The difference above 1 is
  small and only affects how bright a highlight blooms, not the scene.
- The game's own eye adaptation lives in the vanilla bloom's alpha (`bloom.w`, used as
  `1 / max(bloom.w, HDRParam.x)`). NVR's bloom path never used it; the vanilla bloom path now skips it
  when auto exposure is on, so the frame never adapts twice.

## Order (as it runs) and why

| Stage | Effect | Space | Why here |
|---|---|---|---|
| pre | CombineDepth, Normals | - | inputs for everything below |
| pre | point / sun / contact shadows, skin scattering | scene (gamma) | must see the scene as drawn |
| pre | snow accumulation, **AO**, wet world | scene | lighting-like, before fog |
| pre | volumetric light / fog, god rays, flashlight beam | scene | atmosphere over lit surfaces |
| pre | **AutoExposure** measure (log average, centre weighted) | linear | before bloom and the curve |
| pre | **Bloom** pyramid (Karis 13-tap down, tent up, soft knee) | linear HDR | needs unclipped highlights |
| pre | Lens dirt (reads the bloom buffer) | linear | driven by highlights |
| HDR blend | vanilla tint/saturation → bloom add → **exposure × 2^EV × auto exposure** → **GradeScene** → **tone curve** → weather contrast → **GradeDisplay** → fade / night eye → encode → debug view | linear → display | one curve, grading split around it |
| post | **TAA** | LDR | LDR history cannot ghost like HDR highlights |
| post | rain, snow | LDR | particles, no depth to reproject |
| post | LUT (unless PreTonemapping), DOF, motion blur | LDR | on the stable resolved image |
| post | blood / water lens, SMAA, sharpening, Cinema (grain, CA, vignette, letterbox), ImageAdjust | display | fixed to the screen, last |

## What already existed (kept, not duplicated)

- 12 tone curves (`[Shaders.Tonemapping.*] TonemappingMode`): 0 none (for DXVK HDR output),
  VT/NVR Lottes, Reinhard, Reinhard-Jodie, ACES (Narkowicz and Hill fit), Uncharted 2 (Hable),
  Uchimura GT, AgX (with its CDL look and fixed matrix orientation), DICE, Khronos PBR Neutral.
- Auto exposure: log-average over a 24 × 16 grid, centre weighting, separate bright/dark adaptation
  times, min/max scale, per Main/Night/Interiors with the usual transitions (smooth door changes).
- Bloom, lens dirt, LUTs (pre or post curve), Cinematic DOF, camera motion blur, TAA, SMAA,
  sharpening, Cinema, ImageAdjust, the Debug effect, `GpuProfiler` timings.

## Added on 2026-10-16

All inside the existing HDR blend pass: no new render targets, no new full-screen passes.

- **`[Shaders.ColorGrading.Main/Night/Interiors]`**, blended like other per-time settings:
  - before the curve (linear scene light): `Temperature`/`Tint` (Unity's LMS white balance, normalised
    so white keeps its luminance), `Contrast` in stops around `ContrastPivot`, `Saturation`, `Vibrance`;
  - after the curve (display image): `Lift`/`Gamma`/`Gain` per channel, `HighlightSaturation`.
  - Neutral values switch it off on the CPU (`TESR_GradingControl.x = 0`); `Enabled` in Main.
- **`[Shaders.Tonemapping.*] ExposureEV`**: exposure compensation in stops, multiplied into Exposure.
- **`[Shaders.Tonemapping.Main] DebugView`**: 1 false-colour exposure, 2 clipping, 3 split screen
  (untonemapped left, tonemapped right), 4 bloom only.
- Constants c33-c40 in both HDR blend shaders (`TESR_Grading*`). Static size went from 626 to 782 slots;
  the new code sits behind uniform branches, so a neutral grade costs two branch instructions.

## Limitations and features not built

- **SSR**: no material G-buffer (no roughness, metalness or specular colour reaches a screen pass; see
  `nvr-lighting-work` notes). Reflections already come from sky SH and DynamicCubemaps. Screen-space
  reflections would reflect onto every surface equally and double-count the forward specular.
- **Motion blur** is camera-only (depth + previous camera). Per-object motion vectors were attempted and
  removed on 2026-10-15 (`saved-patches/motion-vectors-2026-10-15`).
- **TAA** is camera reprojection only, for the same reason. No DLSS/FSR2: they need per-object motion.
- **AO** is NVR's existing effect (its `Samples` key is unused, the sample count is hardcoded). A GTAO
  replacement is the next AO step, not done here.
- **DXVK HDR output** (`TonemappingMode 0`, `Gamma 1`): the display grade then works on linear values.
- None of the 2026-10-16 additions has been seen in game yet.

## Quality presets (suggested values)

These are toml values to paste over the live file with the game closed; NVR has no quality-preset switch.

**Performance**: `[_Shaders.Bloom.*] Passes = 5`, `[_Shaders.AmbientOcclusion.Status] Enabled = false`,
`[_Shaders.CinematicDOF.Status] Enabled = false`, `[_Shaders.MotionBlur.Status] Enabled = false`,
`[_Shaders.TAA.Status] Enabled = true`, `[_Shaders.SMAA.Status] Enabled = false`.

**Balanced**: `Passes = 7`, AO on, `[_Shaders.CinematicDOF.Main] BokehQuality = 0`, TAA on.

**Ultra**: `Passes = 8`, AO on, `BokehQuality = 2`, TAA on, `[_Shaders.Sharpening.Status] Enabled = true`
(light `Strength`, to recover TAA softness).

Colour grading, exposure and the tone curve cost the same in every preset.

## Cinematic grade (optional, not the default)

A starting point for a warm desert look in `[_Shaders.ColorGrading.Main]`:
`Temperature = 12`, `Contrast = 1.1`, `Vibrance = 0.15`, `HighlightSaturation = 0.85`,
`LiftB = 0.01`, `GainR = 1.02`. Nights: `Temperature = -10`, `Saturation = 0.9`.
