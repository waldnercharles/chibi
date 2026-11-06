from pathlib import Path
scope={}
exec(Path('.chibi/native-maps/gen.py').read_text(),scope)
generic,typed=scope['GENERIC'],scope['TYPED']
# New indices are always initialized independently of the caller allocator.
typed=typed.replace('  %fresh = call ptr @chibi_map.allocate(ptr %ctx, i64 %bytes)\n  br label %head', '  %fresh = call ptr @chibi_map.allocate(ptr %ctx, i64 %bytes)\n  call void @chibi_scalar.zero(ptr %fresh, i64 %bytes)\n  br label %head', 1)
# Slot counts and capacities use unsigned i32. Avoid overflow before growth.
typed=typed.replace('insert:\n  %full', '''insert:
  %atlimit = icmp uge i32 %count, 1073741824
  br i1 %atlimit, label %limit, label %available
limit:
  %limit.at = getelementptr inbounds { ptr, ptr, ptr, ptr, ptr }, ptr %ctx, i32 0, i32 3
  %limit.fail = load ptr, ptr %limit.at
  call void %limit.fail(ptr %ctx, i32 %count, i32 1073741824)
  unreachable
available:
  %full''')
for old in ['%entries','%cap','%used','%slots','%index']:
    typed=typed.replace('[ '+old+', %insert ]','[ '+old+', %available ]')
def quoted(value):
    return ',\n'.join('        "'+line.replace('\\','\\\\').replace('"','\\"')+'"' for line in value.strip('\n').split('\n'))
p=Path('tools/compiler/llvm_native/Emit.chibi')
s=p.read_text()
# Preserve first-draft declarations and replace only the generated helper bodies.
a=s.index('MapShared :: () -> string {')
b=s.index('// The helpers of map type',a)
s=s[:a]+'''MapShared :: () -> string {
    return strings.Join([
'''+quoted(generic)+'''
    ], "\\n");
}

'''+s[b:]
a=s.index('    text := strings.Join([',s.index('MapHelpers ::'))
b=s.index('    text = strings.ReplaceAll(text, "$PREHASH"',a)
s=s[:a]+'''    text := strings.Join([
'''+quoted(typed)+'''
    ], "\\n");
'''+s[b:]
s=s.replace('(op >= MapLength && op <= MapValueAt','(op >= MapLength && op <= MapLiveAt')
s=s.replace('item.op <= MapValueAt','item.op <= MapLiveAt')
s=s.replace('pub InstructionCount: int : 69;', '''// output = the physical entry span, including dead positions, for map iteration.
pub MapSpan: int : 69;
// output = whether the right slot's int position in the entry span is live.
// A position outside the span is false, so no key or value is read there.
pub MapLiveAt: int : 70;
pub InstructionCount: int : 71;''')
s=s.replace('op == MapValueAt;','op == MapValueAt || op == MapLiveAt;')
s=s.replace('item.op == ArrayResize) {','item.op == ArrayResize || item.op == MapSet || item.op == MapDelete || item.op == MapCopy) {')
s=s.replace('if (item.op == MapLength) {\n        return item.type == abi.IntStorage;', 'if (item.op == MapLength || item.op == MapSpan) {\n        return item.type == abi.IntStorage;')
s=s.replace('    if (item.op == MapKeyAt) {\n        return item.type == key', '    if (item.op == MapLiveAt) {\n        return item.type == abi.BoolStorage && Typed(slots, item.right, abi.IntStorage);\n    }\n    if (item.op == MapKeyAt) {\n        return item.type == key')
s=s.replace('''    if (item.op == MapLength) {
        out = format_text("{}  %v{} = extractvalue {} %m{}, 0\\n", out, pc, map, pc);''','''    if (item.op == MapLength || item.op == MapSpan) {
        field := 0;
        if (item.op == MapSpan) {
            field = 1;
        }
        out = format_text("{}  %v{} = extractvalue {} %m{}, {}\\n", out, pc, map, pc, field);''')
