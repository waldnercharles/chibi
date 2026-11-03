Prepared intermediate native bootstrap compilers for Linux x64 and macOS arm64,
from exact source `14f7a46a047ab92acfe58398b3cdac25ab7d0312` and LLVM 22.1.8.

The macOS stage2/stage3 pair matches byte for byte. The Linux accepted pair is
fixed5/chibi and fixed6/chibi, also byte-identical. Each selected platform
compiler passes all 485 compiler tests: macOS 24.64 seconds, Linux 71.73 seconds.

The earlier Linux stage3/stage4 comparison was not a fixed point. Its 58
changed bytes are exclusively ELF STT_FILE basename strings. Original binaries
and logs remain in the proof archive alongside the accepted pair's evidence.

The complete 3,400-blob Git source archive, licenses, build/test scripts,
provenance, source inventory, proof binaries, checksums and an offline verifier
accompany the executable assets. Bootstrap builder hashes are independently
checked against source.json. These are bootstrap results, not final product or
VM-retirement acceptance.

Status: local preparation only. The proposed two-pin manifest is not installed,
this candidate has no publication approval, and Windows is excluded.
