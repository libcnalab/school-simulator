# HANDOFF

Working notes for whoever continues (human or agent). Dates are when something was last verified.

## State (2026-10-14)

- First session of the project. The simulation core (plan, navigation, timetable, schedule, three-fidelity
  simulation, elevators, tests) and the CNA application (player, lazily built world, people, sky, weather,
  screens, room demonstrations, audio, HUD, map, scripting) exist and run. `school_tests`: 154 500 checks,
  0 failures; `school_app_tests`: 3.46 million checks, 0 failures. `school-sim-cli --day --check` passes at
  normal, busy and extreme population. `tools/tour.sh` photographs 17 places on OPENGLES3 and OPENGL33 with no
  draw errors. A one-day soak at x30 with the busy preset: resident memory 279 -> 302 MB over 3.5 school hours,
  flat afterwards (navigation trees and door plates filling up).
- Later on 2026-10-14: default preset is busy (1000 students); lamp posts glow at night and room lights follow
  occupancy (light switches beside the doors override them); door plates, wing signs, floor indicators,
  computer-lab desktops, diagnostics monitors, art still life, locker and toilet visits, people sitting on
  benches, trophy cabinets, elevator chime, F12 screenshots.
- **Wayland** (reported by the owner 2026-10-14): CNA refused to start because it could not centre the window
  (`cannot position non-popup windows`). `Sdl3Window::CenterOnDisplay` is best-effort now; the fix went into CNA
  with commit `96522bf34` on branch `next` (the other agent's re-centring change). Not yet confirmed on the
  owner's Wayland session after a rebuild; fallback is `SDL_VIDEO_DRIVER=x11` (XWayland). `tools/soak.sh -p extreme -n 6`: memory flat at
  307 MB, nobody stuck. `tools/determinism.sh` passes.
- Elevators (2026-10-14): two shafts (east and west wings, levels 1-4) with cabs, calls, capacity 8, boarding
  and alighting at full fidelity, timed rides for abstract walkers, the player can ride (call button, cab
  buttons). Riders are the people who prefer the lift (`Npc::usesElevator`: 6 % of students, half the teachers,
  most staff); they route through `NavGraph(plan, true)`; fire drills never use the lift.
- Verified only on the OPENGLES3 renderer (Mesa llvmpipe under Xvfb: headless runs here never touch the machine's GPU). A
  multi-renderer tree (OPENGLES3, OPENGL33, SOFTWARE) is the next thing to build and run; results go to
  `docs/RENDERER_TESTING.md`.
- The host does have an AMD Phoenix1 iGPU and HD-Audio hardware (the owner's GNOME/Wayland session, Xwayland on :0), but every run recorded here was headless (Xvfb + llvmpipe + dummy audio). Nothing has been measured on the GPU, heard through real audio, or tried with a gamepad.

- **Browser build** (2026-10-14): `tools/build_web.sh` makes `build-consumer/web-package/` (WebGL 2 via Emscripten, 6 MB
  compressed); `tools/web_smoke.sh` drives headless Chrome through it (the Claude Chrome extension was not connected, so
  `tools/web_drive.mjs` speaks the DevTools protocol itself). Details in `docs/WEB_BUILD.md`.
- **Rendering performance** (2026-10-14): corridor groups and portal culling cut draw calls in a long corridor from ~500
  to ~100 (`docs/PERFORMANCE.md`). Details added the same day: cars, three tree species, server racks in cold-aisle rows
  with blinking lights, a metronome on the piano, kitchen steam. `school-sim-cli --room CODE` prints a room's rectangle and
  fixtures (for placing the camera).

## Build, run, test

```bash
tools/build.sh                         # 3 jobs (the owner asked for at most 3 cores); build/ is the only tree
tools/check_all.sh                     # everything; -q skips the screenshot tour
tools/tour.sh [-p busy] [-r SOFTWARE]  # screenshots into build/shots, exit 1 on failure or draw error
tools/soak.sh                          # a school morning at x30, memory and stuck-people check
tools/determinism.sh                   # two fixed-step runs, same digest and picture
xvfb-run -a -s "-screen 0 1280x720x24" build/bin/cna-school-simulator --no-audio --script "wait:5;busiest:Cafeteria;shot:x.png;quit"
```

There is no X server here: every run of the app goes through `xvfb-run` with `SDL_AUDIODRIVER=dummy`.
Project rules from `../CLAUDE.md` apply: one shared ccache (`CCACHE_DIR=/rv/cnaccache`, exported by
`tools/build.sh`), never build in `/tmp`, never create build directories outside the closed list.

## Where things are

| Need | Look at |
|---|---|
| Change the building | `src/World/Blueprint.cpp` (strips, cores and programs per wing and level), `ValidatePlan` guards it |
| Furniture and spots of a room type | `src/Rooms/RoomLayout.cpp` (`Gen::Run` dispatches per type) |
| How people decide where to go | `SchoolSim::GoalFor` in `src/Simulation/SchoolSim.cpp` |
| Walking, doors, elevators, fidelity, drills | `src/Simulation/AgentMotion.cpp` (`UpdateElevators`, the ride branch of `StepFull`) |
| Routes and waypoints | `src/Navigation/NavGraph.cpp` (`PathBuilder::Build`) |
| How a room looks | `src/Rendering/RoomGeometry.cpp` (`BuildRoomGeometry`, `AddFixture`, `AddStairs`, `BuildFacadeGeometry`) |
| What is loaded and drawn | `src/Rendering/WorldRenderer.cpp` (`Update` decides, `Draw` culls, `DrawDoors` animates) |
| People on screen | `src/Rendering/NpcRenderer.cpp` (`AddFigure` is the whole skeleton) |
| Screens | `src/Rendering/Screens.cpp` (CCTV cameras, signage, whiteboards, door plates, diagnostics monitors) |
| Animated demonstrations | `src/Rendering/RoomEffects.cpp` |
| Elevator cabs, doors, player ride | `WorldRenderer::DrawElevators`, `SchoolGame::UpdateElevators`, `PlayerController::SetRide` |
| Keys, HUD content, scripting | `src/App/SchoolGame.cpp` (`HandleSystemKeys`, `Draw`, `RunScript`) |
| Sounds | `src/Audio/SchoolAudio.cpp` |

## Gotchas learned the hard way

- The `look:` script op is absolute (yaw 0 = north); `turn:` and `pitch:` are relative to the current view.
  `goto:` and `busiest:` face into the room, `atdoor:` faces the door from two metres outside.
- CNA's configure-time audits fail for a minute now and then while another agent edits `../cna`; `tools/build.sh`
  says so. Wait and retry; never "fix" it by touching CNA's audit files.

- A textured BasicEffect draw whose triangles have a constant texture coordinate renders **black** on EasyGL
  (zero derivatives with anisotropic sampling). Give every face real UVs even on a white texture.
- Panels drawn as two triangles: flipping the "right" axis flips the winding and CullClockwise drops them.
  Mirror the texture coordinates instead (`Screens::DrawQuads`).
- `OccupantsOf(room)` counts people standing at spots plus full-fidelity walkers; abstract walkers passing
  through are not in it. Count with `SchoolSim::Observe` when you need everybody (the `busiest:` script op does).
- Bells rung while fast-forwarding at start-up were reported as news; `TakeBells()` is drained after every jump.
- With more people than seats (extreme preset) the overflow positions must be clamped to the room, or
  `CheckInvariants` reports people outside their room (fixed in `SeatGoal` and `CafeteriaGoal`).

## What is next (priority order)

1. **Renderers on the real GPU**: all six built renderers (OPENGLES3, OPENGL33, SOFTWARE, FNA3D, WEBGPU, SDL_GPU) pass
   the tour headless (2026-10-14, recipe in `docs/RENDERER_TESTING.md`); none has run on the AMD iGPU (needs the
   owner's desktop session, DISPLAY=:0 / Wayland).
2. **Performance on a GPU**: nothing has been measured on the GPU (the headless runs use llvmpipe). Far rooms could share a
   simplified mesh; people beyond 110 m are not drawn at all.
3. **Signs**: wing names at corridor ends, "you are here" boards by the staircases (the door plates and the
   lobby signage show how: `Screens::PlateFor`, `Screens::RenderSignage`).
4. **More demonstrations**: a projected picture in the auditorium (slides), monitors in the computer labs
   that show something, steam over the kitchen, a metronome in the music room.
5. **Elevator polish**: a floor indicator above the hall door, a chime on arrival, the cab's inner door; a
   second pair of lifts in the north and south wings if traffic warrants.
6. **Polish**: stairwell lighting is dim, the car park cars are boxes, trees are two spheres, the server room is
   a wall of black racks, students never visit the nurse or the toilets, nobody uses the fitness room.
7. **Soak tooling**: `tools/soak.sh` that runs a day at x30 and fails on memory growth or stuck people.
