# Architecture

Two halves, one direction of dependency.

```
school_sim  (no CNA)                      school_app  (CNA)
  World/      plan: rooms, doors, stairs    App/         SchoolGame: the loop, options, services
  Rooms/      furniture and spots           Rendering/   RoomGeometry -> WorldRenderer, NpcRenderer, Screens, Weather, Sky
  Navigation/ room graph, waypoints         Scene/       CollisionWorld (walls, furniture, stair solids)
  Simulation/ schedule, people, SchoolSim   Interaction/ camera, PlayerController, usable things
  Testing/    the command line              Audio/, UI/  SchoolAudio, Hud, Minimap
```

`school_sim` is plain C++ with its own `Vec2`/`Vec3`/`Rect`; `school_app` converts to CNA's `Vector3` at the
boundary. Everything random derives from one seed (`World/Rng.hpp`); the simulation never reads the clock.

## The plan is the single source of truth

`BuildSchoolPlan` (`World/Blueprint.cpp`) writes the whole school as short programs per wing and level and
returns a `SchoolPlan`: floors, wings, rooms (rectangles with a level, a type, a capacity), doors (a point on
a shared wall with a width and a kind), stair shafts, walk surfaces (the sloped flights) and collision solids.
`ValidatePlan` checks it (overlaps, doors off their walls, dangling references, stair landings per level).

From the plan alone:

- `NavGraph` builds the room graph (door edges, stair edges) and answers "which door next?" with one
  Dijkstra per destination, cached (`TreeTo`).
- `LayoutCache` / `BuildRoomLayout` generates the furniture and the spots of a room the first time anyone
  asks (the simulation for seats, the renderer for geometry, collision for solids).
- `RoomGeometry` builds the meshes, `CollisionWorld` the boxes, `Minimap` the plan view.

## The simulation

`SchoolSim` (`Simulation/`) holds one `Agent` per person and an event queue. At every schedule checkpoint
`AssignGoals` asks `GoalFor` where each person goes in this phase, staggers departures by door throughput,
and schedules `Depart` events. A walk is a `Journey` (waypoints with cumulative length, start time, speed):
the position at any time is arithmetic, so most people cost nothing per frame. Near a focus point (the player,
the CCTV cameras) walkers are promoted to full fidelity and stepped with separation, door gates and stuck
detection. See `docs/NPC_SIMULATION.md`.

## Partitioning in the application

- `WorldRenderer::Update` keeps the rooms within 70 m on the player's level, 36 m on adjacent levels and
  the outdoor zones within 130 m built on the GPU (at most 240 rooms, 3 builds per frame); everything else is
  dropped. Secondary anchors (CCTV cameras) add small bubbles.
- The facade (outer brick walls with window glass, roofs) and the ground are built once at start-up, one
  chunk per wing, so the building is complete from outside while interiors stream in.
- Every room is one static vertex-coloured mesh per texture (baked ceiling lights indoors, a baked sun
  outdoors modulated by the time of day at draw time); doors are the only moving parts of the architecture.
- People: `NpcRenderer` walks every agent once per frame (cheap: `Observe` evaluates a walker analytically),
  drops those out of range, out of view or in unloaded rooms, and batches the rest as shaded boxes into one
  dynamic vertex buffer; detail depends on distance (10 boxes, 3, 2).
- Screens refresh at a reduced rate: one CCTV camera per frame while the player is near the wall, signage
  once a second within 45 m, whiteboard content once per subject.
- Audio keeps at most eight room ambiences, chosen by distance and occupancy four times a second.
- `RoomEffects` animates only the specialist rooms that are loaded and within 45 m; `Screens` refreshes
  door plates and board textures once and keeps them.

## Frame

`Update`: input -> elevators (statuses, closed-door blockers, the player's ride) -> player (collision, ground
height, or free flight with F6) -> simulation (`Advance(dt * timeScale)` with the focus
set) -> world streaming -> doors near the player -> interactables -> audio.
`Draw`: offscreen screens -> clear -> sky -> world (outdoor tinted, indoor, doors, elevator cabs, glass) ->
people -> screen quads -> room demonstrations -> weather -> debug lines -> HUD and map.
