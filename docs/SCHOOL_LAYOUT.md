# School layout

Units are metres; X runs east, Z south, Y up. Level 0 is the basement (Y = -4), level 1 the ground floor
(Y = 0), levels 2..5 the upper floors (4 m each). Room codes: `<wing letter><floor>.<number>` (`N1.05` = north
wing, first floor, room 5; `SB.03` = south wing basement); outdoor zones are `O.nn`.

## The ring (164 x 124 m, courtyard inside)

```
        north strip NO (z 0..9)   stairs at x 38 and 104, toilets beside them
        corridor (z 9..13)        ------------------------------------------
        north strip NI (z 13..22) passage to the courtyard at x 80 (ground floor)
 west   |                                                               | east
 strips |                    courtyard 120 x 80                         | strips
 WO WI  |                                                               | EO EI
        south strip SI (z 102..111)   main lobby 64..100 x 102..124 (ground floor)
        corridor (z 111..115)
        south strip SO (z 115..124)   stairs at x 38 and 118
```

Corner towers hold a staircase at every level; mid-wing stairwells sit in the outer strips, each with an
elevator shaft beside it in the east and west wings (a 2.6 x 2.6 cab at the corridor, a closet behind it). The ring exists on
levels 1..5 (north) and 0..4 (south); the east and west corridors on 1..4. Every strip is written in
`Blueprint.cpp` as cores (stairs, toilets, passages: the same on every level) and programs for the gaps:
`"CL*"` fills with classrooms, `"PH PP CH PP"` is physics lab, prep room, chemistry lab, prep room.

| Wing | Levels | Programme |
|---|---|---|
| North (Humanities) | 1-5 | classrooms, language rooms; staff room and meeting room on the top floor, computer rooms |
| East (Science) | 1-4 | biology, physics and chemistry labs with prep rooms, computer labs, an office |
| South (Main building) | 0-4 | basement: lockers, boiler, electrical, servers, archive, workshop; ground: reception, offices, principal, security, nurse, counselling, meeting rooms; first: art, music, workshop; second: computer labs; third: classrooms and language rooms |
| West (Languages & Mathematics) | 1-4 | language rooms and classrooms |
| Sports block (west of the ring) | 1 | gymnasium (44 x 42, two storeys), changing rooms, fitness room, equipment store, two workshops, a concourse |
| Community block (east of the ring) | 1 | kitchen, cafeteria (36 x 30, two storeys), auditorium (36 x 42), library (36 x 52), music hall, drama studio |

## Campus

Courtyard inside the ring; front plaza south of the lobby with the car park and bicycle area beyond; east
and west yards; the playground west of the sports block; lawns; the running track and football field north
of the building with five assembly points on its south edge; four more assembly points on the west and east
yards; the street (off campus) south of the car park with three gates. Every outdoor zone touching another is
joined by openings; buildings have external doors from the lobby, the stair towers, the gym, changing rooms,
workshops, kitchen, library, auditorium, music hall and drama studio.

## Doors

Placed by `PlaceDoor`: the generator looks for the wall a room shares with its corridor (or a named
neighbour) and puts the door near one end, alternating, so no two doors collide. Kinds: single (1.1 m),
double (1.6-2.2 m, labs and halls), external (heavy, exits), automatic (the lobby's glass doors), opening
(no leaf: corridor to corridor, courtyard to plaza), gate (the street). `ValidatePlan` checks every door lies
on a wall both rooms share.

## Numbers (from `school-sim-cli --stats`)

764 rooms (737 indoor, 27 outdoor zones), 157 corridor segments, 863 doors, 10 staircases, 2 elevators,
67 000 m2 of indoor floor.
