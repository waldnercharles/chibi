# Space Cadet pinball

A playable 2D table inspired by Space Cadet: charged plunger, two physical
flippers, bumpers, slingshots, target lanes, three balls, scoring and rank bonuses.
The artwork and mission rules are an approximation of the Windows game.

From the repository root:

```sh
CHIBI_EDITOR_SOCKET=/tmp/space-cadet.sock build/chibi --project-root projects/SpaceCadet
```

The startup scene is `content/scenes/SpaceCadet.json`. Press Play and click the
Scene viewport to give it keyboard focus.

| Control | Action |
| --- | --- |
| A / L | Left / right flipper |
| Space | Hold to charge, release to launch |
| X | Nudge |
| R | New game |

Bumpers score 1,000 points and targets score 5,000, multiplied by rank. Eight
scoring contacts complete a target-practice mission, award a bonus and increase
rank. The scene uses the renderer's Scene display mode for flat table colors.
There is no tilt penalty, multiball or persistent high-score table yet.

All game assets were created or edited through the live `editorCommand` tool.
The worktree also extends the shared command palette for project and asset
creation, semantic graph edits, startup scene selection, held input and capture.

Live checks covered both flippers, charging and launch, collision scoring,
drain/ball reset and new-game reset. Focused MCP, authoring and native editor
tests cover the tooling. The broad `make test` run reaches two failing
input-binding checks that also fail in the original checkout's existing build.
