# Open sdk-reflect wrapper finding

Unresolved review: https://github.com/waldnercharles/chibi/pull/1331#discussion_r4443811449

Read-only inspection of `tools/build.sh` at the published source tree confirms
that bootstrap-option scanning continues after the pass-through `--`. Inferred
LLVM options are appended at line 40 and a downloaded pinned-driver option at
line 57. For `sdk-reflect -- ...`, those defaults therefore land in the forwarded
arguments instead of the build driver's bootstrap options. Flag-looking tokens
after the delimiter can also influence the wrapper's bootstrap selection.

No correction or new regression test was made in this preservation checkpoint.
The review remains open. `Main` in `tools/toolchain/Driver.chibi` recognizes the subcommand at the
fixed position following `build`, so blindly prepending options before the verb
is not a valid correction. The eventual fix must stop scanning at the first
delimiter, retain missing-value checks, and place inferred options before the
delimiter while preserving the verb, original quoting, empty arguments and
explicit-option semantics.

The existing `test/bootstrap/native-entry.sh` controlled wrapper fixture is the
appropriate focused regression surface. The direct Driver dry-run pass-through
case does not cover wrapper-inferred defaults. Test explicit and inferred
bootstrap inputs and flag-looking pass-through tails against the exact changed
wrapper, without treating a Driver-only test as wrapper coverage. No successful
fix, test, external bootstrap attempt or review resolution is claimed here.
