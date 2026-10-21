# Performance notes

Everything here was measured on the build host (headless: Mesa llvmpipe software OpenGL, the iGPU unused, a shared machine with a
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

## Draw calls: corridor groups and portal culling (2026-10-14)

Two measures against the draw-call count in long halls (`WorldRenderer`):

- **Corridor groups**: up to three touching segments of one corridor (same wing, level and axis) are built, baked and
  drawn as one mesh (`FindCorridorGroups`, `BuildGroup`); 49 groups for 157 segments. A group is loaded and dropped as
  a whole and shines as brightly as its brightest segment. Alone it saved about 8 % of the calls: most of the calls
  were rooms behind the corridor walls.
- **Portal culling** (`ComputePortalVisibility`): a room is drawn only if the eye is in it, within 3 m of it, or an open
  door (or an opening, or glass) in the view leads to it, up to 12 doors deep; rooms with windows count as visible from
  rooms that have windows themselves and from outdoors. Inside the frustum but unreachable: culled. Interior walls have
  no windows, so nothing visible is lost; the door frames moved to the corridor side of the wall for that reason
  (they used to belong to the room behind the door).

Measured at `--preset normal --fixed-step 0.016` (F3 / `stats`), draw calls and triangles:

| View | before | corridor groups | + portal culling |
|---|---|---|---|
| N2.07 looking west | 333 / 90 500 | 314 / 89 200 | 107 / 50 200 |
| N2.07 looking east | 520 / 104 000 | 478 / 101 000 | 104 / 23 300 |
| cafeteria at lunch | 144 / 112 500 | - | 76 / 53 800 |

Ten views were rendered with the culling on and off and compared pixel by pixel: no difference beyond a few edge
pixels of animated doors (`tools/tour.sh` and `docs/RENDERER_TESTING.md` use the same views). Knobs for comparing:
`SCHOOL_PORTAL_CULLING=0` and `SCHOOL_CORRIDOR_SEGMENTS=1` switch the two off. The F3 line `rooms loaded .. drawn ..
culled ..` shows the effect.

## Determinism

`tools/determinism.sh`: two runs with `--fixed-step 0.05` reach the same digest (`SchoolSim::Digest`, a hash
of every position) and byte-identical screenshots on OPENGLES3.

With `--fixed-step` the pictures depend on the frame count only: rooms are built `buildsPerFrame` at a time without
the 6 ms wall-clock budget, and room lights fade by the fixed step. Without that, which rooms a frame had built
depended on how fast the machine was, and a night view with lights fading in differed between two runs. The script
now fails when the pictures differ. The tour's 17 places plus two views with the HUD (one at night in rain), each
run twice as fixed-step runs, are byte-identical on Mesa llvmpipe.
