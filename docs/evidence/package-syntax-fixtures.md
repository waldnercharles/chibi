# Exact future frontend fixtures

These are approved-syntax integration inputs, not currently parsed or executed. The C `--package-graph` fixture supplies equivalent parsed/resolved descriptors directly. It does not claim parser, filesystem canonicalization, privacy, body resolution, initialization or runtime coverage. Unprefixed imports resolve relative to their declaring file; `project` is the configured content-root collection.

## Two sibling files and a separate nested directory

`app/a.chibi`:
```chibi
package app
import math "project:math"
Answer :: () -> int { return math.One(); }
```

`app/z.chibi`:
```chibi
package app
Twice :: () -> int { return Answer() + Answer(); }
```

`app/nested/n.chibi`:
```chibi
package nested
Label :: () -> string { return "nested"; }
```

`math/m.chibi`:
```chibi
package numbers
One :: () -> int { return 1; }
```

Expected graph: three packages; `a.chibi` then `z.chibi` within `app`; alias `math` exists only in `a.chibi`; `app` depends on canonical `math` directory, whose header is `numbers`. Sibling body resolution remains a later parser/semantic case. Replacing `z.chibi`'s header with `package other` must report both header locations. A second distinct directory declaring `numbers` is an ordinary-build duplicate-name error.

## Resolved identity and default alias

`app/a.chibi`:
```chibi
package app
import first "../math"
import second "project:math"
import "../math"
Answer :: () -> int { return first.One() + second.One() + math.One(); }
```

All three imports must resolve to one canonical package. Their aliases do not define package identity. The default alias is `math`, not `numbers`. Actual path normalization and allowed-root enforcement are future resolver gates.

## File-local and blank imports

`app/a.chibi`:
```chibi
package app
import lib "project:math"
import _ "./nested"
```

`app/z.chibi`:
```chibi
package app
import lib "./nested"
```

The same alias may name different targets in different files. Neither file gains its sibling's import. `_` creates an edge and no lookup binding; repeated blank imports remain edge provenance rather than duplicate bindings. Repeating a named alias within one file is an error with both import locations.

## Direct self-import

`app/a.chibi`:
```chibi
package app
import this_package "."
One :: () -> int { return 1; }
Answer :: () -> int { return this_package.One(); }
```

Expected identity/binding: one package and a self edge; the alias refers to that same package. Its public selector uses normal export rules. A package-private declaration is still callable directly from a sibling but a self import is not an escape hatch through private export checks. The private graph does not yet perform those selector/privacy checks.

## Unused cycles, including self edges

`a/a.chibi`:
```chibi
package a
import _ "."
import _ "../b"
```

`b/b.chibi`:
```chibi
package b
import _ "."
import _ "../a"
```

Expected approved contract: reject the genuine inter-package cycle even with no symbol use. Removing both cross-package imports leaves two independent self-importing packages; no duplicate package instance is created. Removing only `b`'s cross-package import leaves `a` dependent on `b`, so dependency-correct ordering is `b`, then `a`. This is not a literal simulation of the pinned queue anomaly described in the source proof.

## Invalid implicit basename

`app/a.chibi` importing `"project:math-v2"` needs an explicit valid alias such as `import math "project:math-v2"`, or an intentional `import _ "project:math-v2"`. A directory named `_` likewise cannot silently become an implicit blank import. The graph fixtures supply the resolved directory and exercise these alias outcomes.

## Unchanged runtime decisions, outside this slice

Package globals will be shared once per running world; previews are isolated. Initialization runs once at runtime start, finalization at shutdown, with dependency/file/source order and reverse finalization. Compatible edits preserve state without reinitialization; membership and incompatible-global changes initially require restart. No runtime entrypoint is present in these fixtures. Task-handle storage ownership remains unresolved and is not narrowed here.