s=s.replace('entry := format_text("{ {}, {} }", TypeName(key), TypeName(value));','entry := format_text("{ {}, {}, i32 }", TypeName(key), TypeName(value));')
a=s.index('        out = format_text("{}  %length{} = extractvalue',s.index('MapRead ::'))
b=s.index('\n    }\n    return format_text',a)
s=s[:a]+'''        out = format_text("{}  %length{} = extractvalue {} %m{}, 1\\n  %in{} = icmp ult i32 %b{}, %length{}\\n", out, pc, map,
                          pc, pc, pc, pc);
        if (item.op == MapLiveAt) {
            out = format_text("{}  br i1 %in{}, label %inside{}, label %after{}\\ninside{}:\\n", out, pc, pc, pc, pc);
        } else {
            out = format_text("{}  br i1 %in{}, label %inside{}, label %outside{}\\noutside{}:\\n", out, pc, pc, pc, pc);
            out = format_text("{}{}", out, ContextCall(format_text("index{}", pc), 3));
            out = format_text("{}  call void %index{}(ptr %ctx, i32 %b{}, i32 %length{})\\n  unreachable\\ninside{}:\\n", out, pc,
                              pc, pc, pc);
        }
        out = format_text("{}  %data{} = extractvalue {} %m{}, 4\\n  %offset{} = zext i32 %b{} to i64\\n", out, pc, map, pc,
                          pc, pc);
        out = format_text("{}  %hash.at{} = getelementptr inbounds {}, ptr %data{}, i64 %offset{}, i32 2\\n", out, pc, entry, pc, pc);
        out = format_text("{}  %hash{} = load i32, ptr %hash.at{}\\n  %live{} = icmp ne i32 %hash{}, 0\\n", out, pc, pc, pc, pc);
        if (item.op == MapLiveAt) {
            out = format_text("{}  br label %after{}\\nafter{}:\\n  %v{} = phi i1 [ false, %p{} ], [ %live{}, %inside{} ]\\n", out, pc, pc, pc, pc, pc, pc);
        } else {
            out = format_text("{}  br i1 %live{}, label %live.entry{}, label %outside{}\\nlive.entry{}:\\n", out, pc, pc, pc, pc);
            out = format_text("{}  %at{} = getelementptr inbounds {}, ptr %data{}, i64 %offset{}, i32 {}\\n", out, pc, entry,
                              pc, pc, field);
            out = format_text("{}  %v{} = load {}, ptr %at{}\\n", out, pc, TypeName(item.type), pc);
        }'''+s[b:]
# Copy retains only the live pair it just moved. Drop releases only a live pair.
a=s.index('// A loop over the `count` entries')
b=s.index('// The helpers every map type shares',a)
s=s[:a]+'''// The reference changes for the live entry just copied or being dropped.
// Copy's `ymove` block ends in `yretained`, which its loop's phi names.
MapEach :: (call: string, entries: string, prefix: string, from: string, key: int, value: int) -> string {
    offset := "%xoff";
    if (call == "retain") {
        offset = "%woff";
    }
    out := "";
    if (key == abi.TextStorage) {
        out = format_text("  %{}k = getelementptr inbounds $E, ptr {}, i64 {}, i32 0\\n", prefix, entries, offset);
        out = format_text("{}  %{}kv = load $K, ptr %{}k\\n{}", out, prefix, prefix,
                          MapOwner(call, format_text("%{}kv", prefix), format_text("{}k", prefix)));
    }
    if (value == abi.TextStorage) {
        out = format_text("{}  %{}v = getelementptr inbounds $E, ptr {}, i64 {}, i32 1\\n", out, prefix, entries, offset);
        out = format_text("{}  %{}vv = load $V, ptr %{}v\\n{}", out, prefix, prefix,
                          MapOwner(call, format_text("%{}vv", prefix), format_text("{}v", prefix)));
    }
    if (call == "retain") {
        out = format_text("{}  br label %yretained\\nyretained:\\n", out);
    }
    return out;
}

'''+s[b:]
s=s.replace('format_text("{ {}, {} }", TypeName(key), TypeName(value))','format_text("{ {}, {}, i32 }", TypeName(key), TypeName(value))')
s=s.replace('// LLVM { i32, i32, ptr, ptr }, its count, its entries\' capacity, the entries in\n// insertion order and an index over them, by value.', '// LLVM { i32, i32, i32, i32, ptr, ptr }, its live count, physical span,\n// entries capacity, index slots, insertion-ordered entries and index, by value.')
s=s.replace('// output = the left slot\'s map without the right slot\'s key, the entries after\n// it moved down, or as it is for a key it lacks; type is the map type. It', '// output = the left slot\'s map without the right slot\'s key, or as it is\n// for a key it lacks. Indexed maps mark dead entries and compact amortized; it')
s=s.replace('// output = the key of the right slot\'s int position among the left slot\'s map\'s\n// entries in insertion order, of type; a position outside the map ends the\n// program.', '// output = the key at a live physical position in insertion order, of type;\n// an out-of-span or dead position ends the program. Iterate MapSpan/MapLiveAt.')
a=s.index('// Native maps, docs/native-maps.md.')
b=s.index('// The most entries a map keeps',a)
s=s[:a]+'''// Native maps, docs/native-maps.md. The header holds live count, physical span,
// entry capacity, index slot count, entries and index. Each insertion-ordered
// entry is {key, value, hash}, hash 0 marks a dead entry. Robin Hood slots hold
// {hash, entry + 1}; they keep a dead entry's hash until a compaction rebuilds
// the index, so lookup stopping distances stay valid. Each mutation consumes
// the map it reads. A string key/value passed to MapSet is one owned reference.

'''+s[b:]
p.write_text(s)
p=Path('tools/compiler/native_abi/Abi.chibi')
s=p.read_text()
a=s.index('// The LLVM type of a map, by value:')
b=s.index('// The LLVM type of execution_native_failure',a)
s=s[:a]+'''// A map's live count, physical span, entries capacity, index slot count,
// entries and index. The all-zero 32-byte header owns nothing. Each map in a
// record pays this budget; the physical span permits amortized O(1) deletion
// while retaining insertion order (docs/native-maps.md).
pub MapType :: () -> string {
    return "{ i32, i32, i32, i32, ptr, ptr }";
}
'''+s[b:]
p.write_text(s)
