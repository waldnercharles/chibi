# Try Chibi: run, reload, find gaps

Sections 1-3 and 5 ran on `build-release/sdk/chibi` from a clean checkout of main 8337536bb;
section 4 ran on the #842 branch build (not yet on main). macOS arm64, 2026-10-29.

## 1. Build the `chibi` command

From the engine checkout:

```sh
make build-release    # writes build-release/sdk/chibi; 157 s on a cold checkout here
```

Use `build-release`. Plain `make build` failed twice on a cold clean main (774 s, 839 s):
the `asset_compiler_packaged.c` step hit its 600 s limit ("build process timeout").
Reported; not fixed. Below, `chibi` means `build-release/sdk/chibi`.

## 2. Hello World

```sh
mkdir -p /tmp/try && cd /tmp/try
cat > Hello.chibi <<'EOF'
package hello

Main :: () {
    print_string("Hello, world!")
}
EOF
chibi Hello.chibi        # same as: chibi run Hello.chibi
```

`Main` takes no arguments and returns nothing or an `int` exit status (0..255).
Every source needs a `package` line. Arguments: `chibi Hello.chibi -- Ada`, read with
`application.arguments()` (index 0 is the source path).

## 3. A directory: `chibi .`

Use a new, empty folder: every `.chibi` file in it joins the package.

```sh
mkdir -p /tmp/try-dir/greet && cd /tmp/try-dir
cat > Main.chibi <<'EOF'
package app

import greet "project:greet"

Main :: () {
    print_string(greet.Message("Ada"))
}
EOF
cat > greet/Greet.chibi <<'EOF'
package greet

pub Message :: (name: string) -> string {
    return format_text("Hello, {}!", name)
}
EOF
chibi .                  # prints: Hello, Ada!
```

A folder with `project.kdl` runs as a game instead (section 5).

## 4. Edit and Continue (needs PR #842)

```sh
mkdir -p /tmp/try-reload && cd /tmp/try-reload
cat > Hello.chibi <<'EOF'
package hello

Main :: () {
    print_string("started")
}

Frame :: () {
    ticks: retain int = 0
    ticks += 1
    if ticks % 20 == 0 {
        print_string(format_text("tick {}", ticks))
    }
}
EOF
chibi --watch Hello.chibi     # or: chibi --watch .
```

`Main` runs once; `Frame` runs about 60 times a second. Change `tick` to `tock` and save.
Observed: `started`, `tick 20` ... `tick 100`, `tock 120` ... with the counter never
restarting; a syntax error printed one diagnostic while the old code kept ticking.

- A save patches the running code between frames; `Main` is not called again.
- Kept: `retain` values (converted if the type changed). Not kept: ordinary locals.
  Removing `Frame` drops its state. Changed record layouts follow `docs/managed-pointers.md`.
- An invalid edit prints `file:line:col: message` and the old code keeps running; a runtime
  failure pauses until the next good save.
- Triggers: a saved change to the file, or for a directory its siblings and `project:` packages.
  A loop written inside `Main` is not patched; put per-frame work in `Frame` (nonzero `int`
  return stops). Game reload is Studio. Design: `docs/edit-and-continue.md`.

## 5. Something visual (needs a window or `--hidden`)

Create `my-game/` from `docs/guide/toolchain.md#project-files` (format 39). Then:

```sh
chibi fmt --check my-game && chibi check my-game && chibi build my-game
chibi run my-game                       # hold A / D to move the blue square
chibi run my-game -- --hidden --frames 3 --size 320x180 --capture frame.png   # no window
```

Player options go after `--`. Verified: fmt, check, build, hidden capture; A/D untested by hand.

## 6. Gaps and rough edges hit

- Syntax errors say only `Unexpected syntax` at line:col. These guesses all fail that way:
  `import project:greet` (use `import greet "project:greet"`), `1..=15` (use `1..15`),
  `Point{ x: 3 }` (use `p: Point = {x: 3}`).
- `chibi .` over files with different `package` lines fails with `Package sibling headers
  differ`, without naming the files.
- Standalone programs have no stdin; `application.*` offers arguments, environment,
  stdout/stderr, now, wait. Interactivity today means a game project.
- The player option is `--hidden`, not `--headless`. Authored record fields are scalars only.
- Cold `make build` times out; cold `make package-web-sdk` fails (both reported).
- Not verified: Linux, Windows, `make install`, Studio.
