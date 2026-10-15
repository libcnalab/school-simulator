# Browser build (WebAssembly + WebGL 2)

The same application runs in a browser: CNA's `WEBGL2` renderer (EasyGL over WebGL 2 = OpenGL ES 3.0), SDL3 for
window, input and audio, the simulation and everything else unchanged. No application code is specific to the web
except where `Content/` is found (`AssetPaths::FindContentRoot` looks at `/Content` under Emscripten).

```bash
tools/build_web.sh            # configure (first time), build, package into build-consumer/web-package/
tools/build_web.sh --serve    # ... and serve it on http://localhost:8080/
tools/web_smoke.sh            # headless Chrome: load, click, walk, F3, busy cafeteria; pictures in build-consumer/web-shots
```

Needs the Emscripten SDK (`$EMSDK`, default `~/emsdk`). The tree is `build-consumer/` (the shared tree for standalone
consumers; the native `build/` and `build-probe/` are untouched), the ccache is the shared one, 2 jobs. Any static web
server works if it sends `.wasm` as `application/wasm`; no threads and no `SharedArrayBuffer`, so no special headers.

## What the build does

- `CMakeLists.txt` (`if(EMSCRIPTEN)`): links `CNA::EmscriptenExceptionAbi` (CNA's JS-lowered exceptions; `school_sim` is
  compiled with `-fexceptions` to match) and `CNA::EmscriptenAsyncify` (the blocking `Game::Run` loop), `-sMIN/MAX_WEBGL_VERSION=2`
  through `cna_apply_emscripten_renderer_link_contract`, 256 MB initial memory (it can grow), `--preload-file Content@/Content`
  and `web/shell.html` (loading bar, canvas, hint line).
- The desktop options work in the address: `?arg=--preset=light&arg=--time=08:00`, and scripts too:
  `?arg=--script&arg=wait:5%3Bbusiest:Cafeteria` (`;` is `%3B`).

## Size (2026-10-14, Release)

| File | Size | gzip -9 |
|---|---|---|
| `cna-school-simulator.wasm` | 6.3 MB | 1.9 MB |
| `cna-school-simulator.data` (Content: textures, sounds, font, schedule) | 5.9 MB | 4.0 MB |
| `cna-school-simulator.js` | 0.27 MB | 0.06 MB |

About 6 MB over the wire with compression. The school itself is built at start-up (0.6-0.8 s in the browser, 764 rooms)
and room meshes lazily as you walk.

## Verified

Headless Chrome (google-chrome, software WebGL 2 through SwiftShader under xvfb), 1280x720: the school builds, 60 fps
shown by F3 in the lobby and in the busy cafeteria (525 people in the room, 413 drawn, 89 draw calls), the mouse
captures with a click (pointer lock), WASD walks, F3 shows the statistics, no JavaScript error but the one below.
Not verified: a real GPU in a desktop browser, other browsers (Firefox, Safari), touch devices, audio output (the
page needs a first click before browsers start audio; the mixer starts without error).

## Smooth look and frame pacing (2026-10-15)

Reported on a desktop browser: the view turned heavily compared with the desktop build, and the game stalled now and then.
Three causes, all fixed:

- **CNA's browser main loop ignored `IsFixedTimeStep = false`.** It always ran fixed 1/60 s Updates from an accumulator, so every
  `requestAnimationFrame` produced zero, one or several Updates (frame times jitter around the display period): uneven pacing and
  mouse input, and after any slow frame a burst of catch-up Updates that made the next frame slower. `Game::EmscriptenMainLoopCallback`
  now does one Update with the real elapsed time and one Draw per frame when the game asks for a variable step (the fixed-step
  path is unchanged).
- **Pointer lock reported accelerated movement.** A plain pointer lock delivers the movement after the operating system's pointer
  acceleration, which damps slow motion; the desktop build gets raw motion. `web/shell.html` makes every `requestPointerLock()` ask
  for `unadjustedMovement` (falls back to the plain lock where the browser does not offer it, e.g. Firefox). CNA also stopped
  throwing away the fractional part of relative mouse motion (`Sdl3Mouse::ConsumeRelativeDelta`), which mattered for slow movement
  on scaled displays.
- **Door plates were rendered the first time they were seen**, each into its own render target in the middle of the frame (a
  render-target switch per plate; a new corridor meant a dozen of them in one frame, 60-300 ms each under software WebGL). They are
  packed into 2048x2048 atlases (10 x 32 plates) rendered once at start (`Screens::Prepare`, with the whiteboards and wing signs),
  and drawn with a sub-rectangle of the atlas.

Stalls that remained (reported again after the first round), found with `--hitch-log` and fixed the same day:

- **Building a room**: the per-vertex light bake (`MeshBuilder::Bake`) looped over every light for every vertex; a library (108 lights,
  60 000 vertices) took ~60 ms natively, so ~150 ms in a browser, on the frame it came into range. The lights now go into a grid
  and a vertex looks only at its neighbouring cells (same result within a rounding step: checked against the plain loop in
  `school_app_tests`); the slowest room is now ~10 ms natively. The per-frame build budget is 6 ms (one room at least).
- **First use of a texture or a shader variant**: all textures are loaded at start (`TextureLibrary::Preload`) and every
  BasicEffect variant (fog, per-pixel lighting, one or two lights) is drawn once as a degenerate triangle (`EffectBank::WarmUp`), so
  nothing is decoded, uploaded or compiled while walking.
- **Draw calls of the door plates**: quads are batched per texture (one call per atlas instead of one per plate).

`--hitch-log MS` (or `?arg=--hitch-log=50`) logs every frame longer than MS with where the time went (input, player, sim,
screens, world, offscreen, sky, people, ...), for hunting stutter on a real GPU.

## Known limits

- Without a click first, SDL asks for pointer lock at start-up and the browser refuses: one logged
  `WrongDocumentError: The root document of this element is not valid for pointer lock`. Harmless; the click that
  follows locks the pointer.
- No touch controls. Gamepads work where the browser supports the Gamepad API (not tested).
- Screenshots (F12) and the logs go to the in-memory file system and are lost.
- A lost WebGL context needs a reload (the page says so).
