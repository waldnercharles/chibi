# Tetris

A complete guideline Tetris written only as Chibi assets: one behavior in
`content/behaviors/Tetris.chibi`, KDL prototypes and scene, an input map and an
RML heads-up display. It has SRS rotation with wall kicks, a 7-bag, hold, a
ghost piece, lock delay and guideline scoring. `test/tools/validate_project.c`
plays it in `tetris_example_plays`.

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
