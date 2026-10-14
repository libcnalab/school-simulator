#!/usr/bin/env bash
# Soak: runs the app headless through a school day at x30 with the busy preset, logging the statistics every
# 1500 frames, then checks that resident memory did not keep growing and nobody is stuck.
#
#   tools/soak.sh [-p PRESET] [-n ROUNDS]     default busy, 10 rounds of 1500 frames (about a school morning)
set -u
cd "$(dirname "$0")/.." || exit 2
preset=busy
rounds=10
while getopts "p:n:" opt; do
  case $opt in
    p) preset=$OPTARG ;;
    n) rounds=$OPTARG ;;
    *) echo "usage: $0 [-p PRESET] [-n ROUNDS]"; exit 2 ;;
  esac
done
export SDL_AUDIODRIVER=dummy
script="wait:5;speed:30"
for _ in $(seq 1 "$rounds"); do script="$script;wait:1500;stats"; done
script="$script;quit"
log=build/soak.log
timeout 7200 xvfb-run -a -s "-screen 0 640x360x24" build/bin/cna-school-simulator --no-audio --width 640 --height 360 --preset "$preset" --time 07:30 --script "$script" > "$log" 2>&1
status=$?
grep -o "rss [0-9]* MB" "$log" | awk '{print $2}' > build/soak-rss.txt
first=$(head -1 build/soak-rss.txt); last=$(tail -1 build/soak-rss.txt); mid=$(sed -n "$(( $(wc -l < build/soak-rss.txt) / 2 ))p" build/soak-rss.txt)
stuck=$(grep -o "stuck now [0-9]*" "$log" | awk '{print $3}' | sort -n | tail -1)
unreachable=$(grep -o "unreachable [0-9]*" "$log" | awk '{print $2}' | sort -n | tail -1)
echo "rss first ${first:-?} MB, middle ${mid:-?} MB, last ${last:-?} MB; max stuck ${stuck:-?}; max unreachable ${unreachable:-?}; exit $status"
[ "$status" -eq 0 ] || { echo "SOAK FAILED: the app did not exit cleanly (see $log)"; exit 1; }
# Growth of more than 15 % between the middle and the end of the run means something keeps allocating.
if [ -n "$mid" ] && [ -n "$last" ] && [ "$last" -gt $(( mid * 115 / 100 )) ]; then echo "SOAK FAILED: memory keeps growing"; exit 1; fi
[ "${stuck:-0}" -le 3 ] || { echo "SOAK FAILED: $stuck people stuck"; exit 1; }
[ "${unreachable:-0}" -eq 0 ] || { echo "SOAK FAILED: unreachable destinations"; exit 1; }
echo "SOAK PASSED"
