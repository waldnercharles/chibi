from pathlib import Path
p=Path('tools/compiler/scalar_facts/Facts.chibi')
s=p.read_text().replace('pub FunctionAddressRow: int : 40;', '''pub FunctionAddressRow: int : 40;
// A postfix map operation. New consumes alternating key/value operands;
// changes consume map, key and optionally value, and give the replacement map.
// Reads use `literal` for their input map type and `type` for their result.
// A map's assignment or declaration copies through MapCopy before sharing.
pub MapRow: int : 50;''')
s=s.replace('pub ArrayType: int : 1024;', '''pub ArrayType: int : 1024;
// A map from scalar/string K to scalar/string V is MapType + V * 16 + K.
// Records and nested containers need their ownership rules before admission.
pub MapType: int : 2048;
pub MapNew: int : 1;
pub MapLength: int : 2;
pub MapGet: int : 3;
pub MapSet: int : 4;
pub MapHas: int : 5;
pub MapDelete: int : 6;
pub MapCopy: int : 7;
pub MapSpan: int : 8;
pub MapLiveAt: int : 9;
pub MapKeyAt: int : 10;
pub MapValueAt: int : 11;''')
p.write_text(s)
p=Path('tools/compiler/scalar_llvm/Emit.chibi')
s=p.read_text()
s=s.replace('''Valued :: (records: int[], type: int) -> bool {
    if (type >= facts.ArrayType)''','''Valued :: (records: int[], type: int) -> bool {
    if (type >= facts.MapType) {
        key := (type - facts.MapType) % 16;
        value := (type - facts.MapType) / 16;
        return (Integer(key) || key == facts.BoolType || key == facts.StringType) &&
               value >= facts.IntType && value <= facts.StringType;
    }
    if (type >= facts.ArrayType)''')
s=s.replace('''    if (element < facts.IntType || element >= facts.ArrayType || !Valued(records, element))''','''    if (type >= facts.MapType || element < facts.IntType || element >= facts.ArrayType || !Valued(records, element))''')
s=s.replace('''NativeType :: (type: int) -> int {
    if (type >= facts.ArrayType)''','''NativeType :: (type: int) -> int {
    if (type >= facts.MapType) {
        return native.MapOf(NativeType((type - facts.MapType) % 16), NativeType((type - facts.MapType) / 16));
    }
    if (type >= facts.ArrayType)''')
s=s.replace('''    if (operand.kind != facts.ArrayRow) {
        return false;
    }''','''    if (operand.kind == facts.MapRow) {
        return operand.value == facts.MapNew || operand.value == facts.MapCopy ||
               ((operand.value == facts.MapGet || operand.value == facts.MapKeyAt || operand.value == facts.MapValueAt) &&
                operand.type == facts.StringType);
    }
    if (operand.kind != facts.ArrayRow) {
        return false;
    }''',1)
s=s.replace('''    return Op(native.ArrayDrop, type, 0, slot, 0, 0);
}''','''    if (type >= native.MapType) {
        return Op(native.MapDrop, type, 0, slot, 0, 0);
    }
    return Op(native.ArrayDrop, type, 0, slot, 0, 0);
}''',1)
s=s.replace('(operand.kind != facts.LiteralRow && operand.literal != 0)', '(operand.kind != facts.LiteralRow && operand.kind != facts.MapRow && operand.literal != 0)')
needle='''        } else if (operand.kind == facts.ArrayRow) {
            error := ValidArray'''
repl='''        } else if (operand.kind == facts.MapRow) {
            error := ValidMap(rows, records, at, row, last, operand, stack, floor);
            if (error != "") {
                return error;
            }
            stack = slice(stack, 0, length(stack) - MapOperands(operand));
        } else if (operand.kind == facts.ArrayRow) {
            error := ValidArray'''
