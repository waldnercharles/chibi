# Pull request evidence

Every pull request must prove the changed behavior. Use the pull request template;
keep the evidence summary short and do not paste a command transcript.

## Choose the evidence path

- **Automated** — use this for core, data, build, documentation, and other changes
  whose behavior is not meaningfully visible in the running app. List focused unit
  or integration tests and their outcome.
- **Visual** — use this when a user-visible app behavior changed. Include both
  focused automated tests and a feature-specific PNG/JPG or MP4 captured after
  driving the real application with computer-use. Add a short interaction trace
  explaining what the artifact demonstrates.

A headless screenshot is only render sanity evidence: it proves dimensions and
that pixels were produced, not that a user can operate the feature. It does not
satisfy visual evidence by itself.

## Attaching visual evidence

Use GitHub's PR description or comment editor to upload captures: drag the PNG/JPG/MP4 into the editor, wait for the upload to finish, and copy the resulting direct URL. The validator accepts only URLs in this form:
`https://github.com/user-attachments/assets/<uuid>`
Do not paste a release page, a local path, an external hosting URL, a URL with query parameters, or a link to an artifact that is not yet uploaded. GitHub-hosted attachment URLs are preferred because disposable captures do not belong in the repository. A durable screenshot that is part of the product documentation may be committed for documentation purposes, but it does not satisfy this validator's visual-evidence field. Do not claim visual evidence for an artifact that only shows an idle shell or an unrelated screen.

The `Evidence contract` check verifies that every PR records its category, feature
proof, automated tests, and, for visual changes, an artifact URL and interaction
trace. It validates the review contract; it does not replace human review of the
artifact or test quality.
