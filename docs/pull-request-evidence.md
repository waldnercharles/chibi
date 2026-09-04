# Pull request evidence

Every pull request must prove the changed behavior. Use the pull request template;
keep the evidence summary short and do not paste a command transcript.

## Choose the evidence path

- **Automated** — use this for core, data, build, documentation, and other changes
  whose behavior is not meaningfully visible in the running app. List focused unit
  or integration tests and their outcome.
- **Visual** — use this when a user-visible app behavior changed. Include
  focused automated tests and, when useful, a short description of the manual
  interaction or local capture used during verification. Media is optional.

A headless screenshot is only render sanity evidence: it proves dimensions and
that pixels were produced, not that a user can operate the feature. It does not
satisfy visual evidence by itself.

The `Evidence contract` check verifies that every PR records its category,
feature proof, and automated tests. It does not require media, an external URL,
or an interaction trace; those details may be included when they help a
reviewer reproduce the result. It validates the review contract, not the quality
of any optional capture.
