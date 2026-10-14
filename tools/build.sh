#!/usr/bin/env bash
# Configure (first time only) and build school-simulator. Works from a clean checkout.
#
#   tools/build.sh [target ...]        default targets: cna-school-simulator
#
# Environment (all optional):
#   BUILD_DIR            build tree, relative to the repository root (default: build)
#   SCHOOL_RENDERERS     semicolon list for CNA_GRAPHICS_RENDERERS, e.g. "OPENGLES3;OPENGL33;SOFTWARE";
#                        the first entry becomes the default renderer. Empty: CNA's platform default.
#   SCHOOL_CMAKE_ARGS    extra arguments for the configure step (e.g. -DSCHOOL_BUILD_APP=OFF for logic only)
#   RECONFIGURE=1        run cmake again even if the tree is already configured
#   VERBOSE=1            stream the full build output instead of only diagnostics
#   JOBS=N               parallel jobs (default 3: the CPU budget set for this project)
#
# ccache is used when it is on PATH; CCACHE_DIR=/rv/cnaccache and CCACHE_BASEDIR=/rv are exported if unset
# (the one shared cache, see ../CLAUDE.md). Cache size is governed by the cache's own config, never here.
set -u
cd "$(dirname "$0")/.." || exit 2
export CCACHE_DIR=${CCACHE_DIR:-/rv/cnaccache}
export CCACHE_BASEDIR=${CCACHE_BASEDIR:-/rv}
build_dir=${BUILD_DIR:-build}
targets=("$@")
[ ${#targets[@]} -gt 0 ] || targets=(cna-school-simulator)

mkdir -p "$build_dir"

generator=()
command -v ninja >/dev/null 2>&1 && generator=(-G Ninja)

launchers=()
if command -v ccache >/dev/null 2>&1; then
  launchers=(-DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache)
else
  echo "note: ccache not found; building without a compiler cache (slower rebuilds)"
fi

renderer_args=()
if [ -n "${SCHOOL_RENDERERS:-}" ]; then
  default_renderer=${SCHOOL_RENDERERS%%;*}
  renderer_args=("-DCNA_GRAPHICS_RENDERERS=${SCHOOL_RENDERERS}" "-DCNA_GRAPHICS_RENDERER=${default_renderer}")
fi

if [ ! -f "$build_dir/CMakeCache.txt" ] || [ "${RECONFIGURE:-0}" = 1 ]; then
  echo "configuring $build_dir ..."
  # shellcheck disable=SC2086
  cmake -S . -B "$build_dir" "${generator[@]}" "${launchers[@]}" "${renderer_args[@]}" ${SCHOOL_CMAKE_ARGS:-} \
    > "$build_dir/configure.log" 2>&1 || { tail -30 "$build_dir/configure.log"; echo "configure FAILED"; exit 1; }
fi

if [ "${VERBOSE:-0}" = 1 ]; then
  cmake --build "$build_dir" --target "${targets[@]}" --parallel "${JOBS:-3}"
  status=$?
else
  cmake --build "$build_dir" --target "${targets[@]}" --parallel "${JOBS:-3}" \
    > "$build_dir/build.log" 2>&1
  status=$?
  # Errors always; warnings only from this project's own sources (CNA's third_party code is noisy).
  grep -E "error:|FAILED:|undefined reference|CMake Error|^(\.\./)*src/[^ ]*: warning:|^(\.\./)*tests/[^ ]*: warning:" \
    -A3 "$build_dir/build.log" | grep -v "^--$" | cut -c1-260 | head -"${LINES_MAX:-60}"
fi
if [ $status -eq 0 ]; then
  echo "build ok"
else
  echo "build FAILED (log: $build_dir/build.log)"
  # CNA is edited by other agents while we build against it; its configure-time audits fail for a minute now and then.
  if grep -q "audit failed\|Configuring incomplete" "$build_dir/build.log" "$build_dir/configure.log" 2>/dev/null; then
    echo "note: CNA's own configure step failed (its checkout is probably mid-edit by someone else); wait a minute and run this again"
  fi
fi
exit $status
