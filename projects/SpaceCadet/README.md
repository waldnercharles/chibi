# Space Cadet pinball

A playable 2D table inspired by Space Cadet: charged plunger, two physical
flippers, bumpers, slingshots, target lanes, three balls, scoring and rank bonuses.
The artwork and mission rules are an approximation of the Windows game.

From the repository root:

```sh
make run PROJECT=projects/SpaceCadet
```

The startup scene is `content/scenes/SpaceCadet.yaml`. Press Play and click the
Scene viewport to give it keyboard focus.

| Control | Action |
| --- | --- |
| A / L | Left / right flipper |
| Space | Hold to charge, release to launch |
| X | Nudge |
| R | New game |

Bumpers score 1,000 points and targets score 5,000, multiplied by rank. Eight
scoring contacts complete a target-practice mission, award a bonus and increase
rank. The table lighting uses the `GameHRC2D` Scene debug view for flat table colors.
There is no tilt penalty, multiball or persistent high-score table yet.
