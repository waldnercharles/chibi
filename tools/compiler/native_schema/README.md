# Native value schema fingerprints

`Fingerprints(nodes, edges)` hashes each root's complete reachable type graph.
Keys identify semantic types, independent of local table indices. Each node's
body hashes its own representation, including ordered fields and referenced
keys; the adjacency table supplies the corresponding child definitions.

Supported keys must be unique and nonzero. A root that reaches an unsupported
node has fingerprint zero. Invalid table ranges or keys reject before closure
allocation. Inputs remain unchanged. Shared children and cycles are finite;
reordering the table or inserting an unrelated type preserves existing hashes.

The compiler projects checked types into this graph in
`package_compiler/ScalarNativeSchemas.chibi`. The preparation function fills the
private `TypeDecl.native_schema` field and preserves ordinary `.layout` hashes,
type identities and existing pointer tags. Constructors initialize the extra
eight bytes to zero. The final native projection prepares schemas when an
aggregate array cell or view signature needs them. Owned aggregate array cells
use a separate tag domain containing this complete schema; primitive and string
array cells keep their established identities.

Closure uses at most 128 KiB for 1024 nodes and 16,777,216 word unions. The
canonical hash scan visits each reachable definition once per root. Compiler
unit discovery runs the reindex, unrelated-type, nested-drift, recursive-sharing
and malformed-table checks in `tests/`.

`test/native_aot/managed_pointers/SchemaBench.chibi` measures this transform on
POSIX hosts, after constructing its input. One macOS arm64 O2 measurement with
LLVM 22 produced:

| Graph | Nodes | Edges | Milliseconds |
| --- | ---: | ---: | ---: |
| Chain | 64 | 63 | <1 |
| Chain | 992 | 991 | 27 |
| Cycle | 992 | 992 | 27 |
| Complete directed graph | 992 | 984,064 | 40 |

These are measurements, not test deadlines or admission limits.
