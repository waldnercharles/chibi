from pathlib import Path
import json
p=Path('tools/compiler/llvm_native/Emit.chibi')
s=p.read_text()
def change_template(source, function, transform):
    a=source.index('    return strings.Join([',source.index(function)) if function=='MapShared ::' else source.index('    text := strings.Join([',source.index(function))
    a=source.index('\n',a)+1
    b=source.index('    ], "\\n");',a)
    lines=[json.loads(x.strip().removesuffix(',')) for x in source[a:b].strip().split('\n')]
    raw='\n'.join(lines)
    changed=transform(raw)
    quoted=',\n'.join('        '+json.dumps(line) for line in changed.split('\n'))+'\n'
    return source[:a]+quoted+source[b:]
def shared(raw):
    raw+='''
define internal i32 @chibi_map.capacity(i32 %count) {
entry:
  %double = shl i32 %count, 1
  %tiny = icmp ult i32 %double, 4
  %wanted = select i1 %tiny, i32 4, i32 %double
  %over = icmp ugt i32 %wanted, $LIMIT
  %capacity = select i1 %over, i32 $LIMIT, i32 %wanted
  ret i32 %capacity
}'''
    return raw
def typed(raw):
    raw=raw.replace('''  %dbl = shl i32 %count, 1
  %tiny = icmp ult i32 %dbl, 4
  %newcap = select i1 %tiny, i32 4, i32 %dbl''','''  %newcap = call i32 @chibi_map.capacity(i32 %count)''')
    a=raw.index('define internal $M @chibi_map.$N.delete(')
    before,cut=raw[:a],raw[a:]
    cut=cut.replace('  %used = extractvalue $M %m, 1\n','  %used = extractvalue $M %m, 1\n  %cap = extractvalue $M %m, 2\n',1)
    cut=cut.replace('''packed:
  %still''','''packed:
  %wanted = call i32 @chibi_map.capacity(i32 %n)
  %smaller = icmp ult i32 %wanted, %cap
  %capacity = select i1 %smaller, i32 %wanted, i32 %cap
  %still''',1)
    cut=cut.replace('''refill:
  %sbytes0 = zext i32 %slots to i64''','''refill:
  %packedslots = call i32 @chibi_map.slots(i32 %capacity)
  %sbytes0 = zext i32 %packedslots to i64''',1)
    cut=cut.replace('call void @chibi_map.place(ptr %index, i32 %slots, i32 %fh, i32 %fe)','call void @chibi_map.place(ptr %index, i32 %packedslots, i32 %fh, i32 %fe)',1)
    cut=cut.replace('''  %c2 = insertvalue $M %c1, i32 %w, 1
  ret $M %c2''','''  %c2 = insertvalue $M %c1, i32 %w, 1
  %c3 = insertvalue $M %c2, i32 %capacity, 2
  %c4 = insertvalue $M %c3, i32 %packedslots, 3
  ret $M %c4''',1)
    cut=cut.replace('''  %u3 = insertvalue $M %u2, i32 0, 3
  %u4 = insertvalue $M %u3, ptr null, 5
  ret $M %u4''','''  %u3 = insertvalue $M %u2, i32 %capacity, 2
  %u4 = insertvalue $M %u3, i32 0, 3
  %u5 = insertvalue $M %u4, ptr null, 5
  ret $M %u5''',1)
    return before+cut
s=change_template(s,'MapShared ::',shared)
s=change_template(s,'MapHelpers ::',typed)
# The shared helper's count limit comes from the same definition as insertion.
a=s.index('MapShared ::')
b=s.index('// The helpers of map type',a)
part=s[a:b].replace('return strings.Join([','text := strings.Join([',1).replace('    ], "\\n");\n}', '    ], "\\n");\n    return strings.ReplaceAll(text, "$LIMIT", format_text("{}", MapLimit));\n}',1)
s=s[:a]+part+s[b:]
p.write_text(s)
p=Path('tools/compiler/scalar_llvm/Emit.chibi')
s=p.read_text()
a=s.index('    ops: int[] = [',s.index('MapLowered ::'))
b=s.index('    right :=',a)
s=s[:a]+'''    op := native.MapLength;
    if (operand.value == facts.MapGet) { op = native.MapGet; }
    else if (operand.value == facts.MapSet) { op = native.MapSet; }
    else if (operand.value == facts.MapHas) { op = native.MapHas; }
    else if (operand.value == facts.MapDelete) { op = native.MapDelete; }
    else if (operand.value == facts.MapCopy) { op = native.MapCopy; }
    else if (operand.value == facts.MapSpan) { op = native.MapSpan; }
    else if (operand.value == facts.MapLiveAt) { op = native.MapLiveAt; }
    else if (operand.value == facts.MapKeyAt) { op = native.MapKeyAt; }
    else if (operand.value == facts.MapValueAt) { op = native.MapValueAt; }
'''+s[b:]
s=s.replace('item := Op(ops[operand.value], type, slot, stack[base], right, target);','item := Op(op, type, slot, stack[base], right, target);')
p.write_text(s)
