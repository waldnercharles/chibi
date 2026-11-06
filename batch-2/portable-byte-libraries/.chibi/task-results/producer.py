from pathlib import Path
p=Path('tools/compiler/package_compiler/Scalar.chibi');s=p.read_text()
s=s.replace('// A task handle is a word.','// Task observers retain their own type through lowering.').replace('return facts.WordType;\n    }\n    storages', 'return facts.TaskType;\n    }\n    storages')
s=s.replace('return declared.kind == OptionalDecl ||', 'return declared.kind == TaskResultDecl || declared.kind == OptionalDecl ||')
s=s.replace('if (kind == OptionalDecl || ((kind == RecordDecl', 'if (kind == TaskResultDecl || kind == OptionalDecl || ((kind == RecordDecl')
s=s.replace('// A task handle is a word whatever its function, which `record` names.','// A task observer keeps its success identity in source metadata.').replace('return facts.WordType;\n    }\n    if (container == MapContainer)', 'return facts.TaskType;\n    }\n    if (container == MapContainer)')
needle='''    if (Is(forest, value, syntax.Identifier)) {
        found := Lookup(names, Text(forest, text, value));'''
repl='''    if (Is(forest, value, syntax.AwaitExpression)) {
        awaited := Unparenthesized(forest, Field(forest, value, syntax.FieldValue));
        if (!Is(forest, awaited, syntax.CallExpression)) {
            current: Callable = {package: package};
            function := ScalarTaskCallee(forest, text, callables, names, current, awaited);
            return TaskResultRecord(types, function);
        }
    }
'''+needle
# avoid partially-initialized Callable by helper taking package? Chibi permits default omitted record fields.
s=s.replace(needle,repl,1)
s=s.replace('''        if (!Is(forest, awaited, syntax.CallExpression)) {
            return 0;
        }
        function := ScalarTaskCallee''','''        if (!Is(forest, awaited, syntax.CallExpression)) {
            function := ScalarTaskCallee(forest, text, callables, names, callable, awaited);
            if (function >= 0 && TaskResultRecord(types, function) >= 0) { return UnionStorage; }
            return 0;
        }
        function := ScalarTaskCallee''',1)
s=s.replace('''    if (Is(forest, node, syntax.Identifier)) {
        return ScalarVariable(names, Text(forest, text, node)).storage;
    }''','''    if (Is(forest, node, syntax.Identifier)) {
        binding := ScalarVariable(names, Text(forest, text, node));
        if (binding.storage == UnionStorage && binding.record >= 0 && types[binding.record].kind == TaskResultDecl &&
            TaskResultPresent(forest, text, binding, node)) {
            return TaskResultSuccess(types, binding.record).storage;
        }
        return binding.storage;
    }''',1)
s=s.replace('''        if (function < 0 || !callables[function].waits || !Is(forest, awaited, syntax.CallExpression)) {
            return ScalarOperands.Failed(Diagnose(forest, node, 0,
                                                  "scalar proof awaits a call of a waiting function, or a task as a statement"));
        }
        // The call is awaited: its rows give the result.''','''        if (function < 0 || !callables[function].waits) {
            return ScalarOperands.Failed(Diagnose(forest, node, 0, "native task result needs a known success type"));
        }
        if (!Is(forest, awaited, syntax.CallExpression)) {
            record := TaskResultRecord(types, function);
            if (record < 0) { return ScalarOperands.Failed(Diagnose(forest, node, 0, "native task result type is unsupported")); }
            switch ScalarOperand(forest, text, callables, params, callable, names, types, members, constants, awaited, TaskStorage, rows) {
                case .Failed(diagnostic): return ScalarOperands.Failed(diagnostic);
                case .Built(built):
                    output := built;
                    success := 1 - (FindVariant(types, record, "Cancelled") - types[record].first);
                    row := ScalarAt(forest, node, facts.AwaitResultRow, ScalarRecordType(types, record), function, 1, "");
                    row.literal = i64(success);
                    output.append(row);
                    return ScalarOperands.Built(output);
            }
        }
        // The call is awaited: its rows give the result.''',1)
s=s.replace('''        ordinal := ScalarVariable(names, Text(forest, text, node)).index;
        output.append(ScalarAt(forest, node, facts.ReadRow, type, ordinal, 0, ""));''','''        binding := ScalarVariable(names, Text(forest, text, node));
        ordinal := binding.index;
        if (binding.storage == UnionStorage && binding.record >= 0 && types[binding.record].kind == TaskResultDecl &&
            TaskResultPresent(forest, text, binding, node)) {
            output.append(ScalarAt(forest, node, facts.ReadRow, ScalarRecordType(types, binding.record), ordinal, 0, ""));
            output.append(ScalarAt(forest, node, facts.PayloadRow, type, 0, 1, ""));
        } else {
            output.append(ScalarAt(forest, node, facts.ReadRow, type, ordinal, 0, ""));
        }''',1)
s=s.replace('''    variant := FindVariant(types, held, Text(forest, text, Field(forest, pattern, syntax.FieldName)));
    operator := facts.Equal;''','''    variant := FindVariant(types, held, Text(forest, text, Field(forest, pattern, syntax.FieldName)));
    if (types[held].kind == TaskResultDecl && TaskResultPattern(forest, text, node)) {
        variant = FindVariant(types, held, "Cancelled");
    }
    operator := facts.Equal;''',1)
s=s.replace('''    if (own == storage) {
        return ScalarOperands.Built(rows);
    }
    // An optional''','''    if (own == storage) {
        return ScalarOperands.Built(rows);
    }
    if (own == TaskStorage && storage == BoolStorage) {
        output := rows;
        output.append(ScalarAt(forest, node, facts.TaskRunningRow, facts.BoolType, 0, 1, ""));
        return ScalarOperands.Built(output);
    }
    // An optional''',1)
s=s.replace('''    // Named results are locals after the parameters''','''    if (nothing && callable.waits) { rows[0].value = facts.FunctionVoid; }
    // Named results are locals after the parameters''',1)
s=s.replace('''        if (declared.kind == OptionalDecl) {''','''        if (declared.kind == TaskResultDecl) {
            rows.append(ScalarAt(forest, 0, facts.UnionRow, 0, 0, 2, format_text("taskresult{}", position)));
            for (ordinal in 0..<2) {
                variant := types[declared.first + ordinal];
                name := "Success";
                if (variant.name == "Cancelled") { name = "Cancelled"; }
                rows.append(ScalarAt(forest, 0, facts.VariantRow, 0, ordinal, variant.count, name));
                if (variant.count == 1) {
                    payload := ScalarValueType(types, declared.value_storage, declared.value_container, declared.value_record);
                    rows.append(ScalarAt(forest, 0, facts.MemberRow, payload, 0, 0, "value"));
                }
            }
            continue;
        }
        if (declared.kind == OptionalDecl) {''',1)
p.write_text(s)
