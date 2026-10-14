# Known issues

## Open

- **Constant UVs render black on EasyGL** (OPENGLES3, Mesa llvmpipe): a textured BasicEffect draw whose
  triangles all carry the same texture coordinate comes out black with `SamplerState::AnisotropicWrap`.
  The school gives every face real UVs, so nothing in the app shows it; a CNA-side check (LOD clamp or
  anisotropic fallback) is still worth doing. Found 2026-10-14.
- FNA3D, WEBGPU and SDL_GPU pass the tour (on the CPU, lavapipe); none of the renderers has run on the AMD iGPU, and DIRECTX9 has not been run (`docs/RENDERER_TESTING.md`).
- Elevator: abstract riders inside a shaft are not drawn (they would float at the wrong height); the cab has
  no inner door and no arrival chime; a cab that is full keeps people waiting up to 60 s, then they board anyway.
- Frame rate on the build host is 10-25 fps because headless runs use Mesa llvmpipe (software GL under Xvfb); the host's AMD iGPU has not been measured.
- Extreme population: 21 late arrivals per day (people whose seat overflowed walk further); fine, but the
  overflow spots are rings around a seat, not extra desks.
- Corridors are drawn one mesh per 18 m segment with about six draw calls each; a wing-level merge would
  halve the draw calls in long views.
- The player can look through the gap between the interior wall and the facade at a window reveal when
  standing inside the reveal box (0.3 m); the collision keeps the player 0.3 m from walls, so it needs effort.

## Fixed

- **Start-up failed on Wayland**: `Window::CenterOnDisplay failed: wayland cannot position non-popup windows`.
  CNA treated SDL's refusal to place a window as fatal; it is cosmetic. Fixed in CNA (`Sdl3Window::CenterOnDisplay`
  is best-effort now; committed in CNA as 96522bf34). Until a rebuild of this project picks it up, `SDL_VIDEO_DRIVER=x11 build/bin/cna-school-simulator` (XWayland) works.
  2026-10-14.

- NPC boxes rendered black (constant UVs, see above). 2026-10-14.
- Screen quads invisible: flipping a panel's width axis flipped the winding, CullClockwise dropped them;
  texture coordinates are mirrored instead. 2026-10-14.
- CCTV quads hidden inside the screen fixture (missing the depth offset). 2026-10-14.
- Extreme population: seat and queue overflow positions could leave the room; clamped. 2026-10-14.
- Bells rung during the start-up fast-forward were announced as news. 2026-10-14.