s=s.replace(needle,repl,1)
a=s.index('// Whether the Array row at `row`')
s=s[:a]+'''// The postfix operands of one map operator; -1 means no such operator.
MapOperands :: (operand: facts.Row) -> int {
    if (operand.value == facts.MapNew) {
        return operand.count;
    }
    if (operand.value == facts.MapLength || operand.value == facts.MapCopy || operand.value == facts.MapSpan) {
        return 1;
    }
    if (operand.value == facts.MapSet) {
        return 3;
    }
    if (operand.value >= facts.MapGet && operand.value <= facts.MapValueAt) {
        return 2;
    }
    return -1;
}
// Validate maps independently of array encoding and mutation rules. Each change
// must end an assignment of the map it first reads, so no descriptor aliases
// an allocation while another descriptor mutates it.
ValidMap :: (rows: facts.Row[], records: int[], at: int, row: int, last: int, operand: facts.Row, stack: int[],
             floor: int) -> string {
    consumed := MapOperands(operand);
    base := length(stack) - consumed;
    mismatch := "scalar map operation differs from its operands";
    changed := operand.value == facts.MapSet || operand.value == facts.MapDelete;
    mapped := operand.type;
    if (operand.value != facts.MapNew && operand.value != facts.MapCopy && !changed) {
        mapped = int(operand.literal);
    } else if (operand.literal != 0) {
        return mismatch;
    }
    if (operand.name != "" || consumed < 0 || operand.count != consumed || base < floor ||
        mapped < facts.MapType || !Valued(records, mapped)) {
        return mismatch;
    }
    key := (mapped - facts.MapType) % 16;
    value := (mapped - facts.MapType) / 16;
    if (operand.value == facts.MapNew) {
        if (consumed % 2 != 0) {
            return mismatch;
        }
        for (index in 0..<consumed) {
            wanted := key;
            if (index % 2 == 1) {
                wanted = value;
            }
            if (stack[base + index] != wanted) {
                return mismatch;
            }
        }
        return "";
    }
    if (stack[base] != mapped) {
        return mismatch;
    }
    if ((operand.value == facts.MapLength || operand.value == facts.MapSpan) && operand.type != facts.IntType) {
        return mismatch;
    }
    if ((operand.value == facts.MapHas || operand.value == facts.MapLiveAt) && operand.type != facts.BoolType) {
        return mismatch;
    }
    if ((operand.value == facts.MapGet || operand.value == facts.MapValueAt) && operand.type != value) {
        return mismatch;
    }
    if (operand.value == facts.MapKeyAt && operand.type != key) {
        return mismatch;
    }
    if (consumed >= 2) {
        wanted := key;
        if (operand.value == facts.MapLiveAt || operand.value == facts.MapKeyAt || operand.value == facts.MapValueAt) {
            wanted = facts.IntType;
        }
        if (stack[base + 1] != wanted) {
            return mismatch;
        }
    }
    if (operand.value == facts.MapSet && stack[base + 2] != value) {
        return mismatch;
    }
    if (changed && (rows[at].kind != facts.AssignRow || base != 0 || row != last - 1 ||
                    rows[at + 1].kind != facts.ReadRow || rows[at + 1].value != rows[at].value)) {
        return "scalar map change is not the assignment of its map";
    }
    return "";
}

'''+s[a:]
a=s.index('// One native instruction without a literal.')
s=s[:a]+'''// The native instruction for a map operator other than its literal.
MapLowered :: (operand: facts.Row, type: int, slot: int, stack: int[], base: int) -> native.Instruction {
    ops := [0, 0, native.MapLength, native.MapGet, native.MapSet, native.MapHas, native.MapDelete,
            native.MapCopy, native.MapSpan, native.MapLiveAt, native.MapKeyAt, native.MapValueAt];
    right := 0;
    target := 0;
    if (MapOperands(operand) >= 2) {
        right = stack[base + 1];
    }
    if (operand.value == facts.MapSet) {
        target = stack[base + 2];
    }
    item := Op(ops[operand.value], type, slot, stack[base], right, target);
    if (operand.literal != 0) {
        item.literal = i64(NativeType(int(operand.literal)));
    }
    return item;
}

'''+s[a:]
needle='''                } else if (operand.kind == facts.ArrayRow) {
                    base := length(stack) - ArrayOperands(operand);'''
repl='''                } else if (operand.kind == facts.MapRow) {
                    base := length(stack) - MapOperands(operand);
                    op := operand.value;
                    if (op == facts.MapNew) {
                        item = Op(native.Constant, type, slot, 0, 0, 0);
                        for (pair in 0..<operand.count / 2) {
                            key := stack[base + pair * 2];
                            value := stack[base + pair * 2 + 1];
                            for (kept in [key, value]) {
                                if (slots[shape.first_slot + kept] == abi.TextStorage) {
                                    stored.append(kept);
                                }
                            }
                            body.append(item);
                            slots.append(type);
                            item = Op(native.MapSet, type, slot + 1, slot, key, value);
                            item.literal = 1;
                            slot += 1;
                        }
                    } else {
                        item = MapLowered(operand, type, slot, stack, base);
                        if (op == facts.MapSet) {
                            for (kept in [stack[base + 1], stack[base + 2]]) {
                                if (slots[shape.first_slot + kept] == abi.TextStorage) {
                                    stored.append(kept);
                                }
                            }
                        }
                        if ((op == facts.MapSet || op == facts.MapDelete) && Holds(owned_vars, row.value)) {
                            item.literal = 1;
                        }
                        if (op != facts.MapSet && op != facts.MapDelete && Holds(fresh, stack[base])) {
                            pending.append(stack[base]);
                        }
                        // A string read borrows from its map. Retain it before
                        // a fresh input map is dropped, then track the result
                        // as fresh so the statement either keeps or releases it.
                        if ((op == facts.MapGet || op == facts.MapKeyAt || op == facts.MapValueAt) && type == abi.TextStorage) {
                            body.append(item);
                            slots.append(type);
                            body.append(Op(native.TextRetain, type, 0, slot, 0, 0));
                            item = Op(native.Local, type, slot + 1, local, 0, 0);
                            locals.append(type);
                            body.append(Op(native.Save, type, 0, slot, 0, local));
                            local += 1;
                            slot += 1;
                        }
                    }
                    stack = slice(stack, 0, base);
                } else if (operand.kind == facts.ArrayRow) {
                    base := length(stack) - ArrayOperands(operand);'''
s=s.replace(needle,repl,1)
# Generic owning containers share lifetime tracking, but use their own drops.
s=s.replace('Op(native.ArrayDrop, payloads.types[index], 0, at + 3, 0, 0)','Giving(payloads.types[index], at + 3)')
s=s.replace('Op(native.ArrayDrop, frame.held_type, 0, next, 0, 0)','Giving(frame.held_type, next)')
s=s.replace('Op(native.ArrayDrop, frame.held_type, 0, slot, 0, 0)','Giving(frame.held_type, slot)')
s=s.replace('Op(native.ArrayDrop, held_type, 0, spent, 0, 0)','Giving(held_type, spent)')
s=s.replace('Op(native.ArrayDrop, type, 0, value, 0, 0)','Giving(type, value)')
s=s.replace('Op(native.ArrayDrop, type, 0, slot, 0, 0)]','Giving(type, slot)]')
p.write_text(s)
