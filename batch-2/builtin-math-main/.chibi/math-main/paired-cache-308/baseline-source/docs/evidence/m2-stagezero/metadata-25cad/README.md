# Bounded 25cad metadata validation

Source 25cad452ae87beb899d1f2b43bb30a10c09af498, tree e6633caa3bd369eb4ed54ec5e1a39f7dced27931.
This evidence is separate from frozen 8d5 and d459.

## Supported full build: failed, preserved

Normal CMake test_language/chibi-compiler-host build, two jobs, mandatory assets
enabled, exited 2 in 12.740s. Before the asset generator ran, build-host compiler
loading tried supported recovery from the retained 8d input. It compiled current
sources, loaded that result, compiled them again and rejected byte inequality:
`the recovered compiler compiles the sources to 13673789 other bytes`.
Neither rejected intermediate was written. This is not a capacity failure.
Changed serialized instruction origins are a reviewed hypothesis, not proven
byte analysis or evidence of convergence. No repeated recovery, altered check,
new seed writer, public bootstrap/pin update or PR688 adoption followed.

## Isolated current-source fixtures: passed

The current C fixture and exact embedded current Chibi sources were compiled
and linked using the existing CMake commands and unchanged native objects.
`src/` and `sdk/` are byte-identical between d459 and 25cad. The only changed C
fixture was replaced; original link inputs were reused. Source inventories,
input/driver hashes and commands are recorded in source-bound.json and
fixture/results.json. The linked old asset artifact is unused by these selected
compiler tests. These results do NOT pass the failed default asset/fullbuild gate.

- Plain compiler: 2 cases / 16,731 assertions, including the prior duplicate-watch
  failure and direct/grouped ~0 fixed literal -1 cases.
- Plain typed compiler: 1 / 5,011, including the prior missing inner-expression point.
- Compiled compiler: 1 / 11,833.
- Compiled typed compiler: 1 / 4,411.
- Debug-free compiler: 1 / 2,100.

Five selectors, six cases, 40,086 assertions passed. Existing numeric values,
diagnostics and evaluation/effect oracles were retained.

## Negative control: intended failure

Removing only FoldStep's narrow NumberLiteral watch compiled and linked
successfully. The unchanged C fixture then failed specifically at `folded
complement retains its watch`: reference watches at 40..42 and 77..79 were absent
from the authored result. The remaining native-assert case passed. Metadata
comparison detects the loss before emitted execution of that bad sample; do
not claim both emitted backends execute it. Base fixture/header/driver hashes
were verified unchanged afterward. Raw logs and exact production.patch included.

## Pending coordination

The next proposed supported check is the existing current-source bootstrap
fixture with CHIBI_BOOTSTRAP_SEED unset, checking self-reproduction without
writing a seed. It has not run at 25cad. Coordinator authorization remains pending
before another bootstrap/generation attempt. Current-source seed capture,
production writer, recovery/fixedpoint, default assets and publication gates
remain unpassed. The separate four clean-HEAD prepared20 failures also remain.

Independent read-only review verified all five passing selectors, the specific
negative-control failure, log/driver hashes, original compile/link flags and
inputs, unchanged native sources, all14 embedded source arrays against the
current source view, and unchanged baseline driver. It approves this as a held
source/test checkpoint, not full build or atomic integration acceptance.
