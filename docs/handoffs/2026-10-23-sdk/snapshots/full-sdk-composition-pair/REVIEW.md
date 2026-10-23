# HOST/2 SDK source review

The two-file patch adds a prepared `publish_pair` capability proof and strict HOST/2 witness fixtures on top of the reviewed warm-cache SDK draft. Independent and coordinating exact-patch source reviews found no material issues. `root-source-review.json` records the later clearance separately from the preserved frozen manifest.

The SDK generates a separate probe, removes its source before prepared execution, and verifies the resulting files in the parent. It covers all four combinations of prior destination presence and requires clean status 0, exact new bytes, consumed sources, and no leftover files. The new native call appears only in generated text, so the existing SDK `Verify` entry keeps its previous binding requirements.

The witness has ten metadata rows and six ordered passed checks, ending with `publish-pair`. PREPARE/1, its sixteen scalar fields and four policy outputs, and the Inspect response format remain unchanged. The C fixtures reject old, missing, duplicated, extra and reordered witness rows after updating the inventory digest; they then restore the original files and check the original identities.

Source checks completed: patch application against the exact warm baseline, whitespace validation, C formatting, and independent source review. The reviewed warm patch and manifest and the frozen SDK index remain unchanged.

Execution is held while CI owns validation. This draft has not been built, exported, run, imported, installed or published. No runtime witness exists from this work. Adoption also requires the reviewed native binding 941 implementation and ABI, the corrected Project publisher and HOST/2 reader with exact source hashes, native fault checks for statuses 1/2/3, and actual prepared, installed and relocated SDK validation.

`manifest.json` records exact file and patch hashes, baseline dependencies, review scope and the remaining gates. `source.patch` is the minimal delta from the warm SDK/C sources; it does not include or replace the six-file warm patch.
