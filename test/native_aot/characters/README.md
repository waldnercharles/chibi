# Native characters

`sh test/native_aot/characters/run.sh /path/to/chibi /path/to/llvm [legacy-chibi]`
checks nominal characters at O0/O2 and runs the untouched core UTF-8 tests. The
optional legacy runner compares the same ten character/authored-shadow
outcomes. The foreign-declaration and procedure-shadow tests run natively; the
legacy runner explicitly skips foreign imports.

Characters use their ordinary i32 representation after the ordinary checker
preserves nominal argument rules. Dynamic conversion checks the full original
integer against Unicode scalar bounds before narrowing, with AssertionFailed
for negative, surrogate, out-of-range and wide values whose low bits look valid.
The legacy classifier recognizes the char guard's exact custom text as its
existing assertion category; unrelated custom messages remain unclassified.
Formatting preserves ordinary i32 decimal/default and low-byte `:c` behavior.
