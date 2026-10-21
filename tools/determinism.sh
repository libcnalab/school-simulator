#!/usr/bin/env bash
# Two identical fixed-step runs must reach the same simulation digest and the same picture.
#   tools/determinism.sh
set -u
cd "$(dirname "$0")/.." || exit 2
export SDL_AUDIODRIVER=dummy
mkdir -p build/shots
run() {
  timeout 600 xvfb-run -a -s "-screen 0 1280x720x24" build/bin/cna-school-simulator --no-audio --fixed-step 0.05 --preset busy --time 08:44 \
    --script "wait:5;busiest:Corridor;speed:5;wait:150;stats;shot:build/shots/det_$1.png;wait:2;quit" 2>&1 | grep -o "digest [0-9a-f]*"
}
a=$(run a); b=$(run b)
echo "run a: $a"; echo "run b: $b"
if [ "$a" != "$b" ] || [ -z "$a" ]; then echo "DETERMINISM FAILED: digests differ"; exit 1; fi
if ! cmp -s build/shots/det_a.png build/shots/det_b.png; then echo "DETERMINISM FAILED: pictures differ"; exit 1; fi
echo "pictures identical"
echo "DETERMINISM PASSED"
