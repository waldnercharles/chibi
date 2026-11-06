from pathlib import Path
p=Path('tools/compiler/scalar_llvm/Emit.chibi');s=p.read_text()
s=s.replace('return Scalar(type) || type == facts.StringType ||','return Scalar(type) || type == facts.StringType || type == facts.TaskType ||')
s=s.replace('''NativeType :: (type: int) -> int {
''','''NativeType :: (type: int) -> int {
    if (type == facts.TaskType) { return native.TaskType; }
''')
s=s.replace('''return ArrayTyped(operand.type) || operand.type == facts.StringType;''','''return ArrayTyped(operand.type) || operand.type == facts.StringType || operand.type == facts.TaskType;''')
s=s.replace('return ArrayTyped(type) || type == abi.TextStorage;', 'return ArrayTyped(type) || type == abi.TextStorage || type == native.TaskType;')
s=s.replace('''Giving :: (type: int, slot: int) -> native.Instruction {
''','''Giving :: (type: int, slot: int) -> native.Instruction {
    if (type == native.TaskType) { return Op(native.TaskDrop, type, 0, slot, 0, 0); }
''')
s=s.replace('''                found.slot_types.append(types[at]);
                next += 1;
''','''                found.slot_types.append(types[at]);
                next += 1;
                if (types[at] == native.TaskType) {
                    found.slot_types.append(native.TaskType);
                    found.code.extend([Op(native.Constant, native.TaskType, next, 0, 0, 0),
                                       Op(native.Save, native.TaskType, 0, next, 0, places[vars[at]])]);
                    next += 1;
                }
''',1)
s=s.replace('(operand.kind != facts.LiteralRow && operand.kind != facts.RefRow && operand.kind != facts.MapRow && operand.kind != facts.CallRow && operand.literal != 0)', '(operand.kind != facts.LiteralRow && operand.kind != facts.RefRow && operand.kind != facts.MapRow && operand.kind != facts.CallRow && operand.kind != facts.AwaitResultRow && operand.literal != 0)')
s=s.replace('stack[base] != facts.WordType', 'stack[base] != facts.TaskType').replace('wanted_type = facts.WordType;', 'wanted_type = facts.TaskType;')
s=s.replace('''        } else if (operand.kind == facts.CallRow) {
            if (operand.value''','''        } else if (operand.kind == facts.TaskRunningRow) {
            base := length(stack) - 1;
            if (operand.name != "" || operand.count != 1 || operand.value != 0 || base < floor ||
                stack[base] != facts.TaskType || operand.type != facts.BoolType) {
                return "scalar running test needs a task observer";
            }
            stack = slice(stack, 0, base);
        } else if (operand.kind == facts.AwaitResultRow) {
            base := length(stack) - 1;
            if (operand.name != "" || operand.count != 1 || base < floor || stack[base] != facts.TaskType ||
                operand.value < 0 || operand.value >= length(heads) || operand.literal < 0 || operand.literal > 1 ||
                UnionHead(rows, records, operand.type) < 0) {
                return "scalar task result differs from its task and union";
            }
            callee := rows[heads[operand.value]];
            union_at := UnionHead(rows, records, operand.type);
            success := VariantRow(rows, union_at, int(operand.literal));
            cancelled := VariantRow(rows, union_at, 1 - int(operand.literal));
            wanted := 1;
            if (callee.value == facts.FunctionVoid) { wanted = 0; }
            if (rows[union_at].count != 2 || success < 0 || cancelled < 0 || rows[success].count != wanted ||
                rows[cancelled].count != 0 || (wanted == 1 && PayloadType(rows, records, operand.type, 0) != callee.type)) {
                return "scalar task result success type differs from its waiting function";
            }
            stack = slice(stack, 0, base);
        } else if (operand.kind == facts.CallRow) {
            if (operand.value''',1)
# actual variant helper name to reconcile below
s=s.replace('!Valued(records, declaration.type) || declaration.value != 0 ||', '!Valued(records, declaration.type) ||\n        (declaration.value != 0 && (declaration.value != facts.FunctionVoid || declaration.type != facts.IntType)) ||')
s=s.replace('waits = waits || row.kind == facts.YieldRow || row.kind == facts.AwaitRow || row.kind == facts.DelayRow ||','waits = waits || row.kind == facts.YieldRow || row.kind == facts.AwaitRow || row.kind == facts.AwaitResultRow || row.kind == facts.DelayRow ||')
s=s.replace('''            if (row.kind == facts.CallRow) {
                if (row.literal''','''            if (row.kind == facts.AwaitResultRow && !waiting[row.value]) {
                return "scalar task result names a function that does not wait";
            }
            if (row.kind == facts.CallRow) {
                if (row.literal''',1)
