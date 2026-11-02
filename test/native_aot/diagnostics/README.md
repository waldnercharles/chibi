# Native diagnostic coordinates

Run the focused real-native check with an existing compiler and LLVM directory:

```sh
sh test/native_aot/diagnostics/run.sh /absolute/path/to/chibi /usr/lib/llvm-18/bin
```

The script compiles this checkout's producer and native diagnostic consumer into
one standalone native probe. It appends the actual driver catalog and pure core
rather than maintaining test copies. It does not run the broader scalar corpus.

Rejected package sets retain the established two-row contract: the source path
is the first input's path, its text is the parsed documents joined with newline
boundaries, and the rejection range/line/column addresses that joined text.
The focused cases cover parser and semantic failures in the entry and imported
file, a native-only lowering rejection, and target-source substitution. The
native driver's displayed location must map back to the original input file.
Accepted rows, messages, row layout, and the existing C joined-coordinate
tripwire are unchanged.
