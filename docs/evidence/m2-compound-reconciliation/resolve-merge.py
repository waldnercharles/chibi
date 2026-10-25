from pathlib import Path
import subprocess,re,json,hashlib
p=Path('.chibi/compound-proof')
policies={
'src/toolchain/compiler_host.c':(2,'Keep one identical compiler_host_root at its reviewed main690 position; reject duplicate relocation.'),
'test/language/compiler_artifact_chibi.c':(1,'Retain reviewed production snapshot/full-source retention path for Strings and seven unaffected guards; reject alternate32KiB fixture cap.'),
'test/language/float_conversion_cases.h':(21,'Retain explicit narrowing diagnostics, independent rounding literals and signed/grouped/subnormal/overflow constants. Author positive cases are already covered; keep constant classification fields.'),
'test/language/main.c':(2,'Retain both durable shift selectors and all main957 dispatch entries.'),
'test/language/package_compiler_chibi.c':(15,'Retain reviewed compound fixtures, two-backend debug metadata, strict typed-constant diagnostics, cleanup, five fold literal expectations and no-ahead oracle paths. Author two compound cases are included in expanded tested seven-case adaptation.'),
'test/language/semantic.c':(3,'Retain approved float admission name, exact rejection diagnostics and registered case; do not weaken narrowing tests.'),
'tools/compiler/bootstrap/bootstrap.json':(3,'Retain exact historical S input pair; author-generated pair has different source/target provenance. No generation or relabeling.'),
'tools/compiler/package_compiler/Body.chibi':(1,'Parenthesis traversal already present and verified; keep source identity, omit redundant comment in fold dispatch branch.'),
'tools/compiler/package_compiler/Compile.chibi':(1,'Retain integer-only ConstantExpression classification comment and ordinary semantic FoldConstant ownership.'),
'tools/compiler/package_compiler/Number.chibi':(1,'Retain reviewed FloatingBits conversion; reject competing NarrowedBits/WidenedBits helpers.'),
'tools/compiler/package_compiler/Steps.chibi':(7,'Retain explicit eight-operator scalar compound checks before widening/reference delegation, conditional NumberLiteral watch, strict FloatWidens admission and existing conversion diagnostics. No broad operator/field guard or unconditional watch removal.')}
receipts=[]
for path,(count,reason) in policies.items():
 f=Path(path);s=f.read_text();pattern=r'^<<<<<<< HEAD\n(.*?)^=======\n(.*?)^>>>>>>> [^\n]+\n';blocks=list(re.finditer(pattern,s,re.M|re.S));assert len(blocks)==count,(path,len(blocks))
 receipts.append({'path':path,'reason':reason,'hunks':[{'index':i,'selected':'reviewed integration side','integration_sha256':hashlib.sha256(m[1].encode()).hexdigest(),'author_sha256':hashlib.sha256(m[2].encode()).hexdigest()} for i,m in enumerate(blocks)]})
 f.write_text(re.sub(pattern,lambda m:m[1],s,flags=re.M|re.S))
# Resolve binary provenance explicitly, retaining the reviewed input rather than the author output.
binary='tools/compiler/bootstrap/compiler.chibip';Path(binary).write_bytes(subprocess.check_output(['git','show','HEAD:'+binary]));receipts.append({'path':binary,'reason':'Keep exact S b1d902d3 historical input, not cf24 generated31d5c893; new generation remains pending.'})
# Inspect and remove the specific automatically merged alternatives, not only conflict markers.
def edit(path,old,new,reason):
 f=Path(path);s=f.read_text();assert s.count(old)==1,(path,old[:100],s.count(old));f.write_text(s.replace(old,new));receipts.append({'path':path,'automatic_hunk':reason,'removed_sha256':hashlib.sha256(old.encode()).hexdigest(),'replacement_sha256':hashlib.sha256(new.encode()).hexdigest()})
edit('test/language/compiler_artifact_chibi.c','BINARY_CAP = 32 * 1024,\n    /* Each source of the closure; core:strings is just past 16 KiB. */\n    SOURCE_CAP = 32 * 1024','BINARY_CAP = 32 * 1024','Remove now-unused alternate source cap; actual production limits unchanged.')
path='tools/compiler/package_compiler/Compile.chibi';s=Path(path).read_text();a=s.index('                folding := Folding.Waiting;');b=s.index('                    case .Failed(diagnostic):',a);old=s[a:b];new='''                switch FoldConstant(forest, documents[constant.file].text, constant.file, constants,
                                    Aliases(links, constant.file), package, constant.node, constant.storage) {
''';edit(path,old,new,'Do not route floating declarations to older standalone evaluator; reviewed FoldConstant handles approved types.')
s=Path(path).read_text();a=s.index('// The bits of the floating constant `node`');b=s.index('// The exact value of the constant integer expression',a);edit(path,s[a:b],'','Remove entire alternative FoldFloat/FloatConversion/FloatBitsAs implementation, covered by ordinary semantic owner.')
edit(path,'return Evaluated.Failed(Diagnose(forest, node, file, Mismatched(left_type, right_type)));','''return Evaluated.Failed(Diagnose(forest, node, file,
                                             format_text("Mismatched integer types {} and {}; convert one operand",
                                                         TypeName(left_type), TypeName(right_type))));''','Retain exact diagnostic construction without unrelated helper refactor.')
path='tools/compiler/package_compiler/Steps.chibi'
edit(path,'''                if (Floating(conversion)) {
                    return Fail(forest, item.node, document, "wrap converts between integer types");
                }
''','','Remove redundant alternative conversion-label diagnostic; existing approved check before named-argument dispatch remains.')
edit(path,'return Fail(forest, item.node, document, Mismatched(left.storage, right.storage));','''return Fail(forest, item.node, document,
                        format_text("Mismatched integer types {} and {}; convert one operand", TypeName(left.storage),
                                    TypeName(right.storage)));''','Retain existing noncompound binary operator scope and diagnostic construction.')
path='test/language/package_compiler_chibi.c';s=Path(path).read_text();a=s.index('    /* Floating constants fold at their type:');b=s.index('    { .name = "untyped constant expressions",',a);edit(path,s[a:b],'','Author floating sample is subsumed by stronger independent literal/subnormal/rounding/overflow controls in FLOAT_CONVERSION_CASES; do not substitute cast-vs-cast oracles.')
s=Path(path).read_text();a=s.index('/* A folded constant\'s value is watched once');b=s.index('/* Each compiler backend has a focused selector.',a);edit(path,s[a:b],'','Omit redundant two-span watched_once machinery; full same_watches multiplicity comparisons remain, and new compounds explicitly compare on both compiler backends.')
edit(path,'''/* An accepted module, `ir` its IR, which this frees: its Main must give the
 * literal `expected`, and each probe its oracle, on both VMs and natively at
 * O0 and O2. */''','''/* An accepted module, `ir` its IR, which this frees: each of its calls must
 * give one value on both VMs and natively at O0 and O2. */''','Keep exact tested fixture bytes; actual literal Main expectations and native O0/O2 assertions remain intact. Author comment substance recorded in evidence.')
(p/'per-hunk-resolutions.json').write_text(json.dumps(receipts,indent=2)+'\n')
