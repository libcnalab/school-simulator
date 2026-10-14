# People

## Who

`BuildPopulation` (`Simulation/Population.cpp`) makes classes of about 25 students (years 7-12, home rooms
in the classroom wings), a teacher per subject need, and the staff: reception, admin, principal, nurse,
counsellor, IT, security, cooks, librarians, janitors, maintenance. Each class gets a weekly timetable
(`kSchoolDays` x periods) with subjects sent to the right room types (labs, computer labs, gym, art, music,
library...). `ValidateTimetable` rejects double-booked teachers or rooms and bad references.

Presets: empty 0, light 100, normal 500, busy 1000, extreme 2400 students (`--preset`, `--students N`).

## Where they go

At every checkpoint (phase start; lunch has a second one for the second wave) `GoalFor` decides per person:

- Students: first lesson's room or a break outdoors on arrival; the timetabled room in lessons and
  passing phases; cafeteria, library, outdoors or a corridor near the next room in the long break; the
  cafeteria in their lunch wave, otherwise library / outdoors / lobby; home at dismissal, 15 % stay for a club
  (library, gym, music, computers, art, football).
- Teachers: staff room, the room of the class they teach, lobby duty, cafeteria at lunch, some supervise clubs.
- Staff: their work spot (reception, offices, kitchen, serving counter at meal times); janitors and guards
  patrol a new room every few minutes (`Event::Tick`).
- Bad weather (`--weather rain|snow`) keeps the breaks indoors.

Departures after a bell are staggered 1-8 s per person and then by door throughput per room (0.6 s per
door lane), so a classroom empties in a stream rather than a burst.

## How they move

A `Journey` is built by `PathBuilder::Build`: the route through the room graph, then per room the door
approach point, the doorway, the exit point; corridor lane points keep opposite flows on their right-hand
side; stairs are climbed along `StairClimbPath` (entry landing, flight, half landing, flight); inside the
start and end rooms the aisles of the layout lead from and to the seat. Cumulative lengths make the position
at any time a lookup.

Three fidelity levels (`UpdateFidelity`, twice a second):

| Level | Who | Cost |
|---|---|---|
| Abstract | far from every focus point | nothing per frame: an arrival event in the queue |
| Reduced | within 90 m or on the focus level | position refreshed by arithmetic for counts, audio and the map |
| Full | within 38 m of a focus point, at most 700 | stepped at 20 Hz: steering to the next waypoint, separation from neighbours in a spatial hash, door gates (wait at the approach until the door is open and it is your turn, per direction), clamped inside the current room, stuck detection (no progress for 7 s hops to the next waypoint) |

Promotion materialises a walker exactly where the arithmetic says it is; demotion shifts the journey's start
time so the arithmetic agrees with where the walker got to. The unit test `full and abstract agree` checks
both ways of walking end in the same rooms.

Doors: `DoorRuntime` per door with an open fraction, a "wanted open until" time (walkers, the player) and a
next-free time per direction; `UpdateDoors` animates and emits open/close sounds.

## Elevators

Two shafts (`SchoolPlan::elevators`, east and west wings, levels 1-4) with a cab room per level and an
elevator door to the corridor. People who prefer the lift (`Npc::usesElevator`) route through a second graph
built with ride edges (`NavGraph(plan, true)`, 8 m-equivalent per level plus 10 for boarding, against about
30 for a flight of stairs); everybody else, and everybody during a fire drill, uses the stairs graph. A ride is
a run of `Ride` waypoints, one per level, in the journey.

The cab (`ElevatorRuntime`): idle, opening, open (3.5 s dwell, extended while people board), closing, moving
at 1.4 m/s; calls per level served in the current direction first (a plain SCAN); capacity 8. At full fidelity a
walker calls the cab at the hall door's approach point, boards when it is open at that level and there is room
(`passengers`), presses its destination (the level of the last ride point), is carried at the cab's height in
the cab room of whatever level the cab is passing, and walks out when the doors open at its level. Abstract
walkers take a timed ride (12 s plus 6 s per level) and are not drawn while inside a shaft. The player calls
the cab with the button beside the door and chooses up or down on the panel inside.

## Fire drill

`StartDrill` sends everybody on campus to the nearest assembly point (load-balanced, per class), with the
alarm as a bell event; arrivals are counted; the drill ends by itself four minutes after the last arrival
(or after an hour) and `AssignGoals` resumes the timetable. A drill is scheduled in the schedule file
(`drill 10:20 wed`); `F` or the alarm points in the lobby start one by hand.

## Checks

`CheckInvariants` (modes vs rooms vs journeys, finite positions, nobody below the building) runs every
simulated second in `school-sim-cli --day --check` and in the tests; `Digest` hashes every position so two
runs can be compared; `StuckSuspects` counts full walkers with no progress.
