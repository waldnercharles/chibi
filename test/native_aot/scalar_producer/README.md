# Native scalar producer coverage

Run `sh test/native_aot/scalar_producer/run.sh /path/to/chibi /path/to/llvm`.
The existing `language-package-compiler-chibi-scalar` selector now runs this
native fixture; the C selector and its producer-only host adapters are removed.

The runner joins the test declarations with the driver's current private
`Catalog.chibi` source. The compiler therefore receives its real target,
instruction names, native signatures and identities without a new product API
or a separately maintained catalog. The tests call `CollectScalar` and
`CollectScalarPackages`, then the real scalar validator and LLVM consumer.

Coverage from the eight producer behavior cases is preserved:

- fifteen source projections retain their rows and heap-backed source text
  after later collections; their scalar, generic, aggregate, string, control
  flow, assertion and formatting rows retain the original shape and IR checks;
- twelve exact producer row-kind sequences and 169 malformed edit sequences
  check precise diagnostics and empty output, including sequential field copies;
- imported packages, collection packages, unsorted pointer-forwarding files
  and imported rejection coordinates keep their original source inputs;
- library symbol IDs, library rejections and eager retained scalar manifests
  keep their previous contracts;
- every authored scalar corpus case checks its own source oracle, validation
  and emitted/rejected result, including the formerly skipped deep operands.

The old invalid conversion selector `2` became the supported `CheckedChar`
selector. Its unknown-mode test now uses `3`; character acceptance and failure
semantics remain covered by `test/native_aot/characters`.

The C native emitter transport check was already covered by the authored
`llvm_native/tests` call and invalid-callee tests. Its transport wrapper and the
C oracle-helper self-test are removed with the producer selector. The separate
six native backend C cases remain until their own replacements are complete.
