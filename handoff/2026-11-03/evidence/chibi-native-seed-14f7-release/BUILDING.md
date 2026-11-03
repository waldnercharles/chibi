# Rebuild the intermediate native seed

Source commit: `14f7a46a047ab92acfe58398b3cdac25ab7d0312`.
`chibi-source.tar.gz` is the complete Git archive of that commit. All 3,400
tracked blobs and their executable/symlink modes were checked against Git and
against the source snapshot used by the recorded proof. `SOURCE_FILES.json`
records their object IDs, SHA-256 values and sizes.

The two executable assets need only their platform system runtime. Building
requires LLVM/LLD 22.1.8: `opt`, `llc`, and `ld64.lld` on macOS arm64, or `ld.lld`
and the libc development/start files on Linux x64. No C compiler or VM builds
this native compiler. Other repository products have their own dependencies.

First verify the prepared files without executing a compiler:

```sh
python3 verify-package.py --repo /absolute/path/to/a/chibi/git/checkout
```

The `--repo` argument verifies the archived inventory against the exact Git
commit as well. Omit it for a checksum, archive, fixed-point and log audit using
the accompanying receipts alone. No network access is needed.

Extract `chibi-source.tar.gz`, enter its `chibi` directory, and set `SEED` to the
absolute path of this package's executable for your platform. Set `LLVM_BIN` to
an absolute tools directory and `WORK` to a new directory:

```sh
set -eu
root=$(pwd -P)
mkdir "$WORK"
case "$(uname -s)-$(uname -m)" in
  Darwin-arm64) triple=aarch64-apple-darwin ;;
  Linux-x86_64) triple=x86_64-unknown-linux-gnu ;;
  *) exit 2 ;;
esac
builder=$SEED
for stage in 2 3; do
  mkdir "$WORK/stage$stage"
  env CC=/usr/bin/false CXX=/usr/bin/false "$builder" \
    --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --output "$WORK/stage$stage/chibi" --llvm "$LLVM_BIN" --target "$triple"
  builder=$WORK/stage$stage/chibi
done
cmp "$WORK/stage2/chibi" "$WORK/stage3/chibi"
CHIBI_CORE="$root/packages/core" "$builder" test --root "$root/tools/compiler" \
  --llvm "$LLVM_BIN" "$root/tools/compiler"
```

Use the same output basename `chibi` for consecutive Linux builds. The recorded
first Linux attempt used `stage3` and `stage4`, producing 58 different ELF
`STT_FILE` names. It was not accepted as a byte-identical fixed point. The later
`fixed5/chibi` and `fixed6/chibi` outputs are the accepted equal pair. Both
initial binaries, every original build log, the original scripts and a parsed
ELF difference audit are preserved in `chibi-proof.tar.gz`.

The proof archive includes both members of each accepted consecutive pair.
`proof/ACCEPTED_PAIRS.json` records their original paths, archived names, hashes,
actual `cmp` status and equality with the selected executable asset.

The recorded macOS pair is `stage2` and `stage3`. Its builder's verified bytes
already equal the selected result. This is recorded rather than presented as
an independent source lineage. The Linux builder is the verified compiler from
CI artifact 13204584163; its original receipt accompanies the proof archive.

Both builds set `CC` and `CXX` to `/usr/bin/false`. The recorded Linux test run
inherits those settings from its build script. The separate macOS test command
sets `CHIBI_CORE` and has no explicit `CC` or `CXX` override. Exact commands,
selected binary hashes and environment claims are in `PROVENANCE.json`.

These are local intermediate bootstrap candidates. `native-seed.tsv` proposes
two pins; no release, installed pin or Windows asset is created by preparation.
