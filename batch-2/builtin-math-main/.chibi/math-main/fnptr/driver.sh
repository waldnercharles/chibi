#!/bin/sh
R=/Users/cwaldner/Personal/chibi/.worktrees/builtin-math-main/chibi
F=$R/.chibi/math-main/fnptr
rm -rf $F/driver && cp -R $R/tools/native_aot $F/driver
python3 - <<'P'
p='/Users/cwaldner/Personal/chibi/.worktrees/builtin-math-main/chibi/.chibi/math-main/fnptr/driver/driver/Driver.chibi'
t=open(p).read()
marker='    if (Invoke(host, emitter, callable, input, module, "120000") != 0) {'
assert marker in t
hook='    if (Invoke(host, emitter, callable, input, module, "120000") == 0) {\n        build.write_file("/Users/cwaldner/Personal/chibi/.worktrees/builtin-math-main/chibi/.chibi/math-main/fnptr/last.ll", build.read_file(module));\n    }\n'
open(p,'w').write(t.replace(marker,hook+marker,1))
P
