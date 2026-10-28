# Release retention

Releases are durable download dependencies, not a record of every successful
build. Ordinary build output should eventually use expiring Actions artifacts;
selected, tested engine packages, compiler seeds and dependency archives need
stable downloads.

## Read-only proposal

The first slice adds inventory and planning only. It does not change publication,
deploy Studio, delete anything, or generate executable deletion commands. Existing
publishing and consumer workflows continue unchanged.

```sh
python3 .github/release-retention.py collect > /tmp/chibi-releases.json
python3 .github/release-retention.py plan /tmp/chibi-releases.json > /tmp/chibi-cleanup.json
python3 .github/test-release-retention.py
```

Collection uses authenticated `gh api --method GET` with pagination, reads main
and open PR runtime/dependency manifests at exact source SHAs, reads the public
Studio manifest, then rechecks main and open PR heads. It never prints credentials.
Permission errors fail collection; an unreadable live deployment blocks the plan.
Missing manifest files on older PR heads are recorded explicitly, not interpreted
as proof that their historical dependencies are unused.

The proposal retains:

- The newest **10 complete engine snapshots**, ordered by publication time.
- The deployed Studio engine, including export tools consumed by game publishing.
- Main and open PR head/base snapshots, plus explicitly selected rollback tags in
  `.github/release-retention.json` (`protected_tags`).
- Stable releases and **all non-engine releases** in this initial slice. Recorded
  runtime/dependency pins explain known consumers, but unpinned dependencies are
  not yet deletion candidates. Historical recovery is a separate policy decision.
- Drafts, incomplete or nonstandard asset inventories, immutable releases,
  mismatched/annotated/missing tags, and tag-only entries for manual investigation.

An engine snapshot must have exactly the five expected uploaded, nonempty assets
and a lightweight tag pointing to the SHA in its name. This is inventory
completeness, not a claim that its binaries have been downloaded and validated.
Old documentation or acceptance receipts are not automatic permanent exemptions;
add a checkpoint to `protected_tags` only if its reproducibility is still wanted.

The output names each proposed release ID, matching tag and SHA, and every asset's
ID, name, size, digest (when supplied by GitHub), URL and update time. It includes
the input fingerprint and reasons for every retained release. Unknown deployments,
missing protected releases, changing source heads, or observations over one hour
old block review readiness. `deletion_authorized` is always false, including when
`ready_for_review` is true. There is deliberately no apply mode or scheduled job.

Before any separately approved cleanup, refresh the inventory, review other
deployed apps and chosen rollback pairs, compare the exact release/tag/asset list,
and obtain approval for that list. A read-only snapshot is not atomic with future
publication. Recheck it immediately before removal. Historical, external, local
and archived-repository consumers are not exhaustively proven unused by this tool.

Deleting a release removes uploaded binaries and breaks pinned download URLs.
Keeping its Git tag or source commit does not preserve those assets. Deleting the
tag also removes the named source reference. Do not promise recovery without a
verified copy of the exact assets and metadata. An approved obsolete snapshot's
release and matching tag should be handled together; source branches are retained.

## Selected-build publication transition (not enabled)

Studio currently resolves engine main and requires its complete `engine-<SHA>`
release for production deployment. Source fallback builds do not deploy. Game
publishing reads the deployed Studio manifest and downloads the matching engine
export tools. Stopping engine publication first would break that contract.

Prepare and review this sequence across both repositories before changing it:

1. Keep current publication working. Let an ordinary package build save complete
   packages, checksums and source/build identity as bounded-retention CI artifacts.
2. Add explicit selection/promotion of one reviewed, tested exact engine SHA to a
   durable `engine-<SHA>` release, without replacing existing published bytes.
   Validate the complete asset set and checksums before exposing it to consumers.
3. Update Studio to select that published SHA explicitly instead of assuming every
   new engine main has a release. Verify the exact matching SDK/export identities,
   production deployment, game export, retry behavior and selected rollback pair.
   Promote engine first, deploy Studio second; preserve existing download URLs.
4. Only after those consumer checks pass, make routine engine builds artifact-only
   and durable release publication an explicit promotion. Do not replace public
   bootstrap/download dependencies with expiring or authentication-only artifacts.
5. Refresh the protected deployed/rollback set and prepare a new cleanup proposal.
   Automated destructive pruning, dependency retirement and tag-only cleanup need
   separate design, tests and authorization; this change enables none of them.
