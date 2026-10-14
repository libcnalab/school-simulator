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
consumers; the native `build/` and `build-probe/` are untouched), the ccache is the shared one, 3 jobs. Any static web
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

## Known limits

- Without a click first, SDL asks for pointer lock at start-up and the browser refuses: one logged
  `WrongDocumentError: The root document of this element is not valid for pointer lock`. Harmless; the click that
  follows locks the pointer.
- No touch controls. Gamepads work where the browser supports the Gamepad API (not tested).
- Screenshots (F12) and the logs go to the in-memory file system and are lost.
- A lost WebGL context needs a reload (the page says so).
