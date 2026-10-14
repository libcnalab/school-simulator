#!/usr/bin/env bash
# Visits the school's key places headlessly and saves a screenshot of each to build/shots/.
#
#   tools/tour.sh [-r RENDERER] [-p PRESET] [-o OUTDIR]
#
# Needs xvfb-run. Exit status 1 if any run failed or any draw error was logged. Each stop is one process so a
# problem in one place never hides the others; the log of every run is kept next to its picture.
set -u
cd "$(dirname "$0")/.." || exit 2
renderer=${SCHOOL_RENDERER:-}
preset=normal
out=build/shots
while getopts "r:p:o:" opt; do
  case $opt in
    r) renderer=$OPTARG ;;
    p) preset=$OPTARG ;;
    o) out=$OPTARG ;;
    *) echo "usage: $0 [-r RENDERER] [-p PRESET] [-o OUTDIR]"; exit 2 ;;
  esac
done
bin=build/bin/cna-school-simulator
[ -x "$bin" ] || { echo "build first: tools/build.sh"; exit 2; }
mkdir -p "$out"
export SDL_AUDIODRIVER=dummy
env_renderer=()
[ -n "$renderer" ] && env_renderer=(env CNA_GRAPHICS_RENDERER="$renderer")

status=0
stop() {
  local name=$1 time=$2 script=$3
  shift 3
  local log="$out/$name.log"
  if ! timeout "${SCHOOL_TIMEOUT:-300}" xvfb-run -a -s "-screen 0 1280x720x24" "${env_renderer[@]}" "$bin" --no-audio --preset "$preset" --time "$time" "$@" \
       --script "wait:5;$script;wait:45;shot:$out/$name.png;wait:3;stats;quit" > "$log" 2>&1; then
    echo "FAILED  $name (see $log)"; status=1
  elif grep -q "draw-error" "$log"; then
    echo "DRAW ERROR  $name: $(grep draw-error "$log" | head -1)"; status=1
  else
    echo "ok      $name  $(grep -o 'people [0-9]* .*detailed)' "$log" | tail -1)"
  fi
}

stop entrance   07:55 "look:0,0"
stop lobby      08:05 "goto:Main lobby;look:180,5"
stop classroom  08:10 "busiest:Classroom"
stop physics    09:20 "busiest:Physics lab"
stop computers  10:00 "busiest:Computer lab"
stop corridor   08:46 "busiest:Corridor"
stop stairs     08:46 "busiest:Stairwell;look:180,-10"
stop courtyard  09:45 "goto:Courtyard;look:45,5"
stop cafeteria  12:40 "busiest:Cafeteria"
stop library    12:45 "goto:Library"
stop gym        10:00 "goto:Gym"
stop auditorium 10:00 "goto:Auditorium"
stop security   08:47 "goto:Security office;wait:60"
stop drill      10:05 "drill;goto:Assembly point A;look:180,0;speed:5;wait:150;speed:1"
stop night      21:30 "look:0,10"
stop rain       09:50 "goto:Courtyard;look:45,5" --weather rain
stop map        08:47 "map"
exit $status
