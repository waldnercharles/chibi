# Hosted Ubuntu dependency downloads

`install-ubuntu-dependencies.py` changes mirror preference for the three Linux
dependency steps in `ci.yml` and web publishing in `publish.yml`. It copies the
runner's sources and mirror list to a unique owned `/tmp/chibi-apt-*` directory, preferring
`https://archive.ubuntu.com/ubuntu/`, then
`https://security.ubuntu.com/ubuntu/`, and retaining Azure as the last fallback.
It does not edit `/etc`, change timeouts, remove packages, or change test gates.

## Bounded input contract

The inspected runner was `ubuntu-24.04`, image `20261012.334.1`. Its
[image provisioning script](https://github.com/actions/runner-images/blob/ubuntu24/20261012.334/images/ubuntu/scripts/build/configure-apt-sources.sh)
creates a three-entry, 144-byte `/etc/apt/apt-mirrors.txt` with tab-separated
priorities 1/2/3 for Azure/archive/security. It substitutes
`mirror+file:/etc/apt/apt-mirrors.txt` in `ubuntu.sources` (deb822); its older
image path uses `sources.list` (one-line format).

The helper accepts exactly those three mirror entries, plus blank lines and
comments. It changes only their priorities to 3/1/2. Added mirrors, changed
priorities, or filtering metadata fail before any APT invocation. Review the
new runner configuration and add a fixture before extending this contract.
This avoids interpreting or discarding mirror architecture/suite/component
filters. APT documents [mirror priorities and metadata](https://manpages.ubuntu.com/manpages/noble/man1/apt-transport-mirror.1.html).

The source substitution accepts a single-URI `URIs:` line or a `deb`/`deb-src`
line with optional bracketed options. It replaces only the exact mirror-list
URI. Continuations of matched `URIs` fields, duplicate fields in matched deb822
stanzas, multi-URI Ubuntu fields, direct Azure entries, and malformed
references fail closed. No recognized mirror reference is also an error.
Other source fields and unrelated repository files are copied verbatim; suites,
components, architectures, `Signed-By`, and APT preferences remain in force.
This is a bounded textual substitution, not an APT parser: APT validates the
resulting sources and signed indexes during update.

Update, candidate reporting, and install share command-local source and list
options. Lists start empty and binary package caches are disabled, so the new
mirror URI cannot reuse old indexes. `APT::Update::Error-Mode=any` makes even a
partial update failure stop installation. Signature validation and TLS checks
remain enabled; `Debug::Acquire::gpgv=true` exposes verification in the job log.
Candidate versions (`apt-cache policy`) and installed versions (`dpkg-query`)
are logged for every requested package. Normal package selection is unchanged;
fresh official indexes can differ from a lagging mirror, so compare inventories
before attributing a timing difference solely to transport.

The temporary source tree uses searchable directories (0755) and readable files
(0644), not writable permissions for other users. Before update, the helper
checks every copied file and directory as `_apt`, including ancestor traversal.
`RUNNER_TEMP` can have a private ancestor and is deliberately not used or chmodded.
APT keeps its normal sandbox; the helper removes only its uniquely allocated tree
in a `finally` block, using sudo because APT creates root/_apt-owned list entries.
No package or system source configuration is changed by the access probe.

## Validation and measurement

Run `python3 .github/test-ubuntu-dependencies.py` for offline fixtures, including
both source formats, unrelated repositories, options, invalid mirror lists,
fresh list isolation, continuation/duplicate rejection, sandbox access command
construction, inaccessible leaves/ancestors, and cleanup after preparation/access/install
failures. These use temporary files and mocked
commands; they do **not** establish live APT or signature-validation evidence.
The native Linux job also runs them before installing dependencies.

The motivating [Studio job 121592578107](https://github.com/waldnercharles/chibi/actions/runs/40328849874/job/121592578107)
spent 25m06s in the dependency step. APT fetched 48.1 MB in 24m35s (32.6 kB/s).
The retried `libclang-18-dev` download occupied 11m31s; the final Azure
`mesa-vulkan-drivers` attempt occupied 5m39s before HTTPS archive fallback
completed in about a second. Installed versions included
`libclang-18-dev=1:18.1.3-1ubuntu1` and
`mesa-vulkan-drivers=25.2.8-0ubuntu0.24.04.4`.

Use the normal PR CI jobs to inspect selected mirror, valid signatures, package
candidates/installed versions, download bytes/rate, and total dependency-step
duration for native Linux, Studio, and Graph SDK. Keep image version, package
set, and index freshness visible when comparing with existing runs. Wait for
all ordinary gates and independent review; no extra benchmark dispatch is
needed for this checkpoint. A fast run demonstrates viability, not a guaranteed
speedup or elimination of the intermittent download tail. Fresh index downloads
add work on healthy runners; unrelated build and test latency is outside scope.

Web publishing uses the same helper with its existing package list and
`--no-install-recommends`; its CMake pin, caches and release behavior are unchanged.
The motivating [web publish job 122074850978](https://github.com/waldnercharles/chibi/actions/runs/40470057262/job/122074850978)
on main `88ec9dc` spent 11m42s installing dependencies: APT update fetched
12.6 MB in 1s, but package downloads fetched 38.4 MB in 11min 24s (56.1 kB/s)
from Azure. Pip installed CMake 4.2.3 in about 1.5s. The workflow runs only on
main; ordinary PR CI exercises helper fixtures but does not prove the actual
web publishing install path. Compare its next main run using the same inventory
and download evidence above before claiming a measured improvement.
