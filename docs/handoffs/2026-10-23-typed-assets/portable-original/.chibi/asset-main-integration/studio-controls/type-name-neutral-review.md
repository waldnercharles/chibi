# Typed decoder type-name assignment: independent source review

Reviewed removal from Studio `src/editor/collab/actions_graph.c`, frozen SHA256 `233e86db49b97f118894ccc13d6f5ec8559e71a4953b65d9b760d7e2d5da8ec8`. No actionable counterexample found for the accepted scalar/plain-struct/ARRAY/SET contracts. No edits to production, builds, or test executions were performed in this review.

- `types/value_document_read.c:339` initializes scalar metadata in `read_single`; lines 355–357 copy a provided type name into reader-owned storage before decoding its payload. `PIN_STR` retains that initialized metadata. Plain structs, including null struct defaults, retain it as well; `read_struct` preserves `out->type_name` when constructing its result.
- `read_typed` owns collection declaration names. `read_array` owns both the returned header name and its array element name; element reads preserve it. SET uses the same reader and restores the declared SET container. Nested collection documents are rejected by this collaboration producer's single-element contract; their shared reader also preserves its owned declaration metadata.
- `pin_value_copy_to_graph` uses `pin_value_snapshot_to_arena`. `snapshot_value` copies the type name into graph storage, snapshots text/struct/array payloads, and reindexes SET snapshots before the temporary decode arena and JSON document are freed. Accepted values carry no authored record declaration in this adapter.
- Direct scalar actions outside the shared document branch retain their JSON-borrowed type name until the synchronous graph setter snapshots it; the removed assignment never participated in that path.

The explicit `decoded.type_name = type_name` assignment repeated the shared typed decoder contract and replaced an already-owned name with a temporary borrowed name immediately before another owned copy. Removing it preserves behavior and keeps the decoder authoritative. The final graph setter owns the destination metadata and bytes.

Evidence classification: the parent executed the graph-type-name control and it passed all 261 assertions (exit 0, control binary `725eaa53b99d4fdfa97e463e44d8269fff6f07d90307d9fdc3216400b112cc48`). This is a neutral control establishing redundancy in the current typed path, not fail-before evidence. Keep its receipt unchanged. The five independent negative controls for codec rejection, history byte decoding, checkpoint before/after validation, and batch preflight remain separate expected failures; no reruns are requested by this review. The old prepared plan's source hashes intentionally no longer match the streamlined graph file.

The parent's post-removal model rebuild/recheck supplies subsequent execution evidence. This source review claims none.
