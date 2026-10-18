# Refreshed native startup check — prepared, not executed

This fresh isolated directory contains the SDK10/Studio15 host candidate for
the documented two-hidden-frame startup/readback check. The parent authorized
copy preparation after both builds completed. No Studio launch, renderer probe,
build, test or asset formatting/migration ran during this preparation. The
previous `startup-native` directory, its failing UBSan logs and PNG are intact.

Reserve this exact invocation for the parent after source review and once
builds are idle; do not execute it automatically:

```sh
set -o noclobber
bash /workspace/outliner-build/startup-native-2/run-startup.sh \
  > /workspace/outliner-build/startup-native-2/invocation.log 2>&1
```

The new runner SHA-256 is
`2805f4913a9f65a0f8f9aa7b7fcd20ce358e896a7b347343802b896cc982d692`.
Compared with the independently reviewed original runner
`11e5d01ab59b5a80e39a9aa1d102a3290dfa2bc1bf570268c09a3262c544a757`,
its only change is `startup_root` pointing at this new directory. The original
application log guards and exit handling are unchanged, as the parent directed.
There is exactly one `test_policy.run_commands` call with `seconds=60` and this
single application command:

```sh
/workspace/outliner-build/startup-native-2/runtime/bin/chibi-dev \
  --project-root /workspace/outliner-build/startup-native-2/project \
  --headless --frames 2 --fixed-dt 0.016666666666666666 \
  --screenshot /workspace/outliner-build/startup-native-2/startup.png
```

Initialization, both frames, rendering/readback and teardown share the existing
production process-group/descendant deadline. The existing screenshot readback
also has its own ten-second bound. No checkpoint is supplied: native host
defaults to 1920x1080. No input sequence, extra frames, retry, renderer fallback,
display server or security-setting change is included. This documented startup
route remains separate from the external acceptance-selector binary-size gate.

## Required result review

The runner refuses an existing PNG/log, returns a nonzero supervisor result,
and fails on any `Studio UI:`, `runtime frame failed` or `Could not initialize`
marker. Its boolean result alone does not reject every recoverable sanitizer
diagnostic. The parent must inspect the full retained invocation/application
logs for `runtime error:`, `AddressSanitizer`, `UndefinedBehaviorSanitizer` and
`LeakSanitizer`; any sanitizer diagnostic fails this check even with exit 0
and a valid PNG. Do not set sanitizer suppression/runtime-option overrides.
The original startup failure demonstrates why this separate review is required.

Also require a newly created decodable 1920x1080 PNG, inspect actual Studio
chrome/panels/text/icons, record PNG hash and measured supervisor time, and
confirm no child remains. Preserve all evidence on failure and stop without
automatic retry. This opens an empty Start scene and establishes only startup
and readback, not Outliner input, scale, browser behavior or UI latency.

## Isolation and renderer

The current acceptance project (three files) and all 394 current resource
files were copied with `cp -a --reflink=auto`. The built executable was copied
the same way and byte-compared with the source. Binary/share/chibi adjacency
is preserved. Source/copied project manifests and source/copied resource
manifests compare exactly; neither copied tree contains a symlink. All
preferences, cache, tmp, XDG runtime/config/data directories begin empty.
The outer directory and XDG runtime are mode 0700. HOME and CODEX_HOME are
untouched. Only these disposable copies may be changed during startup.

The runner sources `/workspace/outliner-build/test-env.sh`, which loads the
saved build/cloud environment, and asserts the original loader path and
`/workspace/.cloud-tools/root/usr/share/vulkan/icd.d/lvp_icd.json`. SDL remains
offscreen and audio dummy. Existing capability evidence identified Mesa
llvmpipe/LLVM19.1.7 software Vulkan; no new driver enumeration or rendered-frame
claim is made by preparation. Any eventual timing describes instrumented Debug
code and software rendering, not hardware GPU performance.

The runner unsets ASAN_OPTIONS/UBSAN_OPTIONS/LSAN_OPTIONS, retaining sanitizer
defaults and the saved symbolizer path. All 195 current Studio compile entries
and 1562 SDK compile entries contain `-fsanitize=address,undefined`; stored
CMake flags include frame pointers. Static ELF symbol inspection confirms
ASan/UBSan handlers in the copied host without executing it.

## Exact candidate identities

All values are SHA-256 unless labeled otherwise. Copies of complete manifests,
compile records, build logs, source status/diff and source file hashes are in
`provenance/`. The human labels SDK10/Studio15 are supported by these values:

- Installed SDK package build ID:
  `778b78b7b14216ea6f3445b4b3ff23abe0efa609e7ee9d717b88c03c2a52fa27`.
- Engine build ID:
  `ae09e05d499645060c7de14f16bed12ba730037ca74f37fc719269f875489fb0`.
- SDK source snapshot:
  `8cf9fcb246917d70fbd688b891ba6880378078cba5a5668a4ff989ffeb96abdf`,
  dirty engine revision `ff6d06c888fe660894776255ec7f9fc507db6393`.
- The copied SDK engine.json equals runtime/share/chibi/engine.json. Studio's
  configured SDK build identity equals the installed package ID above.
- `chibi-dev`, 402936248 bytes:
  `196cb2cf4603ad2e08430a3c2f32b796e279f728c2932acf08bfde253498175f`.
- Resource manifest:
  `72e5e29ad5e0f4e0c9333efce24fb03bf43ce6dffece99c5b95e861a54334264`.
- Three-file project manifest:
  `e37684b8e34f6c36f4db0ebf6ef94593489dae0de937ce23f4c93c946b7c9bc5`.
- Preparation-time 891-file Studio source manifest:
  `28184c87f93a28cb1a369fbd769d5178d2c4e598fc6c771b6201da5a2925791c`.
- Studio source HEAD `95ca95bc3ca2da5b899ec06596982ade26d2d94f`, with the
  recorded working changes; this source manifest includes acceptance/docs
  files and does not assert those were compiled into chibi-dev.
- Formatted production adapter:
  `f343e1771d312f80f89b74212f287da878860088de7684752178e908a2bb063e`.
- Bundled OutlinerRowPolicies:
  `e2c727396b2a5004327fcf17cd3e1a8194629c928db75c4927ef6f4cae9c28a5`.
- Bundled OutlinerRows:
  `510f2155a49ff77724efd38be5d1098f5ac5957c62acb0c06eb755de22e8a450`.

The last two bundled assets match the current authored source exactly. The
current checked implementation includes request fuel and the reviewed scalar
append/Cute fixes; installed package/source identity is pinned above. Patch,
dependency wiring, profile, loader/driver, supervisor, SDK and binary input
hashes are retained in `provenance/inputs.sha256`. SDK10 packaging and Studio15
build logs are copied; configuration/build time remains separate from the
startup command. Current output PNG, PNG log and invocation log are absent.
