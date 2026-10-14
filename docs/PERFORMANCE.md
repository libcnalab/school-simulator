# Performance notes

Everything here was measured on the build host (no GPU, Mesa llvmpipe software OpenGL, a shared machine with a
load average around 12), so frame rates say nothing about hardware; the simulation numbers do.

## Simulation (headless, `school-sim-cli`)

| Run | Wall time | Notes |
|---|---|---|
| one day, normal preset (559 people), abstract | 2-4 s | 14 000 path builds, 16 000 events |
| one day, busy preset (1112 people), abstract | 5-8 s | 26 000 path builds |
| one day, extreme preset (2500+ people), abstract | 8-12 s | 52 000 path builds, 72 000 events |
| one day, extreme, full fidelity around the lobby (up to 700 walkers at 20 Hz) | 94-266 s | the spread is the shared machine; 31 000 door passes, 140 unstuck hops |
| three days, 300 students, full fidelity around the lobby | 36-68 s | 13 700 door passes, 18 unstuck hops, 14 late arrivals |

Per frame in the app the simulation costs 0.02-0.7 ms at normal and busy presets (F3 shows `sim ms`); the
full-fidelity walkers near the player dominate it.

## Memory

`tools/soak.sh` (a school morning at x30, 640x360): resident memory 279 -> 302 MB at busy over 3.5 school hours,
then flat; 307 -> 308 MB at extreme over six rounds. What grows and stops: navigation trees (one per
destination ever used, at most 764), door plate and board textures, room meshes up to the 240-room cap.

## Rendering (software OpenGL, 1280x720)

| Scene | fps | triangles | draw calls | people drawn |
|---|---|---|---|---|
| classroom during a lesson | 20-25 | 20 000 | 80 | 34-40 |
| corridor at the bell, busy preset | 12-23 | 85 000-100 000 | 420-500 | 50-115 |
| cafeteria at lunch | 10 | 140 000 | 280 | 205-260 |
| security office with the CCTV wall refreshing one camera per frame | 10 | 21 000 (main view) | 78 | - |

Draw calls are one per texture per visible room (about six for a classroom) plus one per door leaf, one per
elevator cab part, one batch of up to 2 730 boxes for the people, and one per screen quad. A GPU should run
all of this far above 60 fps; the first thing to measure there is the number of rooms drawn in long corridor
views (`F3`: `rooms drawn`) and whether merging corridor segments per wing is worth it.

## Determinism

`tools/determinism.sh`: two runs with `--fixed-step 0.05` reach the same digest (`SchoolSim::Digest`, a hash
of every position) and byte-identical screenshots on OPENGLES3.
