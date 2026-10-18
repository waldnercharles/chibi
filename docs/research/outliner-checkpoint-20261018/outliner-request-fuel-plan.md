# Outliner request fuel: implementation plan, not implemented

This is the minimal synchronous policy change recommended after the scalar
append fix is stable. No production source, build, test, or runtime policy was
changed while preparing this plan. The earlier evidence and limitations remain
in `outliner-budget-design.md` (SHA-256
`2c0d33c8778620690a190a3f5610d2962158c47576aac4532751ce1ee045d26c`).

The policy limits logical authored execution checkpoints, including synchronous
helpers, for one explicitly opted-in host request. It does not impose an actor
ceiling, promise interaction latency, charge every native operation, or permit
asynchronous/incremental publication. Capture, Project and guarded Publish stay
in the same uninterrupted synchronous handler.

## 1. Own the policy on the retained dispatch token

Extend the private `execution_dispatch` in `execution/authority.c` with an
optional work account. Keep ordinary `execution_dispatch_new()` unchanged.
Illustrative public host APIs, in the existing authority header:

```c
execution_dispatch *execution_dispatch_new_work(uintptr_t initial_limit);
bool execution_context_grant_work(
    const behavior_function_context *, execution_dispatch *, uintptr_t total_limit);
```

The account contains `uintptr_t spent, limit`, plus enabled/granted state. A
budgeted token starts with the existing 10,000-check allowance, not a new magic
large allowance. Use a smaller explicit allowance in tiny runtime fixtures.
The new constructor requires a nonzero finite initial limit; zero never means
unlimited. An optional read-only account accessor can expose spent/limit for
the existing counters/fixtures if needed; authored code gets no grant API.

The grant validates the actual live context using the same runtime-owned
activation/context traversal as `execution_context_authority`, requires the
exact active token, an enabled account, valid original-handler authority,
noncancelled execution, and no prior grant. It sets an absolute total limit,
never `spent + extra`, and never zeroes spent. A second, foreign, fabricated,
late, paused/resumed or cancelled grant fails. The Studio service must first
pass its existing module, mount, generation, scene-epoch and protected-state
checks; runtime validation does not replace them.

No additional queue-owned allocation is needed: `queued_event.dispatch` already
retains the token, and activations/host requests retain it separately. Thus the
policy automatically survives queue detachment/reordering and caller-reference
release. `dispatch_events` must use the token account for an opted-in request,
not grant another allowance from a later pump's `p->budget`. All matching
handlers/fan-out under that same token share its spent count. A new UI request
gets a new token. Re-enqueueing an existing token cannot renew its allowance.

## 2. Share one live balance through every synchronous call

Add a borrowed account pointer to `behavior_function_context`. Populate it from
the retained activation token at run entry. Copy it in `call.c:frame_new` and
retain the same value on helper refresh/resume. Task/continuation activations
that already inherit the dispatch origin retain the same account, never a new
grant; they still do not inherit publication authority. Activation cache reset
must clear the borrowed pointer together with its old dispatch context.

For an opted-in context, set its legacy per-frame `budget` ONCE to
`UINTPTR_MAX` on entry; keep that value fixed through synchronous helpers and
backend re-entry. The real finite cap is the account. For ordinary contexts,
preserve all existing per-frame and pump budget behavior. In particular, do not
raise the 10,000 default for other sources or ordinary control events.

At each `BF_CHECK`, before executing the next checked operation:

```c
if (work && work->spent >= work->limit)
    fail_at_this_check(BEHAVIOR_FUNCTION_BUDGET);
if (work)
    ++work->spent;
/* Existing frame step/check accounting remains, for diagnostics. */
```

Check before increment, so even a representable maximum limit cannot wrap the
account. Inlined frame push/pop only resets/restores diagnostic frame steps; it
never resets request work. Outlined helper calls and recursion use the same
account. The account storage is stable until its last token owner releases it.
Existing cancellation/authority checks remain; this adds no cross-thread
cancellation contract.

This stable legacy frame ceiling solves the cached-JIT problem without a second
compiled representation: Capture only changes `work->limit`; it never changes
the frame budget/countdown base. The new charge reads live account state at each
checkpoint, including after a native Capture call. Do not cache remaining fuel
across native callbacks. Explicitly review unsigned countdown, parent-frame
restoration, failure materialization and `UINTPTR_MAX` arithmetic on both
32-bit and 64-bit contexts.

