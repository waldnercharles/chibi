# Tetris

A Tetris-style game written only as Chibi assets: one behavior in
`content/behaviors/Tetris.chibi`, KDL prototypes and scene, an input map and an
RML heads-up display. It has SRS rotation with wall kicks, a 7-bag, hold, a
ghost piece, lock delay and basic line/drop scoring. `test/tools/tetris.c`
checks play, restart, ceiling kicks and blocked hold. T-spin, combo
and back-to-back scoring are not implemented.

| Action | Keys |
| --- | --- |
| Move | Left/Right arrows, A/D |
| Soft drop | Down arrow, S |
| Hard drop | Space, W |
| Rotate | Up arrow, X |
| Rotate back | Z |
| Hold | C, Shift |
| Restart after game over | Enter, R |

From the engine checkout, after `make install`:

```sh
chibi examples/tetris
chibi export examples/tetris --output /tmp/tetris
```
