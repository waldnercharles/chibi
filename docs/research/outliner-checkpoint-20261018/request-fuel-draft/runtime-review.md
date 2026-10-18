# Independent runtime review and first-measurement admission

This is read-only source review by outliner_runtime_authority. No build, test,
source preparation or authored execution was performed by this reviewer.

The engine candidate is pinned by ENGINE.sha256 (14 files) and engine.diff
SHA-256 4bd2776151879b876b5a89d786810e0fde91dc563f44e91a914e43db418246ff.
No blocking source finding remains. Reviewed paths include token/account
lifetime, cached-context clearing, synchronous and resumed propagation, exact
one-time live-authority grant, ordinary-budget preservation, checkpoint fast-path
bypass, VM/SLJIT/Wasm/generated-C accounting and explicit prepaid refusal. The
SLJIT check uses R3 when a float leaf keeps variables in R0, avoids R1 countdown
clobbering, and debits only after the legacy check succeeds. The account and
context layouts are included in the prepared ABI fingerprint.

The separate runtime fixture authority.c is pinned at
0e42d72f7fc4868859208f0797e09c8fa935167f2e9aa4bb1768aa7e395f71f5.
Its five cases are admitted for one first measurement under a shared hard
60-second deadline: 12 modules (10 behaviors and two imports), six tiny IR
definitions, 40 tokens with at most four live, ten emits, 20 dispatch API calls,
14 pumps, 14 direct tiny calls and two outlined child entries. The total is 14
synchronous helper entries, two tasks, two continuations and two held-request
completions. One held request and seven observations are the maximum live
fixture state. Loops stop at 32, recursion at three frames and grants at 1000.

The fixture asserts exact spent balances and failure locations, grant rejection
without balance mutation, payload ownership and FIFO across failure, caller
release while runtime ownership remains, cached account clearing, and loss of
grant authority on resumed/task/continuation execution. It requires actual
compiled entries with no bytecode inspection fallback and invokes the direct
SLJIT prepaid entry. The last-paid-check grant and nested invalidation grant are
not separate new cases; static review and existing authority coverage do not
constitute execution evidence for those precise fuel combinations.

The separate generated-C fixture request_work_c.c is pinned at
b961d70ef07ffa8973b2ef48739e85af2c7ab025a2158ccabf1c5874f3010748.
It is admitted for one first measurement under the existing shared 60-second
and compiler-child 10-second deadlines. The workload is one pico case, two IR
definitions of at most six instructions, one compiler child, generated source
below 16 KiB, one temporary shared library and fourteen calls (seven actual C
and seven bytecode). It checks exact first/second-check exhaustion, node and
error-step diagnostics, finite legacy-budget debit order, unchanged ordinary
behavior and explicit prepaid refusal before output mutation. Cleanup owns the
source stream, shared object, builder, definitions and temporary directory.

These admissions do not establish measured native, Wasm or generated-C success.
No aggregate expansion is implied. Rebuild all ABI consumers and run the final
frozen source census before adopting its numeric completion coefficient.
