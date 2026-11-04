# Native default initializers: unvalidated diagnosis

Source-only diagnosis and proposed correction, not an implementation or passing build. Baseline revision: `5a6d81ce3233f448aa6d5752e2427320ee117b2f`. This public technical summary is a derivative of the original diagnosis and has its own hash.

## Observed failure and source contract

The preserved host-build diagnosis reports 19 strict-GCC `-Werror=pedantic` diagnostics from `text_definition.c`: 17 expansions from `text_definition.h:29`, one from line 61 and one from line 72. `src/capabilities/definition.h` expands `NATIVE_PORT` and `NATIVE_CSTRING_PORT` into static aggregate initializers containing `.default_value = VALUE`. The inspected values are parenthesized `pin_value` compound-literal expressions, which are not ISO C constant initializers for a static aggregate member. This snapshot does not reproduce or independently validate the reported compiler outcome.

The source audit found 30 `((pin_value){` producer forms across ten live runtime definition headers: arithmetic 1, arrays 1, comparison 1, conversion 1, core 7, math 5, selection 4, sets 1, text 3, vectors 6. Forwarding includes `NATIVE_PORT_FLAGS` and math/vector macros. The narrow host recipe includes the text and arrays definition units, but changing the shared consumers affects all ten packs.

The same row values also have expression consumers, including `archive/src/graph/model/native_definition.h`, with further forwarding in archived flow/component nodes. Archived authoring, input, and scene-flow custom descriptor consumers also use `.default_value = VALUE`. The header is listed among SDK headers in `tools/graph/engine/Targets.chibi`. Changing producers to initializer tuples would therefore alter the source-level macro contract and require consideration of these other consumers.

## Proposed adapter, not applied or compiled

A consumer-only adapter could retain the existing producer expression shape while extracting braces for the two static descriptor consumers:

```c
#define NATIVE_DEFAULT_INITIALIZER(...) NATIVE_DEFAULT_INITIALIZER_TYPE __VA_ARGS__
#define NATIVE_DEFAULT_INITIALIZER_TYPE(TYPE)
/* In NATIVE_PORT and NATIVE_CSTRING_PORT only: */
.default_value = NATIVE_DEFAULT_INITIALIZER VALUE
```

Manual token tracing suggests `NATIVE_DEFAULT_INITIALIZER ((pin_value){ .type = PIN_I32, .container = PIN_CONTAINER_ARRAY })` becomes `{ .type = PIN_I32, .container = PIN_CONTAINER_ARRAY }`. This is not preprocessing or compilation evidence. The proposal relies specifically on the current `((pin_value){...})` shape and discards its type token. Type-checking implications, alternate expressions, parentheses and types remain open compatibility questions. No unrestricted source API compatibility is claimed.

The intended runtime cost is zero added instructions, allocations, fields, branches or copies. Static const tables, designators and vector-address relocations should remain unchanged, but neither descriptor equivalence nor those cost properties have been measured.

## Unexecuted validation plan

- Independently review the source/API contract and freeze baseline/candidate inputs and the strict toolchain recipe before execution.
- Use the actual failed recipe for real text and arrays translation units, preserving GNU23, O3, warnings and `-Werror` policy. Require the baseline's known nonconstant-initializer failure and candidate object creation. Commands have not been extracted or executed for this proposal; a toy preprocessor example is insufficient.
- Design and review a bounded descriptor test for zero and typed scalar defaults, array type/container tags, nonzero f32/f64 values, owner/reference flags, vector defaults, C-string opt-in and `NATIVE_PORT_FLAGS` forwarding. Exercise all ten live producer packs and at least one expression consumer. Compare meaningful fields and referenced vector values, not padding or pointer addresses.
- Measure expanded row counts, baseline/candidate outcomes, descriptor-data equivalence and compatibility. Keep existing array/cookie fixes and unrelated runtime/compiler code unchanged.

The exact fixture remains unwritten. No fix, compiler/preprocessor run, test, full host build, Windows validation, or merge-readiness claim is provided by this checkpoint. The separate union native16-byte investigation is outside its scope.
