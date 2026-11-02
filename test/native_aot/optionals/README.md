# Native optional values

`sh test/native_aot/optionals/run.sh /path/to/chibi /path/to/llvm [legacy-chibi]`
checks ten optional value outcomes at O0/O2 and compares the same sources with
an optional legacy runner. It also executes the original authored tagged-union
and named-struct-result regressions without changing their source.

Guarded union matching projects Some while retaining the optional's identity
for reassignment and nil checks. Argument and return contexts use that same
projection. Separate programs check the original diagnostics for unguarded
record access, union switches and union tests. The string-union projection and
existing force-unwrap ownership programs run with poisoned frees and balanced
allocation checks at both optimization levels. This does not admit new union
array payloads or new pointer forms.
