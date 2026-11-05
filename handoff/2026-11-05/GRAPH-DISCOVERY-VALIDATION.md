# Complete Linux Discovery validation

The complete authored `DiscoveryTests.Run` passed on Linux x86-64 and was independently accepted at published source commit `d65f29d4106a85e416e72570b619157780f5ba66`, tree `6df05ece0bb708a7ed970fe96ce82595729f48d2`. The later base checkpoint `5bec249d2d98375d17ae5e8bf96b0239786643a2` adds only Build/Driver validation documentation. This report does not relabel the tested source as a later exact-head run.

## Portability correction and complete scope

The test-only correction appends inherited `CMAKE_LIBRARY_PATH` after the fixture library path. Fixture prefix/include isolation and search precedence remain unchanged. All 50 authored assertion sites and 11 child invocations remain: Drivers, Prefixes, PathSpelling, and eight BasenamePaths cases. The full entry constructed four real cached native child programs. Normal compiler job and level defaults were unchanged. Assertion-site counts are static source inventories, not instrumented dynamic counts.

The final Prefixes assertions still require a complete external libclang installation after deleting the fixture libraries. The supplied genuine LLVM 22.1.8 development headers, shared library and resource headers were selectively extracted from three official LLVM packages into the workspace, with every byte and link independently checked. No host prefix was changed. No package driver was executed and libclang was not loaded or linked by this filesystem-discovery suite. Successful fallback paths were not printed; their exact selection is inferred from the completed assertions and pinned search precedence, not a runtime trace.

## Ownership review and result

Before launch, independent review accepted the single exact temporary symlink `project/.chibi/graph-discovery/basename paths 3/include -> headers` during output census. Its sibling had to be a real directory, the link was counted without traversal, and all other symlinks/nonregular outputs remained rejected. Independent negative filesystem controls checked the boundary. Strict final inventory still rejects every symlink and requires the whole authored fixture root removed.

The changed census body received six fresh Python ownership controls rather than reusing historical owner acceptance. Clean and dead-adopted cases passed; live-adopted, timeout, worker-failure and post-quiescence-fence cases failed as intended and cleaned up. Independent review accepted their actual outer exit zero in 5.252 seconds within the original 30-second bound, followed by separate prospective native review and one-use suite admission.

Main construction, full Run, worker and outer process exited zero. Observed outer elapsed time was 37.073 seconds; owned elapsed time was 37.006 seconds. The fixed 600-second total included construction, execution, final fences and ten seconds reserved for cleanup. All eleven authored 30-second child limits were unchanged. These are bounded correctness timings, not benchmarks.

All four top-level split streams and the owner log were empty. Independent terminal review verified ECHILD, EOF, no owner signals/errors/adoptions/survivors, exact executable identities, and definitive post-ECHILD fences. Authored scratch, including the temporary symlink, was removed. Capture and private home/temp/cache/Git-template directories were empty. Only the expected native cache programs remained; original and private compiler copies ended at mode 0600.

The run reverified 4,178 source entries, 891 pinned file facts, 60 directory inventories and 915 required absences. Exact receipt and artifact identities are in [graph-discovery-results.json](graph-discovery-results.json).

## Preservation and remaining gates

Local archive `chibi-graph-discovery-d65f29d4-source-proof-20261105.tar.gz` is 22,007,223 bytes with 8,907 members, SHA256 `2e773e36403995d7ea5e2486cd306b326baba117fb8e577e09cee0a2448d0ca0`. Its 17,276-entry inventory includes 8,371 explicit byte-and-mode-identical duplicate mappings. Every member, source tree and pinned input fact was reverified. Large dependencies remain external and pinned. Packaging performed no native execution or upload.

Direct full-entry dispatch does not validate the `tools/build.sh` registry adapter. Remaining final-source Graph entries, current products and C integration, and actual Windows execution are separate gates. POSIX drive-spelling fixtures are not Windows proof. Whole-suite containment does not establish the stronger historical per-Capture descendant-drain proof. PR #1331's explicit hold, the unknown original c5 bootstrap and excluded native union leak investigation remain unchanged. No merge or release is implied.
