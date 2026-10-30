# Compiler and language

## Compiler

Treat each compiler representation (tokens, nodes, types, IR, diagnostics, interned names) as a dataset with a count, a lifetime and consumers.

- **String-keyed nodes.** Is a node kind compared by string, or a field found by scanning children by name? *Fix:* interned tags and fields addressed by index.
- **Reaching back.** Does a phase reach back into source text or tokens outside error reporting, or re-parse a fact an earlier phase knew?
- **Locations.** Are source locations computed eagerly, or stored as raw offsets with mixed meanings? The cheap form is a token or node index resolved lazily when a diagnostic needs it.
- **Resolution.** Are names, types, layouts and constants resolved on demand by one resolver with "resolving" and "resolved" states, which gives cycle errors at the declaration and the emission order for free? Or is there a separate dependency-order pass, or several binding passes that each walk the tree?
- **Expected types.** Is the expected type passed down during checking, with constants folded on the checker's own operands, rather than solved by a constraint system or re-evaluated in a second folder?
- **Error recovery.** After a parse error, does the parser resynchronize at statements and top-level declarations, keep the signature resolvable when a body fails, and suppress cascading diagnostics? Tools such as navigation need this; a batch build may stop after the failing pass.
- **Throughput metric.** Does a front-end change report tokens per second and bytes per phase on a calibrated corpus?
- **Literals.** Do number-literal parsers check the range before computing `10*r + d`? Are float literals correctly rounded, identically in both stages?
- **Positions.** Are source positions emitted only when they change?
- **Rule tables.** Are simplifications a table of rules keyed by opcode, and could common-subexpression lookup walk a per-opcode chain instead of a hash map?
- **Interning.** Are names copied per node instead of interned once?
- **Consumer migration.** Does a representation change migrate every consumer in the same change: both stages, dumps and tests?
- **Twin implementations.** Does the change add semantics to both the C and the Chibi implementation? Add the feature to the surviving one, and delete its twin once the survivor covers it.
- **Determinism.** Does the order of diagnostics, symbols or generated code depend on hash iteration or pointer values instead of source order? Bootstrap reproducibility depends on this.
- **Serialization.** Can the representation be written and read back without pointer fixups, for caches and Edit-and-Continue?
- **Generators.** Does a parser change bring in a generator, or a dependency on generated tables, where a hand-written recursive-descent case would do?
- **Diagnostic placement.** Is a diagnostic produced far from where the relevant information exists, so the information has to be reconstructed?
- **Rule duplication.** Is one language rule encoded separately in several phases?
- **Shared subset.** Do real sources use only syntax that both stages accept (`docs/language-friction.md`)? Does `Build.chibi`, and other build-host source, parse with the build host's own syntax and the stage-one tokenizer (`docs/language-friction.md`)?
- **Phase-local data.** Is data that belongs to one phase retained globally, beyond its lifetime?

## Language design

A feature passes the gate in AGENTS.md. For a syntax or semantics change, check each part of it:

- **Named program.** Is a named program blocked without the feature? Have inference and existing forms been ruled out in writing?
- **Hidden cost.** Does the feature hide an allocation, a branch or control flow?
- **Second way.** Does it add a second way to do something, or a dialect?
- **Parsing.** Does it parse unambiguously with the stage-one tokenizer? Does it scan and grep well?
- **Interactions.** Has its interaction with each existing feature been checked (optionals, unions, generics, units, behaviors)?
- **Trial.** Was it tried on real code, with its uses counted, and with a plan to remove it if it doesn't pay off?
- **Specialization growth.** Does the feature multiply specializations or instantiations, and is that growth tracked against compile time?
- **Desugaring.** Could the feature desugar into existing forms in far fewer lines than a new compiler path?
- **Removal.** Is each existing feature's use counted, and is a feature that real programs don't use, or that costs more upkeep than it saves, removed?
- **Library form.** Could a library form express it instead?
- **Build-mode dependence.** Does its behavior depend on the build mode (overflow, division, asserts)?
- **Diagnostics.** Does a new diagnostic claim knowledge the compiler can't have? Should it be an error rather than a warning?
- **Recorded evidence.** Is the evidence for the gate (the program, the alternatives ruled out, the use count) written down with the change, along with how other languages handle the same case?