# task parameters always own a separate observer, even while suspended
old='''            body.extend([Op(native.Argument, type, slot, index, 0, 0), Op(native.Save, type, 0, slot, 0, local)]);
            places.append(local);'''
new='''            body.append(Op(native.Argument, type, slot, index, 0, 0));
            if (type == native.TaskType) {
                slots.append(type);
                body.append(Op(native.TaskCopy, type, slot + 1, slot, 0, 0));
                slot += 1;
                owned_vars.append(index);
                owned_types.append(type);
            }
            body.append(Op(native.Save, type, 0, slot, 0, local));
            places.append(local);'''
assert old in s;s=s.replace(old,new,1)
s=s.replace('''                } else if (operand.kind == facts.CallRow) {
                    item = Op(native.Call, type, slot, operand.value, shape.arguments, operand.count);
                    item.literal = operand.literal;''','''                } else if (operand.kind == facts.TaskRunningRow || operand.kind == facts.AwaitResultRow) {
                    base := length(stack) - 1;
                    item = Op(native.TaskRunning, type, slot, stack[base], 0, 0);
                    if (operand.kind == facts.AwaitResultRow) {
                        callee := rows[heads[operand.value]];
                        success := NativeType(callee.type);
                        if (callee.value == facts.FunctionVoid) { success = 0; }
                        item = Op(native.AwaitResult, type, slot, stack[base], success, int(operand.literal));
                    }
                    stack = slice(stack, 0, base);
                } else if (operand.kind == facts.CallRow) {
                    item = Op(native.Call, type, slot, operand.value, shape.arguments, operand.count);
                    item.literal = operand.literal;
                    if (operand.literal == i64(facts.CallSpawned) && rows[heads[operand.value]].value == facts.FunctionVoid) {
                        item.literal = i64(native.CallSpawnedVoid);
                    }''',1)
s=s.replace('''            if (slots[shape.first_slot + value] == abi.TextStorage &&
''','''            if ((slots[shape.first_slot + value] == abi.TextStorage || slots[shape.first_slot + value] == native.TaskType) &&
''',1)
s=s.replace('''                    body.append(Op(native.TextRetain, abi.TextStorage, 0, value, 0, 0));
                    keeps = true;''','''                    if (slots[shape.first_slot + value] == native.TaskType) {
                        slots.append(native.TaskType);
                        body.append(Op(native.TaskCopy, native.TaskType, slot, value, 0, 0));
                        value = slot;
                        slot += 1;
                    } else { body.append(Op(native.TextRetain, abi.TextStorage, 0, value, 0, 0)); }
                    keeps = true;''',1)
s=s.replace('''                slots.append(abi.TextStorage);
                body.extend([Op(native.Local, abi.TextStorage, slot, places[row.value], 0, 0),
                             Op(native.TextRelease, abi.TextStorage, 0, slot, 0, 0)]);''','''                held_type := slots[shape.first_slot + value];
                slots.append(held_type);
                body.extend([Op(native.Local, held_type, slot, places[row.value], 0, 0), Giving(held_type, slot)]);''',1)
s=s.replace('''                if (slots[shape.first_slot + temporary] == abi.TextStorage) {
                    body.append(Op(native.TextRelease, abi.TextStorage, 0, temporary, 0, 0));''','''                if (slots[shape.first_slot + temporary] == abi.TextStorage || slots[shape.first_slot + temporary] == native.TaskType) {
                    body.append(Giving(slots[shape.first_slot + temporary], temporary));''',1)
s=s.replace('''                body.append(Op(native.Save, type, 0, value, 0, local));
                // A variable holding a fresh array''','''                if (type == native.TaskType && row.name == "_") {
                    slots.append(type);
                    body.extend([Op(native.Constant, type, slot, 0, 0, 0), Op(native.Save, type, 0, slot, 0, local)]);
                    slot += 1;
                } else { body.append(Op(native.Save, type, 0, value, 0, local)); }
                // A variable holding a fresh array''',1)
p.write_text(s)