## 3. Cover the actual backends, retaining append optimization

Required engine sites:

- `authority.h/.c`, `program_activation.c`, `program_internal.h`, `call.c`:
  construction, validated grant, retained ownership, shared context plumbing.
  The existing `source_authority.h` dispatch signatures need no new budget
  parameter: the request token already carries the policy.
- `machine.c:BF_CHECK`: authoritative interpreter charging. Wasm currently
  routes BF_CHECK through `execution_machine_step`; retain that path and verify
  emitted blocks cannot elide it.
- `jit.c:BF_CHECK`: equivalent charging in actual compiled SLJIT, preserving
  current failure location and diagnostic frame steps.
- `definition.h:behavior_function_fast_allowed`, native
  `behavior_function_guard`, `loop_budget_guard`, and generated `c.c`: an opted-in
  account must take the checked body rather than a shortcut that folds/elides
  checks. Update direct generated-C emission too, not only the normal runner.
  Review all `root_steps`, `child_budget` and proven-loop paths. For the initial
  implementation, disable these shortcuts only when an account is present;
  do not invent aggregate charges for folded child frames.
- Keep scalar-append/runtime-body optimization enabled where it preserves the
  original BF_CHECK sequence. Do not select inspection mode or fall back to
  bytecode merely to obtain accounting. Existing ordinary fast paths stay as
  they are. Direct root definitions with prepaid `root_steps` need an explicit
  finite debit or a checked canonical body; they must not silently bypass fuel.
- `prepared.c` context ABI fingerprint: include the new context field/layout.
  Rebuild both SDKs and consumers; old prepared/generated code must not be
  accepted with the changed context layout. No project version migration is
  implied, and no SDK ownership/inventory change is needed.

The actual shared count will exceed the previously observed 33,111 top-frame
checks because helper frames now contribute. Do not use that top-frame number
as total fuel or infer a general formula from the flat 4,096-actor case.

## 4. Grant once from the exact successful Capture

Only mounted `Refresh` and `RevealObject` requests opt in. Other controls keep
ordinary tokens. In `studio_outliner_projection.c:capture`, after the existing
protected-state read and successful owned `studio_scene_read_capture`, derive
the allowance from those exact facts. Compute it before returning captured
outputs to authored code. A failed calculation/grant returns a normal service
error, retains the last valid model, and closes/releases the read at the normal
boundary. There is no retry with a larger allowance.

Use checked unsigned arithmetic for every sum, product, string-length
conversion and final conversion to uintptr_t. Reject an unrepresentable result
with a specific workload-overflow diagnostic; do not wrap or silently clamp.
The existing INT_MAX virtual-root rejection remains. The representation limit
is not a newly selected actor-count policy.

Input dimensions, all from the captured facts/protected state:

- N actors; P prototypes; C component hints; F assignable fields; A attachments.
- q filter byte length; l scene-path byte length; r retained reveal text length;
  t the sum of actor-name and actor-prototype byte lengths. Capture's strings
  are owned, terminated copies. Use byte lengths, matching authored FoldByte.
- V is an upper bound on emitted rows from the actual First/Count/Collapsed:
  zero when collapsed; otherwise compute the existing Page arithmetic against
  N instead of filtered shown. Clamp before subtraction exactly as Page does.
  This stays above the actual page size without repeating filtering policy.
- cmax is the largest captured prototype component span; fmax the largest
  captured actor field span. Cv = V*cmax; Fv = min(F,V*fmax). Component spans
  may be reused by many actors, whereas captured actor field spans are disjoint.
- D = V*max_depth. Parent indices are validated earlier indices, so max_depth
  can be computed once in O(N) using a checked temporary N-entry depth vector.
  The simpler D=V*N avoids the temporary vector but grants much more on flat
  scenes; prefer the linear depth calculation. This is workload metadata, not
  host filtering, row projection or publication policy.

For one ContainsFolded call with text byte length n, define:

```
B(n,q) = 1                                  if q == 0 or q > n
         1 + (n - q + 1) * (q + 1)          otherwise
S = sum over actors [B(name_bytes,q) + B(prototype_bytes,q)]
```

