# Queued warm-cache coverage

These are source fixture requirements. No candidate code has executed.

- Positive package/relocation case uses a private warm graph cache seeded with all four excluded native leaves carrying distinct bytes. Package succeeds; cache bytes stay unchanged; strict installed semantic inventory has no excluded paths.
- An excluded cache leaf aliases an external sentinel. Package still omits it; neither the link nor sentinel changes. No excluded leaf is followed for comparison/copy.
- Existing selected authored source missing, bytes changed, or unrelated regular file added to compiler cache: package rejects as stale before publishing a prefix. Removing the negative restores expected membership.
- Nonexcluded compiler parent links point into a protected source snapshot: package rejects, leaves source byte identity unchanged and creates no output/stage.
- Independent semantic fixture oracle compares exact selected membership and file bytes, not only counts. The installed side is strict and cannot contain excluded extras.
- distribution_source snapshots raw compiler source, not a build cache reconstructed as authored input.
- Keep frozen47bbe three opt-in ProjectTools cases and typed reviewed Support/source-root/explicit-false/file_io compile-command oracle (acb83337); no fixed5/7 count oracle.
- Future admitted execution must also cover ordinary warm CMake/Graph rebuild, clean full-SDK installation, prepared Project host/capability/readiness, relocation identity, and runtime asset-source consumer behavior.
