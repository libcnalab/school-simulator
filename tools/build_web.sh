#!/usr/bin/env bash
# The browser build: WebAssembly + WebGL 2 through Emscripten, packaged for any static web server.
#
#   tools/build_web.sh            configure (first time), build, package into build-consumer/web-package/
#   tools/build_web.sh --serve    ... then serve it on http://localhost:8080/ (python3 http.server)
#
# build-consumer/ is the shared tree for standalone consumers (../CLAUDE.md); the native trees (build/, build-probe/) are untouched.
# Environment: EMSDK (default ~/emsdk), JOBS (default 2), RECONFIGURE=1, VERBOSE=1.
set -u
cd "$(dirname "$0")/.." || exit 2
export CCACHE_DIR=${CCACHE_DIR:-/rv/cnaccache}
export CCACHE_BASEDIR=${CCACHE_BASEDIR:-/rv}
emsdk=${EMSDK:-$HOME/emsdk}
[ -f "$emsdk/emsdk_env.sh" ] || { echo "no Emscripten SDK at $emsdk (set EMSDK)"; exit 2; }
# shellcheck disable=SC1091
source "$emsdk/emsdk_env.sh" > /dev/null 2>&1
command -v emcmake >/dev/null || { echo "emcmake not found after sourcing $emsdk/emsdk_env.sh"; exit 2; }
build_dir=build-consumer
launchers=()
if command -v ccache >/dev/null 2>&1; then
  launchers=(-DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache)
else
  echo "note: ccache not found; building without a compiler cache"
fi
generator=()
command -v ninja >/dev/null 2>&1 && generator=(-G Ninja)
mkdir -p "$build_dir"
if [ ! -f "$build_dir/CMakeCache.txt" ] || [ "${RECONFIGURE:-0}" = 1 ]; then
  echo "configuring $build_dir for Emscripten ..."
  emcmake cmake -S . -B "$build_dir" "${generator[@]}" "${launchers[@]}" -DCMAKE_BUILD_TYPE=Release \
    -DCNA_GRAPHICS_RENDERER=WEBGL2 -DCNA_GRAPHICS_RENDERERS=WEBGL2 > "$build_dir/configure.log" 2>&1 \
    || { tail -30 "$build_dir/configure.log"; echo "configure FAILED"; exit 1; }
fi
if [ "${VERBOSE:-0}" = 1 ]; then
  cmake --build "$build_dir" --target cna-school-simulator --parallel "${JOBS:-2}"; status=$?
else
  cmake --build "$build_dir" --target cna-school-simulator --parallel "${JOBS:-2}" > "$build_dir/build.log" 2>&1; status=$?
  grep -E "error:|FAILED:|undefined (reference|symbol)|CMake Error|wasm-ld: error" -A3 "$build_dir/build.log" | grep -v "^--$" | cut -c1-260 | head -60
fi
[ $status -eq 0 ] || { echo "build FAILED (log: $build_dir/build.log)"; exit 1; }

out=$build_dir/web-package
rm -rf "$out" && mkdir -p "$out"
for ext in html js wasm data; do
  [ -f "$build_dir/bin/cna-school-simulator.$ext" ] && cp "$build_dir/bin/cna-school-simulator.$ext" "$out/" \
    || { [ -f "$build_dir/cna-school-simulator.$ext" ] && cp "$build_dir/cna-school-simulator.$ext" "$out/"; }
done
[ -f "$out/cna-school-simulator.html" ] || { echo "no cna-school-simulator.html was produced"; exit 1; }
cp "$out/cna-school-simulator.html" "$out/index.html"
cp LICENSE "$out/" 2>/dev/null
cat > "$out/README.txt" <<'TXT'
School Simulator, browser build (WebAssembly + WebGL 2 via CNA).
Serve this directory with any static web server that sends .wasm as application/wasm, e.g.
    python3 -m http.server 8080
and open http://localhost:8080/ . Click the picture to capture the mouse; F1 shows the controls.
Options of the desktop program can be passed in the address: ?arg=--preset=light&arg=--time=08:00
TXT
du -sh "$out"/* | sed 's/^/  /'
echo "web build ok: $out"
if [ "${1:-}" = "--serve" ]; then
  echo "serving http://localhost:8080/ (Ctrl+C stops)"
  cd "$out" && exec python3 -m http.server 8080
fi
