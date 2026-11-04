# Unarmed successor: distinguish pidfd readiness from errors

The original independently reviewed attribution candidate remains unchanged at `/workspace/shared/chibi-graph-c-owner-candidate-20261104-revision01/graph_c_attribution.py`, SHA-256 `54f1e3e445c4d1750ed77c7ceb012f7c7e084af95e1054c117d933792d95cc6f`.

This runtime successor changes only `GraphCancellation.ready()`. Its SHA-256 is `aeb3759d27e523aeb0257e73790ffce8cca89a1e64feb02290a2442d903914b4`. The old core's source review is precedent, not acceptance of these new bytes.

## Concrete boundary gap

The prior implementation reduced the entire `poll(0)` result to a Boolean. In `finish()`, an injected nonempty POLLERR or POLLNVAL result paired with fdinfo Pid=-1 could therefore be treated as successful retirement. This was discovered by source tracing while preparing case13 models. No owner, fixture, model or kernel process-control operation was run, and no naturally occurring invalid retained pidfd has been demonstrated under the sole-thread ownership contract.

The successor accepts only an empty result (not ready), or one exact `(pidfd, mask)` tuple for the retained descriptor with integer POLLIN, POLLHUP, or their combination. Error bits, unsupported bits, zero masks, wrong descriptors, multiple events and unsupported shapes record a fault-only pidfd_poll_failure event and reject through the existing irreversible failure latch. The event retains the new poll detail even when cleanup already has an earlier immutable failure reason. No attribution, ancestry, wait, cancellation, deadline or signal rule changes.

## Valid states and callers

Linux documents pidfd readability on termination and hangup on reap in [pidfd_open(2)](https://man7.org/linux/man-pages/man2/pidfd_open.2.html). The [Linux v6.12 pidfd_poll implementation](https://raw.githubusercontent.com/torvalds/linux/v6.12/fs/pidfs.c) returns readable bits for a terminal task and also HUP after disappearance. This poller requests POLLIN, not POLLRDNORM; unrequested normal-read bits are not part of the accepted result. HUP alone remains supported as a documented hangup state. These are interface/source references, not a claim that this executor runs that exact kernel source.

- Registration still accepts only the original live identity with no readiness; any readiness rejects the registration. Invalid polling now names and latches the poll failure itself.
- Final retirement still separately requires fdinfo Pid=-1 and a valid nonempty readiness event. Error/invalid events cannot substitute for that evidence.
- Failure cleanup still skips an already signalled child or a valid terminal-ready child. Invalid polling now stops that cleanup action and propagates the already-latched failure. It does not signal through an uncertain descriptor, retry a denied operation, or label incomplete cleanup successful. The fixed caller records unresolved cleanup and the unchanged outer owner retains its rejection behavior and original bound.

Case13 controls bind the actual successor bytes, exercise live/retired/error/invalid/unsupported results at both registration and final retirement, and retain positive valid-readiness controls. All controls and this successor remain unarmed pending separate independent source, artifact and execution admission.
