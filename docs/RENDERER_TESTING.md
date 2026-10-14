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
| 2026-10-14 | OPENGLES3 (EasyGL, Mesa llvmpipe 25.0 under Xvfb) | build host, no GPU | tour: 17/17 ok, no draw errors, 10-25 fps in software |
| 2026-10-14 | OPENGL33 (EasyGL, same Mesa) | build host | tour: 17/17 ok, no draw errors; pictures match OPENGLES3 pixel for pixel in the scenes compared (classroom, cafeteria, stairs) |
| 2026-10-14 | SOFTWARE (CNA's CPU rasteriser) | build host | tour: 17/17 ok with `SCHOOL_TIMEOUT=1500`, no draw errors; same geometry, lighting, render targets and text as the GL renderers; only the room banner's fade differs (frame timing) |
| 2026-10-14 | OPENGLES3 | build host | `tools/determinism.sh`: two fixed-step runs give the same digest and byte-identical screenshots |

Not run: FNA3D, WEBGPU, SDL_GPU, DIRECTX9 (not built into this tree; see the museum project's notes for how).

## CNA findings

- A textured BasicEffect draw with a constant texture coordinate over a triangle renders black on EasyGL
  (anisotropic sampling with zero derivatives). Reproduced with the NPC boxes; avoided by giving every face
  real UVs. Whether CNA should clamp the LOD or SamplerState::AnisotropicWrap should fall back to linear
  for such triangles is a question for CNA; recorded in `docs/KNOWN_ISSUES.md`.
