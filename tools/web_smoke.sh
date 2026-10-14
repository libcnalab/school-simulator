#!/usr/bin/env bash
# Smoke test of the browser build: serves build-consumer/web-package, opens it in headless Chrome (software WebGL 2 under
# xvfb), clicks to capture the mouse, walks forward, shows the F3 statistics and checks the console and the picture.
#
#   tools/web_smoke.sh            (run tools/build_web.sh first)
set -u
cd "$(dirname "$0")/.." || exit 2
pkg=build-consumer/web-package
[ -f "$pkg/index.html" ] || { echo "build first: tools/build_web.sh"; exit 2; }
command -v google-chrome >/dev/null && command -v node >/dev/null && command -v xvfb-run >/dev/null || { echo "needs google-chrome, node and xvfb-run"; exit 2; }
out=${SCHOOL_WEB_OUT:-build-consumer/web-shots}; mkdir -p "$out"
profile=$(mktemp -d "${TMPDIR:-/tmp}/school-web-profile.XXXXXX")   # Chrome's own scratch profile; removed below
port=$((18000 + RANDOM % 1000))
(cd "$pkg" && exec python3 -m http.server "$port" > /dev/null 2>&1) &
server=$!
trap 'kill $server 2>/dev/null; rm -rf "$profile"' EXIT
sleep 1
status=0
check() {   # name, url, steps
  local log="$out/$1.log"
  if ! timeout 300 node --experimental-websocket tools/web_drive.mjs "$2" "$3" "$profile" > "$log" 2>&1; then echo "FAILED  $1 (see $log)"; status=1; return; fi
  if ! grep -q "graphics renderer: WEBGL2" "$log"; then echo "FAILED  $1: WebGL 2 renderer did not start"; status=1
  elif ! grep -q "school built" "$log"; then echo "FAILED  $1: the school was not built"; status=1
  elif grep "EXCEPTION" "$log" | grep -v "pointer lock" | grep -q .; then echo "FAILED  $1: $(grep EXCEPTION "$log" | grep -v 'pointer lock' | head -1)"; status=1
  else echo "ok      $1  $(grep -o 'school built: [^(]*' "$log" | head -1)"; fi
}
check walk "http://localhost:$port/?arg=--time=09:00&arg=--preset=normal" \
  "[{\"wait\":40},{\"click\":[640,360]},{\"wait\":2},{\"key\":\"w\",\"hold\":4000},{\"key\":\"F3\",\"vk\":114,\"hold\":150},{\"wait\":3},{\"shot\":\"$out/walk.png\"},{\"eval\":\"document.pointerLockElement ? 'locked' : 'not locked'\"}]"
check cafeteria "http://localhost:$port/?arg=--time=12:40&arg=--preset=busy&arg=--script&arg=wait:5%3Bbusiest:Cafeteria" \
  "[{\"wait\":45},{\"key\":\"F3\",\"vk\":114,\"hold\":150},{\"wait\":4},{\"shot\":\"$out/cafeteria.png\"}]"
[ $status -eq 0 ] && echo "WEB SMOKE PASSED (pictures in $out)"
exit $status
