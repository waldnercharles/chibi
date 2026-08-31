# Code Review

Review the current pull request against its linked issue and the repository's acceptance criteria.

## Review method

- Read the complete PR diff and relevant surrounding code.
- Check correctness, error handling, ownership/lifetime, portability, security, tests, and maintainability.
- Run focused local checks when practical; do not poll or wait for remote CI.
- Report only actionable findings that are specific, reproducible, and relevant to this PR.
- For each finding, identify the file and line, explain the impact, and give a concrete fix direction.
- Do not request changes for style preferences or speculative risks.

## Output

- Post actionable findings as a GitHub pull-request review with inline comments when possible.
- If there are no actionable findings, do not post a summary comment.
- Add the `agent-reviewed` label to identify that this review was performed by an agent.
- Do not edit files, push commits, approve, or merge.
- Do not post pickup or completion comments.
