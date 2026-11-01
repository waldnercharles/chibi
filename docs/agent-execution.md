# Bounded agent execution

Use the smallest check that answers the current question. Avoid paying for a
full suite after a parser or logic edit. Profiles are explicit and work on any
machine; no hostname, driver name or assumed device capability selects a
profile.

## Build, select, run

```sh
chibi build --target test_tools --target test_language   # the test drivers
chibi build test --list                                  # every selector and group
chibi build test language-ranges                          # one selector
chibi test tools/compiler                                # one package's @test functions
```

`chibi build test` runs each selector as its own process under a deadline and
reports a timeout as a failure, never a pass. [Testing](testing.md) describes
the groups CI runs.

## Reviewed checks and truthful handoff

`chibi build test unit` runs the fast C execution-policy cases.
`chibi build test short` runs all retained C tooling and process integrations.
Select native checks for the affected runtime contracts.

Choose publication checks through independent review of the affected contracts
and their interactions. Preserve each selected check's backend, sanitizer and
hardware requirements. Report the command, candidate identity, configuration,
result and unexecuted coverage. Local evidence supports landing when CI is
disabled unless actual server rules require it. Explicit holds and approvals
remain binding.

## Retired routes

The agent supervisor (`tools/agent.py`: `doctor`, `plan`, `preview`, `mp4`),
the `make agent-*` targets, the GPU preview and MP4 capture routes and the
installed Studio acceptance routes were deleted with `tools/test_policy.py` in
819d20cd4. The renderer and player they drove are archived (`archive/README.md`).
Git history keeps their 2026-10-05 artifact evidence and the CUT-51 and PR171
checkpoints.
