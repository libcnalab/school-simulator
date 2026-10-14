# Renderer testing

The application never names a renderer: it uses `GraphicsCapability` queries, BasicEffect, vertex colours,
static and dynamic buffers, 16-bit indices, SpriteBatch/SpriteFont, RenderTarget2D with a depth buffer, and
the standard blend, depth, rasterizer and sampler states. CNA picks the renderer (`CNA_GRAPHICS_RENDERER`
at run time among those built in; the build's default is the first of `SCHOOL_RENDERERS`).

## Recipe

```bash
SCHOOL_RENDERERS="OPENGLES3;OPENGL33;SOFTWARE" RECONFIGURE=1 tools/build.sh
tools/tour.sh -r OPENGLES3      # 17 places, build/shots/*.png, exit 1 on a draw error
tools/tour.sh -r OPENGL33 -o build/shots-gl33
tools/tour.sh -r SOFTWARE -o build/shots-software     # slow: a few minutes
```

Deterministic scenes for comparison: every tour stop starts the simulation at a fixed time from seed 1 and
waits a fixed number of frames; with `--fixed-step 0.016` the frames are deterministic too. Compare the
pictures of two renderers by eye or with an image diff; the HUD can be hidden with `--no-hud`.

## Results

| Date | Renderer | Machine | Result |
|---|---|---|---|
| 2026-10-14 | OPENGLES3 (EasyGL, Mesa llvmpipe 25.0 under Xvfb) | build host, headless (CPU rasteriser) | tour: 17/17 ok, no draw errors, 10-25 fps in software |
| 2026-10-14 | OPENGL33 (EasyGL, same Mesa) | build host | tour: 17/17 ok, no draw errors; pictures match OPENGLES3 pixel for pixel in the scenes compared (classroom, cafeteria, stairs) |
| 2026-10-14 | SOFTWARE (CNA's CPU rasteriser) | build host | tour: 17/17 ok with `SCHOOL_TIMEOUT=1500`, no draw errors; same geometry, lighting, render targets and text as the GL renderers; only the room banner's fade differs (frame timing) |
| 2026-10-14 | OPENGLES3 | build host | `tools/determinism.sh`: two fixed-step runs give the same digest and byte-identical screenshots |

| 2026-10-14 | FNA3D | build host, multi-renderer tree `build-probe/` | tour: 17/17 ok, no draw errors; pixel difference against OPENGLES3 (pixels off by more than 24/255): 0.1-3 % in 14 scenes, 13 % corridor, 16 % stairs |
| 2026-10-14 | WEBGPU (Vulkan, lavapipe) | same | tour: 17/17 ok, no draw errors; 0.1-3.8 % in 14 scenes, 11 % corridor and stairs |
| 2026-10-14 | SDL_GPU (Vulkan, lavapipe) | same | tour: 17/17 ok, no draw errors; 0.1-5 % in 14 scenes, 24 % corridor, 20 % stairs |

The large differences are in the two crowded scenes (corridor, stairs): the tour is not run with `--fixed-step`, so
people stand in different places from run to run, and those scenes are mostly people. This was not investigated
further; the other scenes differ by the HUD clock, people in motion and the room banner's fade. The three GPU
renderers ran on the CPU (lavapipe) here; the host's AMD iGPU (Phoenix1) has not been used.

Multi-renderer recipe (museum project's, adapted; `build-probe/` is the shared throwaway tree):

```bash
BUILD_DIR=build-probe SCHOOL_RENDERERS="OPENGLES3;FNA3D;WEBGPU;SDL_GPU" \
  SCHOOL_CMAKE_ARGS="-DFETCHCONTENT_SOURCE_DIR_FNA3D=$HOME/deps/FNA3D -DFETCHCONTENT_UPDATES_DISCONNECTED=ON \
  -DCNA_WEBGPU_ROOT=$HOME/deps/wgpu-native-v29.0.1.1 -DCNA_WEBGPU_AUTO_DOWNLOAD=OFF" tools/build.sh
BUILD_DIR=build-probe VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json tools/tour.sh -r WEBGPU -o build-probe/shots-WEBGPU
```

(One build attempt died with `ar: Bus error` while linking; running the same command again succeeded.)

Not run: DIRECTX9 (Windows only).

## CNA findings

- A textured BasicEffect draw with a constant texture coordinate over a triangle renders black on EasyGL
  (anisotropic sampling with zero derivatives). Reproduced with the NPC boxes; avoided by giving every face
  real UVs. Whether CNA should clamp the LOD or SamplerState::AnisotropicWrap should fall back to linear
  for such triangles is a question for CNA; recorded in `docs/KNOWN_ISSUES.md`.
