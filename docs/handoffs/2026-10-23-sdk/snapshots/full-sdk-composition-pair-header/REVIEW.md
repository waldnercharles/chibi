# Independent witness version control

This source-only successor removes one truncation statement from the HOST/1 fixture. The fixture now changes only the header and keeps all ten metadata rows and six check rows. The separate missing-pair fixture still removes the pair row. This independently tests version rejection and row-shape rejection.

The SDK production source is byte-identical to the previous draft. The previous patch, manifest and review receipts are preserved as superseded evidence; root final review placed that draft on hold for this control gap.

`source.patch` is the two-file delta from the reviewed warm baseline. `correction.patch` is the one-line correction from the previous pair draft. Exact hashes and pending gates are in `manifest.json`. Independent and root review are pending. No build, export, runtime test, source import or runtime witness has occurred.
