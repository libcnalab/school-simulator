#!/usr/bin/env bash
# The whole battery: build, unit tests, headless simulation days, data validation and the screenshot tour.
#
#   tools/check_all.sh [-q]      -q skips the screenshot tour (no xvfb needed)
set -u
cd "$(dirname "$0")/.." || exit 2
quick=0
[ "${1:-}" = "-q" ] && quick=1
export CCACHE_DIR=${CCACHE_DIR:-/rv/cnaccache}
export CCACHE_BASEDIR=${CCACHE_BASEDIR:-/rv}
status=0
step() { echo; echo "=== $1"; }

step "build"
tools/build.sh cna-school-simulator school-sim-cli school_tests || exit 1

step "unit and simulation tests"
build/bin/school_tests | tail -3 || status=1

step "plan, timetable, schedule"
build/bin/school-sim-cli --validate --stats --population || status=1
build/bin/cna-school-simulator --validate || status=1

step "one day, normal preset, invariants checked"
build/bin/school-sim-cli --day --check | tail -2 || status=1

step "one day, extreme preset"
build/bin/school-sim-cli --day --preset extreme --check | tail -2 || status=1

step "three days with full-fidelity walking around the lobby"
build/bin/school-sim-cli --days 3 --full --students 300 --check | tail -2 || status=1

if [ $quick -eq 0 ]; then
  step "screenshot tour (build/shots)"
  tools/tour.sh || status=1
fi

echo
[ $status -eq 0 ] && echo "ALL CHECKS PASSED" || echo "CHECKS FAILED"
exit $status
