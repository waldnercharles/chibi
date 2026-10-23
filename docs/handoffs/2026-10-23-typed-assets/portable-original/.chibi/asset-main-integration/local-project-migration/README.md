# Local project migration source map

Source inspection only. The projects and installed SDK have not been changed or executed.

## These two local projects

Terrain Lab: all 19 non-cache files exactly match the pinned engine fixture at d0de4a84ecf50badcab661411470bdf6f3da5fa2. All have corresponding files in the typed worktree's test/fixtures/terrain-lab. Explorer.chibi, QuarryCamera.chibi and QuarryShapes.chibi are byte-identical there. Once the paired SDK is eligible, make a separate candidate from that typed fixture, compare the saved local-file hashes again, then validate the new candidate. Preserve the old directory and SDK until successful. This is a candidate recipe, not a completed migration.

Starter: the only two non-cache files describe New Project and an empty Start scene. Current chibi_project_create (src/assets/project.c:106) creates that shape in a new directory; the public new_project operation calls it (src/automation/controller.c:6968). Request the existing name New Project. Compare the evaluated new manifest and empty scene with the saved old values before any replacement.

## General modified-project path

There is no retained ready legacy converter. docs/typed-assets/PLAN.md:120 records the temporary utility policy; line 130 records removal. Old SDK asset_text_read (old src/support/asset_text.h:14) is the decoding boundary. Keep the legacy SDK in a separate process from the new SDK. Export scalar kinds, object/array order, exact signed and unsigned values, finite-double bits, and length-delimited UTF-8. Plain JSON without type metadata is insufficient: old asset_json.c:16 checks unsigned before signed and reparses positive signed integers as unsigned.

Reconstruct a new owned av_value tree or invoke project:asset_compiler:1:Write:callable with the eight typed preorder columns. Current src/authoring/asset_compiler_host.c:1079 defines column construction; asset_source_write at 1158 invokes the authored writer. tools/compiler/asset_source/Write.chibi:453 owns generation of package asset / import game:assets / public Build returning data.Encode. Exact Value.Int/UInt/Real constructors are in packages/game/assets/Assets.chibi:7; public family constructors in Schemas.chibi:17. Preserve extension fields. Reject unsupported NUL, invalid UTF-8, nonfinite values or source resource limits with diagnostics; do not silently change data.

The root is project.chibi, version 33 (src/assets/project.h:7). Rename data files and rewrite actual path references such as start_scene. Preserve asset names and name-based includes/prototype/behavior references; use family-aware traversal rather than global replacement. Executable behaviors/libraries/systems remain code and require separate compiler-aware changes only if their syntax needs migration. No legacy parser is required in the new product.

## Pending evidence

Actual conversion or candidate creation, value comparisons, project validation, launcher and Studio use against the eligible paired SDK, and final installed SDK currency remain pending. No project edits, builds or tests were performed for this source map.
