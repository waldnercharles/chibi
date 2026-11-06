from pathlib import Path
p=Path('tools/compiler/asset_request/Request.chibi')
old=p.read_text(); request=old[old.index('pub Request :: struct {'):old.index('Space :: (reader: ^Reader)')]
head='''package asset_request

import compiler "project:package_compiler"
import json "project:json_string"
import strings "core:strings"

// Read the existing catalog protocol once, without a JSON tree or source
// copies. Each object has a fixed schema; source and catalog arrays grow
// geometrically. Requests are limited to 64 MiB and nesting to 128 values.
Reader :: struct {
    text: string;
    at: int;
    good: bool;
}
'''+request
basic='''
Space :: (reader: ^Reader) {
    byte := strings.ByteAt(reader.text, reader.at);
    while (byte == 32 || byte == 9 || byte == 10 || byte == 13) {
        reader.at += 1;
        byte = strings.ByteAt(reader.text, reader.at);
    }
}
Expect :: (reader: ^Reader, byte: int) {
    Space(reader);
    if (strings.ByteAt(reader.text, reader.at) != byte) { reader.good = false; return; }
    reader.at += 1;
}
// At the beginning of each array/object item. A close consumes the delimiter;
// otherwise every item after the first requires one comma.
More :: (reader: ^Reader, close: int, first: bool) -> bool {
    Space(reader);
    if (!reader.good) { return false; }
    if (strings.ByteAt(reader.text, reader.at) == close) { reader.at += 1; return false; }
    if (!first) { Expect(reader, 44); }
    return reader.good;
}
Text :: (reader: ^Reader) -> string {
    Space(reader);
    start := reader.at;
    if (strings.ByteAt(reader.text, start) != 34) { reader.good = false; return ""; }
    reader.at += 1;
    while (reader.at < strings.ByteLength(reader.text)) {
        byte := strings.ByteAt(reader.text, reader.at);
        reader.at += 1;
        if (byte == 34) { break; }
        if (byte == 92) { reader.at += 1; }
    }
    decoded := json.Value(strings.Slice(reader.text, start, reader.at - start));
    if (!decoded.valid) { reader.good = false; }
    return decoded.value;
}
Literal :: (reader: ^Reader) -> string {
    Space(reader);
    start := reader.at;
    while (reader.at < strings.ByteLength(reader.text)) {
        byte := strings.ByteAt(reader.text, reader.at);
        if (byte == 44 || byte == 93 || byte == 125 || byte == 32 || byte == 9 || byte == 10 || byte == 13) { break; }
        reader.at += 1;
    }
    return strings.Slice(reader.text, start, reader.at - start);
}
Bool :: (reader: ^Reader) -> bool {
    text := Literal(reader);
    if (text != "true" && text != "false") { reader.good = false; }
    return text == "true";
}
Unsigned :: (reader: ^Reader) -> u64 {
    text := Literal(reader);
    size := strings.ByteLength(text);
    if (size == 0 || size > 20 || (size > 1 && strings.ByteAt(text, 0) == 48)) { reader.good = false; return 0; }
    value: u64 = 0;
    for (byte in strings.Bytes(text)) {
        digit := byte - 48;
        if (digit < 0 || digit > 9 || value > 1844674407370955161 || (value == 1844674407370955161 && digit > 5)) {
            reader.good = false;
            return 0;
        }
        value = value * 10 + u64(digit);
    }
    return value;
}
Integer :: (reader: ^Reader) -> int {
    text := Literal(reader);
    negative := strings.HasPrefix(text, "-");
    at := 0;
    if (negative) { at = 1; }
    size := strings.ByteLength(text);
    if (size <= at || size - at > 10 || (size - at > 1 && strings.ByteAt(text, at) == 48)) { reader.good = false; return 0; }
    value: i64 = 0;
    while (at < size) {
        digit := strings.ByteAt(text, at) - 48;
        if (digit < 0 || digit > 9) { reader.good = false; return 0; }
        value = value * 10 + i64(digit);
        at += 1;
    }
    if (negative) { value = -value; }
    if (value < -2147483648 || value > 2147483647) { reader.good = false; return 0; }
    return int(value);
}
'''
schemas={
'Layout':('Target', [('pointer_storage','int'),('pointer_size','int'),('value_size','int'),('payload_offset','int'),('arguments_offset','int'),('delta_seconds_offset','int'),('results_offset','int'),('error_status_offset','int'),('type_offset','int'),('container_offset','int'),('name_offset','int'),('record_offset','int'),('schema_offset','int'),('count_offset','int'),('items_offset','int'),('data_offset','int'),('stride_offset','int'),('map_count_offset','int'),('data_failure','int'),('argument_failure','int'),('assert_failure','int')]),
'Native':('CatalogNative',[('name','string'),('first','int'),('parameters','int'),('results','int'),('variadic','bool'),('direct','bool'),('qualified_name','string'),('kind','int'),('intrinsic','int'),('effects','int')]),
'Port':('CatalogPort',[('name','string'),('storage','int'),('container','int'),('c_string','bool'),('nominal','u64'),('has_default','bool'),('default_owner','bool'),('default_supported','bool'),('default_bits','u64'),('default_text','string')]),
'Type':('GameType',[('required','bool'),('underlying','int'),('name','string'),('id','u64'),('layout','u64'),('storage','int'),('size','int'),('component','u64'),('first','int'),('count','int'),('relationship','bool'),('traits','int'),('error','bool')]),
'Field':('GameField',[('name','string'),('storage','int'),('offset','int'),('writable','bool'),('c_string','bool'),('record','int'),('constant','bool'),('read','int'),('read_key','string')])}
zero={'int':'0','u64':'0','bool':'false','string':'""'}; read={'int':'Integer','u64':'Unsigned','bool':'Bool','string':'Text'}
body=head+basic
for name,(type,fields) in schemas.items():
 init=', '.join(k+': '+zero[t] for k,t in fields)
 body+=f'\n{name} :: (reader: ^Reader) -> compiler.{type} {{\n    value: compiler.{type} = {{{init}}};\n    Expect(reader, 123);\n    first := true;\n    seen := 0;\n    while (More(reader, 125, first)) {{\n        first = false;\n        key := Text(reader);\n        Expect(reader, 58);\n        bit := 0;\n'
 # Chibi switch string? source supports switch strings? likely switch only numeric? if chain works
 for i,(key,t) in enumerate(fields):
  body+=f'        '+('if' if i==0 else '} else if')+f' (key == "{key}") {{ value.{key} = {read[t]}(reader); bit = {1<<i};\n'
 body+='        } else { reader.good = false; }\n        if ((seen & bit) != 0) { reader.good = false; }\n        seen = seen | bit;\n    }\n'
 body+=f'    if (seen != {(1<<len(fields))-1}) {{ reader.good = false; }}\n    return value;\n}}\n'
