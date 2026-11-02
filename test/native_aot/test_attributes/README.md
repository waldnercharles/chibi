# Native test attributes

```sh
sh test/native_aot/test_attributes/run.sh /absolute/path/to/chibi /usr/lib/llvm-18/bin
```

This focused test compiles a real native program whose `Main :: () -> int`
wrapper invokes selected void `@test` functions. Passing tests execute, a failing
assertion exits134, and another independent invocation still passes. No native
entry ABI is widened. A separate imported-package fixture uses a public
dispatcher to call a private test and helper, matching the native CLI runner.

Unknown, duplicate and malformed attributes, tests with parameters or results,
and selecting a void test directly as the native entry are rejected without
replacing an existing output or writing intermediates. Attribute validation is
the ordinary compiler's existing policy, shared with the native collector; source
annotations are not stripped. No whole test corpus is run by this script.