S conservatively covers outer starts and inner comparisons, including an
unsuccessful final comparison. Separate weights cover the two FoldByte helper
calls per attempted character. Short circuiting only reduces this work.
Attachment scans contribute V*A. IntRange and Icon each contribute Cv;
IntRange and Drop each contribute Fv. Depth contributes D. Title scans the full
path for '/' and at most 127 copied bytes for its final '.', so use l+127.
Controller/Capture/Project/Publish entry and exit, Page, SelectState, row
assembly, reveal matching and the fixed five Drop suffix choices need fixed
per-request/per-actor/per-row weights as well.

A code-shaped allowance is:

```
total = existing_bootstrap
      + k0
      + kn*N + ks*S + kv*V + kd*D
      + kc*(2*Cv) + kf*(2*Fv) + ka*(V*A)
      + kl*(l+127) + kt*(t+q+r)
      + ksnapshot*(N+P+C+F+A)
```

The k values are named, reviewed conservative checkpoint costs for the shipped
producer's corresponding acyclic blocks, including reachable helpers and
inlined frames. They are NOT user tuning, timing estimates, or arbitrary large
allowances. Before enabling the policy, derive them from the frozen canonical
prepared IR/check placement after the append fix, retaining the count evidence
in a C fixture. A simple safe derivation uses the total static BF_CHECK count
of the canonical reachable closure as the common weight, with explicit helper
visit multiplicities above; finer block weights avoid excessive headroom. The
producer validator proves effects/types, not loop complexity, and the existing
`execution_cost_summary` explicitly does not provide counts. Do not pretend
either supplies this missing coefficient audit.

Authored edits continue to receive a finite host allowance for their captured
workload. They are not promised completion for arbitrary new loops/recursion.
Exhaustion is an ordinary supported error; never multiply the allowance by
retry count or grant again from repeated Capture. A changed supported shipped
algorithm needs its weights and finite cases reviewed again.

## 5. Errors and publication boundary

Fuel exhaustion uses the existing precise budget failure and retires that
execution normally. Queue error handling already preserves undispatched events
and their FIFO order; retain it. Superseded internal Refresh cancellation stays
as implemented, and user controls remain distinct.

Preserve the last successfully published valid model. Publish remains its
existing synchronous, validated, atomic commit. A later failure, including the
ShownOpen state assignment after Publish, need not roll back that valid model;
its cache may need a later refresh. Continue charging the whole tail. Do not
disable checks after Publish, move state assignments, stage publication across
frames, or introduce handler-wide transactions as part of this meter.

## 6. Finite verification before enabling

Use C/pico cases in the existing owning dispatchers, with exact source/workload
admission before any new run. Required cases are: a queued request drained by
a different pump budget; retained token lifetime after caller release; shared
debit through local/imported/recursive synchronous helpers; pre-Capture debit
preserved across one grant; duplicate/foreign/forged/cancelled/resumed grants
rejected; exact exhaustion preserving later FIFO; unchanged ordinary per-frame
behavior; and matching failures/counts in bytecode and actual SLJIT. Exercise
the actual Wasm checkpoint import and generated-C path under their separately
admitted routes, with backend counters rejecting fallback.

Studio cases should cover formula overflow, long adversarial folded text,
deep hierarchy, repeated prototype spans, per-actor fields, attachments,
First=0/1/2 and INT_MIN/INT_MAX page inputs, collapsed output, and failure
before/after an accepted Publish. Reuse the fixed 4,096-actor scale case after
the scalar optimization, reporting shared work separately from frame steps.
Do not expand aggregates or use a new million-check diagnostic as production
policy. This plan is not test admission.

## Product decisions that remain genuinely separate

No user choice is needed for correct queue ownership, checked arithmetic,
checkpoint accounting, coefficients or SDK ABI rebuilds. The user has not
chosen a scene ceiling, interaction-latency target or progressive results; this
plan chooses none. Native Capture currently includes parent searches, prototype
searches and schema scanning, and native allocation/copy/text/publication calls
are not interrupted by BF_CHECK. Consequently even a finite request allowance
is not a wall-time bound or a prompt-cancellation guarantee. If the measured
optimized synchronous path still cannot meet a desired responsiveness target,
that later target and any async/incremental UX change need an explicit product
decision and a new publication-authority design.
