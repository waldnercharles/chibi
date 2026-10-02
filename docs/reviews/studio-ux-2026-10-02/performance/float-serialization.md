# Float serialization

Authored KDL now removes redundant digits such as `0.20000000000000001` while keeping ordinary quantities readable. This change follows the laws of UX guidance on reducing visual noise and preserving familiar representations.

## Numeric contract and validation

- `av_real` retains the exact double value, including signed zero. Serialization chooses the minimum significant-digit precision that parses back to the same double. Legitimate values such as `0.2000000000001` are retained.
- `av_float` represents a native float using a canonical decimal promoted to double. Casting that decimal back to float preserves the original float and signed zero. The stored double need not equal the exact binary widening of the original float. Existing callers persist native float fields; double graph pins use `av_real` separately.
- Scientific notation is expanded to fixed decimal for decimal exponents −4 through 15. Expectations include `80.0`, `320.0`, `0.2`, `0.0001`, and `1000000000000000.0`. Outside that range, values stay compact: `1e-05` and `1e+16`.
- The isolated actual KDL test suite passed with zero failures under AddressSanitizer and UndefinedBehaviorSanitizer. Cases include adjacent floating-point values, the smallest subnormals, maximum finite values, negative zero, cloning, parsing, and canonical value equality.
- A deterministic xorshift sample (seed `0x27649273645`, shifts 13/7/17, 10,000 f32/f64 pairs with nonfinite values skipped) passed **19,951 finite f32/f64 bit-pattern round trips**, comparing the original native scalar and its sign after writing and parsing.
- The final asset migration covered 102 KDL files. All 353 changed numeric values preserved their intended f32 bits, with zero unexpected numeric changes. See [numeric-migration.json](numeric-migration.json) for the migration evidence.

## Save-format timing

Measured on the same local machine on 2026-10-02, using the real Sandbox scene and Movement behavior from the isolated Arena2D QA workspace. Each process reads and parses its file once, then times 1,000 `av_doc_write` calls including output allocation and freeing. Each table row reports the average milliseconds per write. Disk I/O and parsing are outside the measured interval.

Both executables use the same harness, compiler, sanitizer flags, and ckdl archive. The baseline links a core archive captured before the serializer change (`%.17g` real output and direct float-to-double promotion). The final executable links the current native core archive with the fixed-decimal policy. Three paired samples were collected. The shared editor test suite was running concurrently, so these measurements include ordinary scheduling noise.

| Sample | Asset | Baseline ms/write | Final ms/write | Baseline/final output bytes |
| --- | --- | ---: | ---: | ---: |
| 1 | Sandbox | 0.0974 | 0.1098 | 3544 / 3482 |
| 2 | Sandbox | 0.0954 | 0.1050 | 3544 / 3482 |
| 3 | Sandbox | 0.0967 | 0.1095 | 3544 / 3482 |
| 1 | Movement | 0.1689 | 0.1710 | 6589 / 6589 |
| 2 | Movement | 0.1698 | 0.1695 | 6589 / 6589 |
| 3 | Movement | 0.1707 | 0.1877 | 6589 / 6589 |

Median Sandbox cost increased from 0.0967 to 0.1095 ms/write (0.0128 ms). Median Movement cost increased from 0.1698 to 0.1710 ms/write (0.0012 ms). The additional significant-digit checks and fixed-decimal formatting have a measurable, small absolute cost on these two documents. These samples bound serialization cost only; they do not establish whole-editor responsiveness, browser performance, disk save latency, or performance for every asset size.

## Reproduction

Save this harness as `float-save-timing.c`:

```c
#include "support/asset_text.h"
#include "support/text_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static double now() {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec / 1e9;
}
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  char *source = text_file_read_disk(argv[1]), error[256];
  av_doc *doc = source ? av_doc_read(source, error, sizeof error) : NULL;
  if (!doc)
    return 3;
  size_t bytes = 0;
  double start = now();
  for (int i = 0; i < 1000; i++) {
    char *out = av_doc_write(doc);
    if (!out)
      return 4;
    bytes += __builtin_strlen(out);
    free(out);
  }
  printf("%s: %.4f ms/save, %zu output bytes\n", argv[1], (now() - start),
         bytes / 1000);
  av_doc_free(doc);
  free(source);
  return 0;
}
```

Compile one executable against the baseline archive and another against the current archive:

```sh
/opt/homebrew/opt/llvm/bin/clang -std=gnu23 -I src \
  -fsanitize=address,undefined float-save-timing.c \
  /tmp/chibi-ui-qa/libcore-before-float-format.a build/libckdl.a \
  -o /tmp/chibi-ui-qa/float-save-timing-old
/opt/homebrew/opt/llvm/bin/clang -std=gnu23 -I src \
  -fsanitize=address,undefined float-save-timing.c \
  build/libcore.a build/libckdl.a \
  -o /tmp/chibi-ui-qa/float-save-timing-final
```

Run both executables against each identical source file:

```sh
/tmp/chibi-ui-qa/float-save-timing-old /tmp/chibi-ui-qa/workspace/projects/arena2d/content/scenes/Sandbox.kdl
/tmp/chibi-ui-qa/float-save-timing-final /tmp/chibi-ui-qa/workspace/projects/arena2d/content/scenes/Sandbox.kdl
/tmp/chibi-ui-qa/float-save-timing-old /tmp/chibi-ui-qa/workspace/projects/arena2d/content/behaviors/Movement.kdl
/tmp/chibi-ui-qa/float-save-timing-final /tmp/chibi-ui-qa/workspace/projects/arena2d/content/behaviors/Movement.kdl
```

The baseline archive and QA workspace are temporary artifacts. To reproduce later, preserve equivalent baseline/current builds and identical input files. Raw results from this run were also recorded in `/tmp/chibi-ui-qa/float-save-timing-final-results.txt`. The harness reports elapsed seconds directly as milliseconds per write because it performs exactly 1,000 writes.
