# Chibi licensing

Chibi's standalone compiler and tools use the GNU General Public License,
version 3 only (`GPL-3.0-only`). Its reusable libraries, embeddable runtime,
and player use the GNU Lesser General Public License, version 3 only
(`LGPL-3.0-only`). The full texts are in [COPYING](COPYING) and
[COPYING.LESSER](COPYING.LESSER). LGPLv3 incorporates GPLv3 together with
its additional permissions; both texts are needed.

## File scope

The following assignments apply to first-party Chibi material. Existing
file-specific license notices, third-party licenses, and third-party asset
terms take precedence; this document does not relicense material owned by
others. Preserve their copyright and license notices when redistributing it.

Paths ending in `/` include their contents recursively. More-specific rows
in the table take precedence over broader rows.

| Files | License |
| --- | --- |
| First-party files not covered by another row | GPL-3.0-only |
| `src/`, except the tool files listed below | LGPL-3.0-only |
| `include/`, `packages/core/`, `sdk/`, `cmake/` | LGPL-3.0-only |
| `CMakeLists.txt`, `Makefile` | LGPL-3.0-only |
| `tools/reflect/`, `tools/docs/`, `tools/collab/config.js.in` | LGPL-3.0-only |
| First-party material in `docs/` and `test/`, except the three listed MIT snippets | LGPL-3.0-only |
| The three explicitly marked snippets in `docs/code-language.md` listed below | MIT |
| `src/toolchain/` except the four shared library files below | GPL-3.0-only |
| `src/toolchain/compiler_json.c`, `src/toolchain/compiler_json.h`, `src/toolchain/compiler_syntax.c`, `src/toolchain/compiler_syntax.h` | LGPL-3.0-only |
| The `src/host/` tool files listed below | GPL-3.0-only |
| `tools/compiler/`, including its bootstrap compiler, and `tools/toolchain/` | GPL-3.0-only |
| `third_party/` and other material with existing third-party notices | Their existing licenses; not relicensed here |

The GPL host-tool files are:

- `src/host/build_catalog.c`
- `src/host/build_host.c`, `src/host/build_host.h`, `src/host/build_host_internal.h`, `src/host/build_timing.h`
- `src/host/build_io.c`, `src/host/build_io.h`
- `src/host/build_main.c`
- `src/host/build_values.c`, `src/host/build_values.h`
- `src/host/chibi_main.c`, `src/host/compile_main.c`
- `src/host/export.c`, `src/host/export.h`
- `src/host/process.c`, `src/host/process.h`
- `src/host/serve.c`, `src/host/serve.h`

The LGPL library scope deliberately extends beyond `src/runtime/`. It
includes execution backends, reusable language and authoring libraries,
native capabilities and bindings, support/types/ECS, rendering, UI, shaders,
public headers, core Chibi packages, player implementation, and web-player
resources. The standalone compiler and tool executables remain GPL when
they combine their GPL tool code with these libraries. Keep this boundary
consistent when adding dependencies: a runtime or reusable-library target
must not silently acquire GPL-only implementation code.

## MIT copy-paste examples

Only the code inside the following three fenced blocks in
`docs/code-language.md` is additionally offered under the MIT license in
[COPYING.EXAMPLES](COPYING.EXAMPLES). Each block contains an MIT SPDX notice
and a code comment with its exact identifier:

| Identifier | Code |
| --- | --- |
| `chibi-mit-example: damage-functions` | `Example` library defining `DamageFor` and `DamageAfterArmor` |
| `chibi-mit-example: reference-parameter` | `Example` library defining `AddScore` |
| `chibi-mit-example: weighted-total` | `Example` library defining `WeightedTotal` |

Each identifier applies only to its enclosing fenced code block, not the
surrounding prose or other code blocks. This list is exhaustive;
adding an MIT marker elsewhere does not extend this grant. These snippets may
be copied and modified in proprietary application code under the MIT terms.
Retain the copyright and permission notice required by that license.

No tests, runtime or compiler implementation, generated Chibi scaffolding,
core library, asset, third-party material, or other example is included in
this carveout. Using the snippets with Chibi does not remove the runtime's
LGPL conditions.

## Games, applications, and generated code

Writing a game or application in Chibi does not by itself license that
application under the GPL or LGPL. Independently authored application code
may remain proprietary. Combining it with LGPL Chibi components requires
compliance with the LGPL; it is not an exemption from the runtime's terms.

Chibi compiler output can contain both application-origin code or data and
Chibi-origin material. In particular, the execution backend and reflection
generator emit Chibi implementation scaffolding, and core packages may be
compiled into an application. Those Chibi portions retain their LGPL terms;
application-origin portions retain their own terms. This is not a blanket
permission to remove notices or treat all generated source as unrestricted.
Header material is also subject to LGPL section 3 where applicable.

Outside the three MIT snippets listed above, the same distinction applies
to examples: merely using the language is not copying Chibi, but copying or
adapting Chibi examples retains the license of those portions. Adapting a
supplied LGPL game example can therefore require source for its copied and
modified LGPL portions; it is not the same as independently writing a game
that only uses Chibi interfaces. Third-party game assets retain their own terms.

Changes to Chibi's compiler, libraries, runtime, player, or other copyleft-covered
code remain subject to their applicable copyleft license when distributed.
Changing the player is not the same thing as writing an independent game
that uses its interfaces. Private use does not itself require publishing
changes, and these licenses do not require every independent implementation
of the language to use the same license.

See [Distributing Chibi and applications](docs/distribution-licensing.md)
for the distribution boundary and the remaining packaging work. That guide
summarizes the standard licenses; it does not add a custom exception or
replace their terms.
