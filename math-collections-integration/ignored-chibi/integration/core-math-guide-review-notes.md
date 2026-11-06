# Core math guide draft review

Prepared from the six math source/test files at
`8b14194143d47ac0ec089e2bb3f034e541d1955e` and the historical guide preserved at
`35208481b36c7f7224376b218149ec1e17c32195`. Draft destination:
`.chibi/integration/core-math-guide.md`; intended public destination:
`docs/core-math.md`.

- Documented every public math export and all 31 easing exports. Kept
  `number=f32`, explicit f64 narrowing, raw interpolation arithmetic and the
  difference between finite-input assertions and easing's unclamped domain.
- Preserved Step's value/edge order, angular half-turn policy, nonpositive
  damping/time behavior, analytic spring regimes, retained velocity on
  retargeting, f64 coefficient work, and absence of settling snaps or
  near-critical substitution. Stated that finite input checks precede identity
  returns.
- Added the exact retired source spellings, verified against the frozen
  pre-retirement native declarations, beside their package replacements.
  Kept behavior migration separate from retained shader/field contracts.
- Removed recovery chronology, old ABI/seed/SDK validation assertions,
  measurements, dated release claims and relative evidence links. The draft
  makes no completed-release or installed-SDK claim. It records the SDK hold and
  incomplete current forward-port validation.
- Read all six authored sources/tests and the current native definitions and
  conversion-boundary checks. Static guide checks cover export names, code-fence
  balance, local evidence-link absence and whitespace. No runtime, build,
  bootstrap, Generate, SDK or backend work was performed for this draft.

The guide's example snippets are based on the existing source/API spellings;
they were not compiled during this documentation preparation. The current
candidate's final validation receipt should remain separate from the API guide.
