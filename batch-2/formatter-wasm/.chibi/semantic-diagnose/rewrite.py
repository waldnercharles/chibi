from pathlib import Path
import re,sys
folder=Path(__file__).parent
s=(folder/'SourceAnalysis.original').read_text()
stubs={'SourceTypeName':'return "";','SourceSites':'return [];','SourcePrepared':'return SourceAnalysis.Rejected({start: 0, end: 0, line: 1, column: 1, document: -1, message: "stub"});','AnalyzeSources':'return SourceAnalysis.Rejected({start: 0, end: 0, line: 1, column: 1, document: -1, message: "stub"});','SameSourceCode':'return false;','SameSourceBody':'return false;','SameSemantics':'return false;'}
for name,body in stubs.items():
 if name in sys.argv[1:]:continue
 m=re.search(r'^(?:pub )?'+name+r' ::',s,re.M);start=s.index('{',m.end());depth=1;quote=False;escaped=False;i=start+1
 while depth:
  ch=s[i]
  if quote:
   if escaped:escaped=False
   elif ch=='\\':escaped=True
   elif ch=='"':quote=False
  elif ch=='"':quote=True
  elif ch=='{':depth+=1
  elif ch=='}':depth-=1
  i+=1
 s=s[:start]+'{ '+body+' }'+s[i:]
(folder/'compiler/package_compiler/SourceAnalysis.chibi').write_text(s)
