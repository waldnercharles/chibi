# CUT-61 integration for final validation — October 6, 2026

This branch prepares the existing opt-in diagnostics for review against main
`b3a1945bbe2ca871accca55f7cf8a3bcb2d3a9a5`. It preserves the prior branch head
`3dc425504b18620483fd66e0faa06a0556021d50`, including tested code
`dfacf12ea8c8e8732b5e16948f4590a8b224e530` and its evidence, through a normal
merge of main. The previously published branch head was
`dba9625c7c87e6cfe9817c2d39e1ab61c8d923d3`. Publication of this scoped work was
explicitly approved; final validation and merge remain with the integration owner.

## Source reconciliation

Main replaced the Studio runner's enum dispatch with named groups and added
`CHIBI_TEST_TIMINGS` counters. Two files required conflict resolution:

- `test/editor/studio_documents.c` keeps main's group dispatch, aggregate order,
  setup/teardown handling, and timing counters. The existing CUT-61 wrapper now
  passes the private `panel-lifecycle` name to that runner, which invokes the
  same `studio_documents_panel_lifecycle_check()` once. The private name is not
  added to `document_cases`, its listings, or the aggregate. The outer profile
  timer uses `profile_started`, separate from main's section timer.
- `test/test_suite.c` retains both selector help entries and both existing
  argument-validation paths. CUT-61's exact selector and graphics-free validation
  mode retain their reviewed behavior and execution-profile guard.

The RmlUi patch, C bridge, backend phase measurements, holder counters, kit probes,
and two CPU validation scripts are unchanged from the preserved head. The native
lifecycle body, assertions, 2,016-frame sequence, geometry propagation, and fixed
settle limit remain unchanged. No optimization or profiling expansion is added.

The outer CUT-61 frame duration now encloses main's existing timing-counter work
as well as the application frame. `CHIBI_TEST_TIMINGS` is an independent opt-in;
record it with any future measurements. Historical durations cannot be treated
as measurements of this integrated tree or as a before/after speedup comparison.

## Validation status and executor limit

Prior evidence remains tied to its original source:

- Initial headless results: `6bedae240471c7436cf5af67874436dd901360b4`.
- Parent-supplied Fedora lifecycle result: `dba9625c7c87e6cfe9817c2d39e1ab61c8d923d3`.
- Six enabled/disabled CPU runs for the four-part Context breakdown:
  `dfacf12ea8c8e8732b5e16948f4590a8b224e530`.

The initial and native measurements do not justify removing post-Context holder
updates. The four-part Context breakdown is diagnostic preparation, not a measured
performance improvement. All raw logs and recorded hashes remain unchanged.

The saved cloud executor is accessible, but current repository preflight
(`tools/cloud/bin/start_skill -- true`) exits 1 because
`/workspace/.cloud-tools/emsdk-6.0.10/upstream/bin/wasm-ld` is missing. No setup,
installation, SDK retry, environment-setting change, build, or runtime test was
performed during this integration. Source reconciliation and diff inspection do
not establish current-head build or runtime acceptance.

After the parent allocates prepared validators, rebuild the normal patched
dependency and run the existing focused checks on the exact candidate commit/tree:

```sh
make build JOBS=4 BUILD_TARGET=test_suite
python3 test/studio_panel_lifecycle_test.py build/test_suite
python3 test/context_update_profile_test.py build/test_suite /tmp/cut61-integrated-new-evidence
```

The evidence directory must be new. The second script uses the existing
graphics-free kit fixture; it does not execute the native lifecycle. Main's
Studio selector checks and the required final verification matrix also remain
with the integration owner. Any native capture requires the parent's hardware
allocation. Current-head CI, independent review of the conflict resolution, and
the applicable CUT-62 integration gates remain outstanding before merge.
