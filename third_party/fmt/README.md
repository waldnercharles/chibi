# Vendored formatter notices

This is the C formatter from STC, not the unrelated C++ fmt library.
The existing `fmt.h` and `fmt.c` headers identify STC `include/c11/fmt.h`,
MIT licensing, `(c) operamint 2022-2026`, and an intermediate GeometryWars
fork that split the source and added positional formatting. Those headers
and the local-modification descriptions are retained unchanged.

`LICENSE` restores the complete upstream MIT permission and disclaimer text,
including its original `Copyright (c) 2026 Tyge Løvset` line, verbatim from
[STC commit d3d41117247a6ff33c8cc8080af47f6ea2a8febb](https://github.com/stclib/STC/blob/d3d41117247a6ff33c8cc8080af47f6ea2a8febb/LICENSE).
The corresponding [formatter header](https://github.com/stclib/STC/blob/d3d41117247a6ff33c8cc8080af47f6ea2a8febb/include/c11/fmt.h)
identifies version 2.4 and the operamint copyright notice.

This is a dated upstream notice reference predating the Chibi import, not
a claim that this commit is the exact source revision of the intermediate
fork. The import did not record that fork's URL or commit. Its exact revision
and any additional fork-specific notices remain unverified; this restoration
does not invent an additional permission grant or remove existing attribution.

Keep this license and the original source notices with redistributed copies.
