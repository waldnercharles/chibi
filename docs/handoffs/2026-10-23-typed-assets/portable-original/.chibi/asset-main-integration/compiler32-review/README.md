# Compiler32 source reconciliation

Source-only review of immutable combined tree `2acb927a94319b4bfff42fa56d5af613e4b664da` against prior rich `b029a6f59d1ff2f8185541538c90c1930a0e3cce` and typed `0b5c20a7222a9d171bfcbe35570c184a78f4c8d1`. No active source/index mutation, build, test, import or generation.

Required adaptation: preserve the pure package language `assert` without a native catalog row. `BodyContext.package_assert` is presently carried but unused by call resolution.

Keep typed `BootstrapContract [5,2]`, ordinary Build/Chibi fixtures, unsigned-domain producers, no arbitrary outer test deadlines, and all typed dispatchers. Incoming compiler Body/Compile/Steps/Behavior and their semantic facts must be reconciled together.

`review.json` gives the integration map and generation implications. Three `.diff` files provide exact immutable source comparisons. Manifest hashes all match. This is not eligibility or runtime evidence.
