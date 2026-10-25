# Stories

Each story is a small project that shows one language or engine feature. The
stage-one compiler is checked against them one at a time, as Storybook shows
one component per story. Every story compiles on its own and is played by its
`content/scenarios/Story.kdl`.

`python3 tools/test_policy.py tool game-equivalence` rebuilds every story with
a reference and a candidate compiler and compares the two artifacts'
declarations and per-step play (see `docs/testing.md#game-equivalence`). Both
compilers are stage zero until stage one compiles a story.

The stories are listed in the order stage one is expected to reach them.

| Story | Shows |
| --- | --- |
| [Counter](Counter/content/behaviors/Counter.chibi) | Behavior state that survives between steps, written to a component field |
| [Motion](Motion/content/behaviors/Drift.chibi) | Vector arithmetic on components, scaled by the step's duration |
| [Controls](Controls/content/behaviors/Pilot.chibi) | Held movement and a pressed action, replayed from recorded input |
| [Results](Results/content/behaviors/Fold.chibi) | Member procedures with one result and with named results |
| [Follow](Follow/content/behaviors/Trail.chibi) | Reading another entity's component through a scene global |

To add a story, make a project here with one scenario named `Story.kdl`. Add
its name to `story_names` in `test/tools/game_equivalence.c` and a row above.
Format its assets with `chibi_ctl --format`.
