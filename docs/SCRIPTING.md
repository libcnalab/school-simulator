# Scripting the app

`--script "step;step;..."` drives the app without a human: tests, screenshots, soak runs. Steps run one per
frame unless they wait. Everything a script does, a player can do too; nothing here bypasses the simulation.

| Step | Effect |
|---|---|
| `wait:N` | skip N frames |
| `goto:ROOM` | stand a couple of metres inside a room, facing into it (ROOM: a code `N1.05`, a name `Main lobby`, or a type `Gym`, `Computer lab`, `Stairwell`; the largest room of a type wins) |
| `busiest:TYPE` | the room of that type with the most people in it right now (walkers passing through count) |
| `atdoor:CODE[,N]` | two metres outside the N-th leaf door of a room, facing the door |
| `pos:x,y,z` | put the feet there |
| `look:yaw,pitch` | absolute view in degrees (yaw 0 = north, 90 = west) |
| `turn:DEG`, `pitch:DEG` | relative yaw, absolute pitch |
| `walk:F,S,N` | walk with forward F and strafe S input (-1..1) for N frames; collision applies |
| `time:HH:MM` | set the school clock (backwards replays the day from midnight, deterministically) |
| `speed:F` | time scale (0 stops the clock) |
| `skip` | jump to the next lesson |
| `drill` | start or end a fire drill |
| `bell`, `announce` | ring the bell, make a PA announcement |
| `e` | use what is under the crosshair (logs what it was) |
| `map`, `debug:N` | toggle the big map; debug overlay 0-3 |
| `stats` | log the F3 statistics (including the simulation digest) |
| `shot:FILE` | save a screenshot two frames later |
| `quit` | exit |

Useful switches with scripts: `--fixed-step 0.05` for deterministic frames, `--no-hud` for clean pictures,
`--frames N` as a safety stop, `--preset`, `--time`, `--weather`, `--seed`.

Examples:

```bash
# The cafeteria at lunch, busy preset, no HUD
xvfb-run -a -s "-screen 0 1280x720x24" build/bin/cna-school-simulator --no-audio --no-hud --preset busy --time 12:40 \
    --script "wait:5;busiest:Cafeteria;wait:40;shot:cafeteria.png;quit"

# Watch a change of rooms from a first-floor corridor at x3
build/bin/cna-school-simulator --time 08:44 --script "wait:5;goto:N1.05;speed:3"
```

`tools/tour.sh`, `tools/soak.sh` and `tools/determinism.sh` are built from these steps.
