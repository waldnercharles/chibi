# Systems lifetime acceptance review

Read-only source review, 2026-10-23. No product source, tests, artifacts, imports, or ABI choices changed. No checks were executed.

Reviewed compiler-owner note: `/Users/cwaldner/Personal/chibi/.worktrees/selfhost-behavior-state/chibi/.chibi/game-support/combined-d0/future-acceptance/systems.md`, SHA256 `98ac5b1cd0ea39e4af20a6cbf49ee2ee73ca9456ecebedbd37baaea3a8476e5c`. Its frozen engine baseline is `1a59410022d128108e7744cf2b5641047a44d713`.

Row 7 now correctly requires a dependency to last until task completion, including after the final component access. Earlier effects remain; removal prevents remaining continuation/completion effects. There is no blocking objection to this wording. The acceptance evidence must distinguish historical system membership cancellation, pending fixed-target runtime support, and future authored inference.

## Owning fixtures and evidence limits

Paths in the first row refer to the compiler-owner checkout. Remaining program fixtures refer to `/Users/cwaldner/Personal/chibi/.worktrees/query-lifetime/chibi`, frozen runtime tree `b2c82d244774c095feb9d4c223740a1a85265099`. Its `test/language/program.c` live blob matches that tree: `d16c9a5e0d64a37786e2f181d617d644b5ba6b2b`. These are source assertions, not execution results from this review.

| Fixture | Useful oracle and limit |
| --- | --- |
| `test/code_system_runtime.c:196`, `persistent_wait_membership`; case `source_system_persistent_wait_membership` at line 527 | Existing native ABI20 suspension, retained registration state, removal/readdition and entity-generation behavior. It accesses Health after the wait, so it cannot establish dependency retention after final access or arbitrary-task inference. |
| `test/language/program.c:4103`, `program_bound_task_checks_admission_before_its_body` | Invalid, replaced and foreign targets prevent initial body effects; empty binding remains unbound. |
| `test/language/program.c:4199`, `program_bound_task_cancels_pending_children_and_preserves_unbound_pointer_users` | Earlier `began` effect occurs, remaining `done` effects stay zero, waiter sees Cancelled, all three requests cancel, replacement does not revive the task, passive pointer equality in an independent task survives. Add an assertion that `began` is still one after cancellation in the future authored case to prove no rollback. |
| `test/language/program.c:4241`, `program_bound_task_does_not_consume_ready_external_results_after_removal`; line 4283, `program_bound_task_stops_when_an_inline_outcome_invalidates_its_target` | Ready-result and inline invalidation gates complement pending-task cleanup. |
| `test/language/program.c:4373`, `program_bound_task_cannot_succeed_after_its_last_expression_removes_the_target` | Invalid target prevents success publication. This is a different case from last access before a wait. |
| `test/language/program.c:4400`, `program_bound_child_unites_inherited_and_distinct_explicit_targets` | Inherited and distinct child dependencies, including cancellation direction. |
| `test/language/program.c:4459`, `program_completed_bound_parent_keeps_its_result_and_child_dependencies` | Completed success remains available after later removal; still-running children retain inherited dependencies. |
| `test/language/program.c:4496`, `program_bound_task_resources_detach_in_each_destruction_order` | Resource detachment across instance, program, world and entity destruction. |

The query-lifetime harness explicitly binds targets: `lifetime_compile` at line 759 locates named Bound functions; `lifetime_start` at line 806 calls `program_task_start_bound`; `lifetime_bind` at line 847 installs target cells. None proves compiler-inferred dependencies. Those cases remain separate, source-only runtime evidence.

## Required future authored acceptance

Extend the existing authored compiler fixture area (`test/language/package_compiler_chibi.c`, current task prerequisite case `package_compiler_runs_task_sequencing_and_cancellation` at line 4778 in the value-semantics checkout) and owning program/system dispatchers after the common source contract is reconciled.

Use a task that accesses the original component, increments an independent counter, waits, then increments a completion counter without accessing the component again. The authored compiler must infer the dependency; no host-installed binding and no authored lifetime marker may supply it.

Remove the component during the wait, including a remove-and-readd variant. Assert the earlier counter remains one, completion remains zero, an independent waiter observes Cancelled, pending requests cannot complete into the task, and cleanup finishes. Include a surviving-target control that completes normally, completed-success retention after later removal, child cancellation, and genuinely independent value-only work. Run the same accepted contract through bytecode and the supported compiled backend once eligible.

The native comparison paragraph should explicitly distinguish existing native parity from new approved semantics. A frozen ABI20 outcome cannot be the oracle for inference or whole-task behavior it does not implement. Two projects without systems cannot establish this coverage. Targets first selected later and dependency propagation to already-running children remain outside this review; do not choose their API or wire representation here.

Our tested task tree `6353f5a382785977b0b9948cfcbcde37facd0d0c` and docs-only lifetime overlay `f0fe82ef0e09532f74c356fc194edf556f78d13d` remain frozen. This ignored review note is the only file written.