for name,type,call in [('Strings','string','Text'),('UInts','u64','Unsigned'),('Ints','int','Integer'),('Natives','compiler.CatalogNative','Native'),('Ports','compiler.CatalogPort','Port'),('Types','compiler.GameType','Type'),('Fields','compiler.GameField','Field')]:
 body+=f'\n{name} :: (reader: ^Reader) -> {type}[] {{\n    values: {type}[] = [];\n    Expect(reader, 91);\n    first := true;\n    while (More(reader, 93, first)) {{\n        first = false;\n        values.append({call}(reader));\n    }}\n    return values;\n}}\n'
layoutinit=', '.join(k+': '+zero[t] for k,t in schemas['Layout'][1])
body+='''
pub Parse :: (text: string) -> Request {
    value: Request = {project: "", collections: [], roots: [], paths: [], sources: [], debug: false,
        target: {'''+layoutinit+'''},
        abi: 0, names: [], natives: [], ports: [], identities: [], types: [], fields: [], entity: 0, os: 0, arch: 0,
        effects: [], valid: false};
    if (strings.ByteLength(text) > 67108864) { return value; }
    reader: Reader = {text: text, at: 0, good: true};
    Expect(&reader, 91);
'''
props=[('project','Text'),('collections','Strings'),('roots','Strings'),('paths','Strings'),('sources','Strings'),('debug','Bool'),('target','Layout'),('abi','Unsigned'),('names','Strings'),('natives','Natives'),('ports','Ports'),('identities','UInts'),('types','Types'),('fields','Fields'),('entity','Unsigned'),('os','Integer'),('arch','Integer'),('effects','Ints')]
for i,(prop,readfn) in enumerate(props):
 if i>0: body+='    Expect(&reader, 44);\n'
 body+=f'    value.{prop} = {readfn}(&reader);\n'
body+='''    Expect(&reader, 93);
    Space(&reader);
    value.valid = reader.good && reader.at == strings.ByteLength(text) &&
                  length(value.collections) == length(value.roots) &&
                  length(value.paths) > 0 && length(value.paths) == length(value.sources) &&
                  length(value.identities) == length(value.natives) * 2 && length(value.effects) == length(value.natives);
    return value;
}
'''
p.write_text(body)
