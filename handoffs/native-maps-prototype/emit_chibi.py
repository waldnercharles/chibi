exec(open('/tmp/w22-map/gen.py').read())
def lit(text):
    lines=text.strip('\n').split('\n')
    out=[]
    for l in lines:
        out.append('        "'+l.replace('\\','\\\\').replace('"','\\"')+'"')
    return ',\n'.join(out)
head = r'''package llvm_native

import abi "project:native_abi"
import strings "core:strings"

// Native maps, docs/native-maps.md. A map type is MapType + value * 16 + key:
// its key a bool, an integer or a string, its value a scalar, a string or a
// record. Its LLVM type is abi.MapType(): the count, the entries' capacity, the
// entries in insertion order, each a { key, value }, and an index of slots that
// exists only above MapScanned entries. The index is one allocation: its slot
// count in 8 bytes, then the slots, each a { hash, entry + 1 } with 0 for empty,
// in Robin Hood order, kept at most three quarters full. The zero map owns
// nothing. Every instruction that changes a map consumes it and gives the map
// that replaces it, as ArrayPush does; MapSet and MapDelete take literal 1 when
// the map owns its allocations, so that those they leave go back to the
// context. A string a map takes with MapSet is one reference the map owns.

// The most entries a map keeps without an index: a scan of that many keys beats
// a hash.
MapScanned: int : 8;

// The key and value types of map type `type`.
MapKey :: (type: int) -> int {
    return (type - MapType) % 16;
}
MapValue :: (type: int) -> int {
    return (type - MapType) / 16;
}
// The type of the map from `key` to `value`.
pub MapOf :: (key: int, value: int) -> int {
    return MapType + value * 16 + key;
}
// Whether a map may be keyed by `key`: a bool, an integer or a string. A float
// is not, as NaN equals nothing.
MapKeyed :: (key: int) -> bool {
    return key == abi.BoolStorage || Integer(key) || key == abi.TextStorage;
}
// Whether `type` is a map type of a keyable type to a scalar, a string or one
// of the module's records.
MapShaped :: (records: Record[], type: int) -> bool {
    if (type < MapType || type >= RefType) {
        return false;
    }
    value := MapValue(type);
    return MapKeyed(MapKey(type)) && value > 0 && value < ArrayType && Valued(records, value);
}
// The map type an instruction works on: a change gives it as its type, and a
// read has it in its literal.
MapUsed :: (item: Instruction) -> int {
    if (item.op == MapSet || item.op == MapDelete || item.op == MapCopy || item.op == MapDrop) {
        return item.type;
    }
    return int(item.literal);
}
// Whether an instruction names the context: it allocates, gives back, or ends
// the program on an index outside the map.
MapCalls :: (op: int) -> bool {
    return op == MapSet || op == MapDelete || op == MapCopy || op == MapDrop || op == MapKeyAt || op == MapValueAt;
}
// Whether a map instruction reads the operands of its type: the map in the left
// slot, a key or an index in the right, and MapSet's value in slot `target`.
MapValid :: (records: Record[], slots: int[], item: Instruction) -> bool {
    used := MapUsed(item);
    if (!MapShaped(records, used) || !Typed(slots, item.left, used)) {
        return false;
    }
    key := MapKey(used);
    value := MapValue(used);
    if (item.op == MapLength) {
        return item.type == abi.IntStorage;
    }
    if (item.op == MapGet) {
        return item.type == value && Typed(slots, item.right, key);
    }
    if (item.op == MapHas) {
        return item.type == abi.BoolStorage && Typed(slots, item.right, key);
    }
    if (item.op == MapSet) {
        return (item.literal == 0 || item.literal == 1) && Typed(slots, item.right, key) && Typed(slots, item.target, value);
    }
    if (item.op == MapDelete) {
        return (item.literal == 0 || item.literal == 1) && Typed(slots, item.right, key);
    }
    if (item.op == MapKeyAt) {
        return item.type == key && Typed(slots, item.right, abi.IntStorage);
    }
    if (item.op == MapValueAt) {
        return item.type == value && Typed(slots, item.right, abi.IntStorage);
    }
    return true;
}
// The map types `code` works on, once each, in order of first use.
MapTypes :: (code: Instruction[]) -> int[] {
    used: int[] = [];
    for (item in code) {
        if (item.op >= MapLength && item.op <= MapValueAt && !used.contains(MapUsed(item))) {
            used.append(MapUsed(item));
        }
    }
    return used;
}
// Whether a map of `type` holds strings, which each hold a reference.
MapHolds :: (type: int) -> bool {
    return MapKey(type) == abi.TextStorage || MapValue(type) == abi.TextStorage;
}

// The instruction in `item` as LLVM text: the map is loaded as %m<pc>, and its
// helpers are the ones of its type, below.
MapRead :: (item: Instruction, pc: int) -> string {
    used := MapUsed(item);
    map := abi.MapType();
    key := MapKey(used);
    value := MapValue(used);
    owns := "false";
    if (item.literal == 1 && (item.op == MapSet || item.op == MapDelete)) {
        owns = "true";
    }
    out := Load(format_text("m{}", pc), used, "s", item.left);
    if (item.op == MapLength) {
        out = format_text("{}  %v{} = extractvalue {} %m{}, 0\n", out, pc, map, pc);
    } else if (item.op == MapGet || item.op == MapHas) {
        out = format_text("{}{}", out, Load(format_text("k{}", pc), key, "s", item.right));
        out = format_text("{}  %v{} = call {} @chibi_map.{}.{}({} %m{}, {} %k{})\n", out, pc, TypeName(item.type), used,
                          MapHelper(item.op), map, pc, TypeName(key), pc);
    } else if (item.op == MapSet) {
        out = format_text("{}{}", out, Load(format_text("k{}", pc), key, "s", item.right));
        out = format_text("{}{}", out, Load(format_text("c{}", pc), value, "s", item.target));
        out = format_text("{}  %v{} = call {} @chibi_map.{}.set(ptr %ctx, {} %m{}, {} %k{}, {} %c{}, i1 {})\n", out, pc, map,
                          used, map, pc, TypeName(key), pc, TypeName(value), pc, owns);
    } else if (item.op == MapDelete) {
        out = format_text("{}{}", out, Load(format_text("k{}", pc), key, "s", item.right));
        out = format_text("{}  %v{} = call {} @chibi_map.{}.delete(ptr %ctx, {} %m{}, {} %k{}, i1 {})\n", out, pc, map,
                          used, map, pc, TypeName(key), pc, owns);
    } else if (item.op == MapCopy) {
        out = format_text("{}  %v{} = call {} @chibi_map.{}.copy(ptr %ctx, {} %m{})\n", out, pc, map, used, map, pc);
    } else if (item.op == MapDrop) {
        return format_text("{}  call void @chibi_map.{}.drop(ptr %ctx, {} %m{})\n", out, used, map, pc);
    } else {
        // An entry by position, for a loop over the map's insertion order.
        entry := format_text("{ {}, {} }", TypeName(key), TypeName(value));
        field := 0;
        if (item.op == MapValueAt) {
            field = 1;
        }
        out = format_text("{}{}", out, Load(format_text("b{}", pc), abi.IntStorage, "s", item.right));
        out = format_text("{}  %length{} = extractvalue {} %m{}, 0\n  %in{} = icmp ult i32 %b{}, %length{}\n", out, pc, map,
                          pc, pc, pc, pc);
        out = format_text("{}  br i1 %in{}, label %inside{}, label %outside{}\noutside{}:\n", out, pc, pc, pc, pc);
        out = format_text("{}{}", out, ContextCall(format_text("index{}", pc), 3));
        out = format_text("{}  call void %index{}(ptr %ctx, i32 %b{}, i32 %length{})\n  unreachable\ninside{}:\n", out, pc,
                          pc, pc, pc);
        out = format_text("{}  %data{} = extractvalue {} %m{}, 2\n  %offset{} = zext i32 %b{} to i64\n", out, pc, map, pc,
                          pc, pc);
        out = format_text("{}  %at{} = getelementptr inbounds {}, ptr %data{}, i64 %offset{}, i32 {}\n", out, pc, entry,
                          pc, pc, field);
        out = format_text("{}  %v{} = load {}, ptr %at{}\n", out, pc, TypeName(item.type), pc);
    }
    return format_text("{}{}", out, Store(format_text("%v{}", pc), item.type, "s", item.output));
}
// The name of the helper a read instruction calls.
MapHelper :: (op: int) -> string {
    if (op == MapGet) {
        return "get";
    }
    return "has";
}

// LLVM text that releases, or adds a reference to, the owner of the string
// `value`, a name unique in the function by `tag`.
MapOwner :: (call: string, value: string, tag: string) -> string {
    text := abi.TextType();
    out := format_text("  %o.{} = extractvalue {} {}, 2\n", tag, text, value);
    if (call == "retain") {
        return format_text("{}  call void @chibi_scalar.retain(ptr %o.{})\n", out, tag);
    }
    return format_text("{}  call void @chibi_scalar.free(ptr %ctx, ptr %o.{})\n", out, tag);
}
// A loop over the `count` entries at `entries` that retains or releases each
// string key and value of the map of key and value types `key` and `value`,
// leaving the block `<prefix>d`; `from` is the block the loop is entered from.
// A map with no strings goes straight on.
MapEach :: (call: string, entries: string, prefix: string, from: string, key: int, value: int) -> string {
    keyed := key == abi.TextStorage;
    valued := value == abi.TextStorage;
    if (!keyed && !valued) {
        return format_text("  br label %{}d\n{}d:\n", prefix, prefix);
    }
    entry := "$E";
    out := format_text("  br label %{}head\n{}head:\n  %{}i = phi i32 [ 0, %{} ], [ %{}next, %{}body ]\n", prefix, prefix,
                       prefix, from, prefix, prefix);
    out = format_text("{}  %{}end = icmp eq i32 %{}i, %count\n  br i1 %{}end, label %{}d, label %{}body\n{}body:\n", out,
                      prefix, prefix, prefix, prefix, prefix, prefix);
    out = format_text("{}  %{}off = zext i32 %{}i to i64\n", out, prefix, prefix);
    if (keyed) {
        out = format_text("{}  %{}k = getelementptr inbounds {}, ptr {}, i64 %{}off, i32 0\n", out, prefix, entry, entries,
                          prefix);
        out = format_text("{}  %{}kv = load $K, ptr %{}k\n{}", out, prefix, prefix,
                          MapOwner(call, format_text("%{}kv", prefix), format_text("{}k", prefix)));
    }
    if (valued) {
        out = format_text("{}  %{}v = getelementptr inbounds {}, ptr {}, i64 %{}off, i32 1\n", out, prefix, entry, entries,
                          prefix);
        out = format_text("{}  %{}vv = load $V, ptr %{}v\n{}", out, prefix, prefix,
                          MapOwner(call, format_text("%{}vv", prefix), format_text("{}v", prefix)));
    }
    return format_text("{}  %{}next = add i32 %{}i, 1\n  br label %{}head\n{}d:\n", out, prefix, prefix, prefix, prefix);
}

// The helpers every map type shares: the context's allocation, a hash, the
// index's Robin Hood placement and its rebuild.
MapShared :: () -> string {
    return strings.Join([
@GENERIC@
    ], "\n");
}

// The helpers of map type `type`, named `@chibi_map.<type>.<helper>`: hash,
// same, find, get, has, set, delete, copy and drop.
MapHelpers :: (type: int) -> string {
    key := MapKey(type);
    value := MapValue(type);
    keyed := key == abi.TextStorage;
    valued := value == abi.TextStorage;
    prehash := "  %wide = zext $K %key to i64";
    same := "  %same = icmp eq $K %a, %b";
    if (keyed) {
        prehash = "  %wide = call i64 @chibi_map.text_hash($K %key)";
        same = "  %same = call i1 @chibi_scalar.text_equal($K %a, $K %b)";
    } else if (abi.IntegerBits(key) == 64) {
        prehash = "  %wide = add i64 %key, 0";
    }
    // What replacing a value, or deleting an entry, gives back.
    replaced: string[] = [];
    deleted: string[] = [];
    if (valued) {
        replaced.append(format_text("  %old = load $V, ptr %rv\n{}", MapOwner("release", "%old", "rv")));
        deleted.append(format_text("  %dv = getelementptr inbounds $E, ptr %hole, i32 0, i32 1\n  %dvv = load $V, ptr %dv\n{}",
                                   MapOwner("release", "%dvv", "dv")));
    }
    if (keyed) {
        replaced.append(MapOwner("release", "%key", "rk"));
        deleted.append(format_text("  %dk = getelementptr inbounds $E, ptr %hole, i32 0, i32 0\n  %dkv = load $K, ptr %dk\n{}",
                                   MapOwner("release", "%dkv", "dk")));
    }
    text := strings.Join([
@TYPED@
    ], "\n");
    text = strings.ReplaceAll(text, "$PREHASH", prehash);
    text = strings.ReplaceAll(text, "$SAME", same);
    text = strings.ReplaceAll(text, "$REPLACED", strings.TrimSuffix(strings.Join(replaced, ""), "\n"));
    text = strings.ReplaceAll(text, "$DELETED", strings.TrimSuffix(strings.Join(deleted, ""), "\n"));
    text = strings.ReplaceAll(text, "$RETAINED", strings.TrimSuffix(MapEach("retain", "%fresh", "r", "clone", key, value), "\n"));
    text = strings.ReplaceAll(text, "$RELEASED", strings.TrimSuffix(MapEach("release", "%entries", "x", "entry", key, value), "\n"));
    text = strings.ReplaceAll(text, "$M", abi.MapType());
    text = strings.ReplaceAll(text, "$E", format_text("{ {}, {} }", TypeName(key), TypeName(value)));
    text = strings.ReplaceAll(text, "$K", TypeName(key));
    text = strings.ReplaceAll(text, "$V", TypeName(value));
    return format_text("{}\n", strings.ReplaceAll(text, "$N", format_text("{}", type)));
}
// The helpers of the map types `code` works on, once each.
MapModule :: (code: Instruction[]) -> string {
    types := MapTypes(code);
    if (length(types) == 0) {
        return "";
    }
    out: string[] = [MapShared()];
    for (type in types) {
        out.append(MapHelpers(type));
    }
    return format_text("\n{}", strings.Join(out, "\n"));
}
'''
g = lit(GENERIC)+'\n'
t = lit(TYPED)
head = head.replace('@GENERIC@', lit(GENERIC)).replace('@TYPED@', lit(TYPED))
open('/Users/cwaldner/Personal/chibi/.worktrees/math-collections-integration/chibi/tools/compiler/llvm_native/Maps.chibi','w').write(head)
