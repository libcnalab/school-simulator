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
- Elevators (2026-10-14): two shafts (east and west wings, levels 1-4) with cabs, calls, capacity 8, boarding
  and alighting at full fidelity, timed rides for abstract walkers, the player can ride (call button, cab
  buttons). Riders are the people who prefer the lift (`Npc::usesElevator`: 6 % of students, half the teachers,
  most staff); they route through `NavGraph(plan, true)`; fire drills never use the lift.
- Verified only on the OPENGLES3 renderer (Mesa llvmpipe under Xvfb, no GPU on the build machine). A
  multi-renderer tree (OPENGLES3, OPENGL33, SOFTWARE) is the next thing to build and run; results go to
  `docs/RENDERER_TESTING.md`.
- Nothing has been run on a real GPU, with real audio, or with a gamepad.

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

1. **Other renderers**: FNA3D, WEBGPU, SDL_GPU (museum-simulator's `docs/RENDERER_TESTING.md` has the build
   recipe); the three built here (OPENGLES3, OPENGL33, SOFTWARE) pass the tour.
2. **Performance on a GPU**: nothing has been measured on hardware. Corridor segments could be merged per wing,
   far rooms could share a simplified mesh, people beyond 110 m are not drawn at all.
3. **Signs**: wing names at corridor ends, "you are here" boards by the staircases (the door plates and the
   lobby signage show how: `Screens::PlateFor`, `Screens::RenderSignage`).
4. **More demonstrations**: a projected picture in the auditorium (slides), monitors in the computer labs
   that show something, steam over the kitchen, a metronome in the music room.
5. **Elevator polish**: a floor indicator above the hall door, a chime on arrival, the cab's inner door; a
   second pair of lifts in the north and south wings if traffic warrants.
6. **Polish**: stairwell lighting is dim, lobby interior needs more presence (benches and a trophy cabinet),
   the car park cars are boxes, trees are two spheres, the server room is a wall of black racks.
7. **Soak tooling**: `tools/soak.sh` that runs a day at x30 and fails on memory growth or stuck people.
