# Schedule

The day is a list of phases; each starts when the previous one ends (`Simulation/Schedule.hpp`).

```
# time  kind       lesson  name
07:30   arrival    -       Doors open
07:50   passing    1       Go to lessons
08:00   lesson     1       Lesson 1
08:45   passing    2       Change of rooms
...
12:35   lunch      -       Lunch           (two waves: checkpoints at the start and halfway)
15:15   dismissal  -       End of school
15:40   clubs      -       After-school clubs
17:30   closing    -       Closing
19:00   night      -       Closed
drill   10:20 wed                          (fire drill at that time on Wednesdays; omit the day for every day)
```

Kinds: `night arrival passing lesson longbreak lunch dismissal clubs closing`. Lessons ring the bell at their
start, as do passing phases, the long break, lunch and dismissal. A passing phase carries the number of the
lesson that follows it so people set off towards the right room.

The file `Content/Data/schedule.cfg` is loaded at start-up (`--schedule FILE` for another one); when it is
missing or invalid the built-in copy (`BellSchedule::Default`) is used and the log says so. `Validate` refuses
overlapping phases, lesson numbers out of order, no lessons, a drill outside the day.

Time controls in the app: `Space` stops the clock, `1`..`5` select x1, x2, x5, x10, x30, `N` jumps to the
next lesson (`SkipToNextPhase`), `--time HH:MM` starts there, the script op `time:HH:MM` jumps anywhere
(backwards replays the day from midnight: everything is deterministic, so the state is the same as before).

The weekly timetable per class (`ClassGroup::timetable[day][period]`) is generated with the population from
the seed; `Population.periods` is the number of lessons the schedule has.
