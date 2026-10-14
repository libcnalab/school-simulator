# HANDOFF

Working notes for whoever continues (human or agent). Dates are when something was last verified.

## State (2026-10-14)

- First session of the project. The simulation core (plan, navigation, timetable, schedule, three-fidelity
  simulation, tests) and the CNA application (player, lazily built world, people, sky, weather, screens, audio,
  HUD, map, scripting) exist and run. `school_tests`: 118 656 checks, 0 failures. `school-sim-cli --day --check`
  passes at normal and extreme population. `tools/tour.sh` photographs 17 places on OPENGLES3 with no draw errors.
- Verified only on the OPENGLES3 renderer (Mesa llvmpipe under Xvfb, no GPU on the build machine). A
  multi-renderer tree (OPENGLES3, OPENGL33, SOFTWARE) is the next thing to build and run; results go to
  `docs/RENDERER_TESTING.md`.
- Nothing has been run on a real GPU, with real audio, or with a gamepad.

## Build, run, test

```bash
tools/build.sh                         # 3 jobs (the owner asked for at most 3 cores); build/ is the only tree
tools/check_all.sh                     # everything; -q skips the screenshot tour
tools/tour.sh [-p busy] [-r SOFTWARE]  # screenshots into build/shots, exit 1 on failure or draw error
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
| Walking, doors, fidelity, drills | `src/Simulation/AgentMotion.cpp` |
| Routes and waypoints | `src/Navigation/NavGraph.cpp` (`PathBuilder::Build`) |
| How a room looks | `src/Rendering/RoomGeometry.cpp` (`BuildRoomGeometry`, `AddFixture`, `AddStairs`, `BuildFacadeGeometry`) |
| What is loaded and drawn | `src/Rendering/WorldRenderer.cpp` (`Update` decides, `Draw` culls, `DrawDoors` animates) |
| People on screen | `src/Rendering/NpcRenderer.cpp` (`AddFigure` is the whole skeleton) |
| Screens | `src/Rendering/Screens.cpp` (CCTV cameras, signage, whiteboards) |
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

1. **Renderer testing**: build the multi-renderer tree, run `tools/tour.sh -r OPENGL33` and `-r SOFTWARE`
   (SOFTWARE is slow: raise the timeout), record in `docs/RENDERER_TESTING.md`, fix CNA where it differs.
2. **Door plates and signs**: room codes on a plate beside every door (render-target text, `Screens`), wing
   names at corridor ends, "you are here" boards by the staircases.
3. **Specialist room demonstrations**: pendulum and projectile in the physics labs, bubbling flasks in
   chemistry, running machines in the workshops, bouncing balls in the gym, a projected picture in the
   auditorium: small animated lit objects per loaded room (`Shapes` + `EffectBank::BeginLit`).
4. **Elevators**: the plan supports shafts (`ElevatorShaft`, `RoomType::ElevatorCab`) but none are placed;
   the lobby could get two. Needs cab motion, calls, NPC boarding, a navigation edge with waiting cost.
5. **App-level tests**: a `school_app_tests` target that builds the geometry of every room and the facade
   (no device needed) and checks bounds and sizes; a leak check over three tours.
6. **Performance on a GPU**: measure; corridor segments could be merged per wing, far rooms could share a
   simplified mesh; NPCs beyond 110 m are not drawn at all.
7. **Polish**: stairwell lighting is dim, lobby interior needs more presence (signage exists, add benches
   and a trophy cabinet), the car park cars are boxes, trees are two spheres.
