#!/usr/bin/env python3
"""AUTHOR-ONLY, NOT RUN: create a disposable counted sibling of typed preparation.

Usage after independent review and execution authorization:
  python instrument_prepared.py EXISTING_TYPED_PREPARATION NEW_OUTPUT
Never imports or executes a project preparer, compiler or test. No cap change.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import stat

HERE = Path(__file__).resolve().parent
PINS = {
    "project/vnext_typed/Model.chibi": "dd157ed822eda9840c5895a48e9bb59beea559b48d2995fb340b8a89263b16eb",
    "project/vnext_typed/Produce.chibi": "cd668a0fd2fa8ce0cecdc5bd0cae5540c5c4f9387838a7baca698c74b7e2b714",
    "project/vnext_typed/Verify.chibi": "f21adba7cb2795e3223e0f647129eff4bf3b1224fa9e2b3eef931aca3ca61487",
    "inputs/Main.chibi.in": "3394f025cd3423c7878c1d09dd6cabe071848f419cd5ba69ed399cd6f3ef10b9",
    "project/source_tokenizer/Tokenizer.chibi": "88234ded25b3278c591602ae0346be3ce63c90130f4b8b0869a0f80c3a5e6072",
    "inputs/source_census/Main.chibi": "0e44190699438f78b223c71a380a903f71526941f0a5dc700c655b62be341f16",
    "core/strings/Strings.chibi": "6fc25eedd2eee12631d2e22c8d075b82d7ee7561ef329e4c76846add13d44586",
}
CORE_PINS = {
    'os/Os.chibi': 'ca7eb34c1d4a0b46377f2d3148079d741a0bdd6db431cd99ade6570c7f09c63c',
    'target/Target.chibi': '9c53c8eed58c028f7c575dea4e91978daaa4f996ba37605527caa19b6c151a93',
    'sys/posix/Posix.chibi': 'e6c0af3b80b8347550e3d9566952d71df3d2dfd0a9550040db6919e93adb0ae0',
    'sys/windows/Decimal.chibi': '75f9b4492cd791fe1dd7328e859dd856043f7c0a4c7a197295007545c3fa1cd4',
    'sys/windows/Windows.chibi': '11652d1d2b3ebd20026f2d5e1090c209e9b74b633680243788b5fb0120a0b6c5',
    'hash/sha256/Sha256.chibi': '7d214328124b1452b89a17122196d6e32486437c16db4febde7d04f51362409d',
    'bytes/Bytes.chibi': '3face4437c9ebd31073da5c8e2996f956dd1fa0105eefc1fe458c3f17f20bb4b',
}
CORE_IMPORTS = {
    'core:os': ['core:bytes', 'core:hash/sha256', 'core:sys/posix', 'core:sys/windows', 'core:target'],
    'core:target': [], 'core:sys/posix': ['core:target'],
    'core:sys/windows': ['core:strings'], 'core:hash/sha256': ['core:bytes'],
    'core:bytes': ['core:strings'],
}
# Strings/comments are opaque; rename identifiers only, never diagnostic text,
# source literals, imported paths or quoted vocabulary.
TOKENS = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_][A-Za-z_0-9]*', re.S)


def once(text, before, after):
    if text.count(before) != 1:
        raise ValueError(f"expected one reviewed anchor: {before!r}")
    return text.replace(before, after, 1)


def counted_producer(source):
    source = once(source, 'pub Source :: struct { path: string; text: string; document: int; }\n', '')
    # Every top-level name from this file gets a private sibling except its
    # shared Source input and Model types. Existing production files stay exact.
    names = set(re.findall(r'^(?:pub )?([A-Za-z_][A-Za-z_0-9]*)\s*(?:::|:)', source, re.M))
    source = once(source, 'State :: struct {', 'State :: struct {\n    meter: WorkCounts;')
    source = once(source, 'pub Produce :: (text: string, document: int) -> Result {',
                  'pub Produce :: (text: string, document: int, meter: ^WorkCounts) -> Result {')
    source = once(source, 'return ProduceSources(sources);', 'return ProduceSources(sources, meter);')
    source = once(source, 'pub ProduceSources :: (sources: Source[]) -> Result {',
                  'pub ProduceSources :: (sources: Source[], meter: ^WorkCounts) -> Result {')
    source = once(source, 'pass_state: State = {tree:', 'pass_state: State = {meter: meter^, tree:')
    source = once(source, 'node_count += length(nodes);',
                  'node_count += length(nodes);\n                meter.parser_nodes += length(nodes);')
    for signature, counter in (
        ('CollectWrite :: (s: ^State, statement: int, depth: int) {', 'prepass_statements'),
        ('Expression :: (s: ^State, output: ^Module, node: int, depth: int) -> u32 {', 'expression_entries'),
        ('Emit :: (s: ^State, output: ^Module, op: u32, type: u32, left: u32, right: u32, payload: u32, node: int) -> u32 {', 'emit_calls'),
    ):
        source = once(source, signature, signature + '\n    s.meter.' + counter + ' += 1;')
    source = once(source, '        slot := FindBinding(s, target);',
                  '        s.meter.assignment_resolutions += 1;\n        slot := FindBinding(s, target);')
    source = once(source, '                row.mask |= u64(1) << u32(slot);',
                  '                s.meter.mask_updates += 1;\n                row.mask |= u64(1) << u32(slot);')
    source = once(source, '        kind := s.nodes[statement].kind;',
                  '        s.meter.emitted_statements += 1;\n        kind := s.nodes[statement].kind;')
    source = once(source, '    header_values := s.current_values;\n    condition := Expression',
                  '    header_values := s.current_values;\n    s.meter.while_conditions += 1;\n    condition := Expression')
    source = once(source, '    FinishEdge(output, body_edge, body, empty);\n    Statements',
                  '    FinishEdge(output, body_edge, body, empty);\n    s.meter.while_bodies += 1;\n    Statements')
    source = once(source, '        pass_state.first_block = length(output.blocks);', '''        meter_values := length(output.values);
        meter_instructions := length(output.instructions);
        meter_edges := length(output.edges);
        meter_arguments := length(output.arguments);
        meter_parameters := length(output.block_parameters);
        pass_state.first_block = length(output.blocks);''')
    source = once(source, '        Statements(&pass_state, &output, body, 0);', '''        Statements(&pass_state, &output, body, 0);
        meter_masks: u64[] = [];
        for (write_row in pass_state.loop_writes) { meter_masks.append(write_row.mask); }
        meter_function: WorkFunction = {
            function_id: function_index + 1,
            complete: length(pass_state.diagnostics) == 0 && pass_state.block == 0,
            blocks: length(output.blocks) - pass_state.first_block,
            values: length(output.values) - meter_values,
            instructions: length(output.instructions) - meter_instructions,
            edges: length(output.edges) - meter_edges,
            arguments: length(output.arguments) - meter_arguments,
            parameters: length(output.block_parameters) - meter_parameters,
            signature_parameters: int(signature.parameter_count),
            assignment_rows: length(pass_state.assignment_targets),
            loop_rows: length(pass_state.loop_writes), write_masks: meter_masks};
        pass_state.meter.functions.append(meter_function);''')
    # Keep counts by value in invocation State, not a stored mutable pointer:
    # the retained seed already rejected the former stored-Module-pointer shape.
    # Publish once at every post-state return, including diagnostic exits.
    boundary = '    // Collect the one ordinary name catalog before reading any annotation.'
    if source.count(boundary) != 1:
        raise ValueError('producer state boundary changed')
    head, tail = source.split(boundary)
    if tail.count('return Result.') != 7 or sum(match[0] == 'return' for match in TOKENS.finditer(tail)) != 7:
        raise ValueError('producer post-state returns changed; review publication points')
    tail = tail.replace('return Result.', 'meter^ = pass_state.meter; return Result.')
    source = head + boundary + tail
    return TOKENS.sub(lambda match: 'Meter' + match[0] if match[0] in names else match[0], source)


def counted_verifier(source):
    # Helpers stay shared, immutable and uninstrumented. Only the operation's
    # body is duplicated; this is not a second implementation to maintain.
    signature = 'pub Verify :: (module: Module) -> int {'
    if source.count(signature) != 1:
        raise ValueError('verifier entry changed')
    source = 'package vnext_typed\n\n' + source[source.index(signature):]
    source = once(source, signature, 'pub MeterVerify :: (module: Module, meter: ^WorkCounts) -> int {')
    source = once(source, '            block := module.blocks[worklist[work_count]];',
                  '            meter.reachability_pops += 1;\n            block := module.blocks[worklist[work_count]];')
    source = once(source, '            block_index := worklist[work_count];',
                  '            meter.dominance_pops += 1;\n            block_index := worklist[work_count];')
    source = once(source, '                source := predecessor_sources[edge_index];',
                  '                meter.predecessor_reads += 1;\n                source := predecessor_sources[edge_index];')
    source = once(source, '                low &= dominators_low[source];',
                  '                meter.word_intersections += 1;\n                low &= dominators_low[source];')
    source = once(source, '                high &= dominators_high[source];',
                  '                meter.word_intersections += 1;\n                high &= dominators_high[source];')
    return source


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_regular(root, name):
    path = root / name
    if Path(name).is_absolute() or any(part in ('', '.', '..') for part in name.split('/')):
        raise ValueError('manifest requires canonical relative names')
    if path.resolve() != path or not stat.S_ISREG(path.lstat().st_mode):
        raise ValueError(f'not an ordinary nonsymlink input: {name}')
    return path.read_bytes()


def prepare(original, output):
    original = Path(original).absolute()
    output = Path(output).absolute()
    if original.resolve() != original or output.resolve() != output:
        raise ValueError('symlinked paths are not accepted')
    if output.exists() or output.is_symlink() or original in output.parents:
        raise ValueError('output must be a new directory outside the input tree')
    manifest_bytes = read_regular(original, 'source-manifest.json')
    manifest = json.loads(manifest_bytes)
    data = {}
    for row in manifest['files']:
        name = row['path']
        if name in data:
            raise ValueError('duplicate manifest path')
        value = read_regular(original, name)
        if digest(value) != row['sha256'] or len(value) != row['bytes']:
            raise ValueError(f'prepared bytes mismatch: {name}')
        data[name] = value
    paths = list(original.rglob('*'))
    if any(path.is_symlink() for path in paths):
        raise ValueError('symlinks are not accepted in a prepared tree')
    actual = {str(path.relative_to(original)) for path in paths if not path.is_dir()}
    if actual != set(data) | {'source-manifest.json'}:
        raise ValueError('unexpected prepared files; use source-only typed preparation')
    for name, expected in PINS.items():
        if digest(data[name]) != expected:
            raise ValueError(f'reviewed source identity changed: {name}')
    # Independently rebind the pinned retained test template, preserving its
    # CheckEqual and layout helpers rather than trusting an arbitrary Main.
    fixtures = {
        'CENSUS_SOURCE': 'inputs/source_census/Main.chibi',
        'FIXTURE_SOURCE': 'inputs/EchoJoin.chibi',
        'PACKAGE_MAIN_SOURCE': 'inputs/modules/Main.chibi',
        'PACKAGE_MATH_SOURCE': 'inputs/modules/math/Math.chibi',
        'FOREIGN_SOURCE': 'inputs/foreign/Main.chibi',
        'MISSING_SYMBOL_SOURCE': 'inputs/missing_symbol.chibi',
        'PRIVATE_IMPORT_SOURCE': 'inputs/private_import.chibi',
    }
    substitutions = {marker: json.dumps(data[name].decode(), ensure_ascii=False) for marker, name in fixtures.items()}
    template = data['inputs/Main.chibi.in'].decode()
    if any(template.count(marker) != 1 for marker in substitutions):
        raise ValueError('retained fixture marker count changed')
    expected_main = re.sub('|'.join(substitutions), lambda match: substitutions[match[0]], template)
    if expected_main.encode() != data['Main.chibi']:
        raise ValueError('prepared Main differs from the pinned test template and input bytes')
    snapshot = HERE / 'core-snapshot'
    snapshot_paths = list(snapshot.rglob('*'))
    if any(path.is_symlink() for path in snapshot_paths):
        raise ValueError('core snapshot must not contain symlinks')
    if {str(path.relative_to(snapshot)) for path in snapshot_paths if path.is_file()} != set(CORE_PINS):
        raise ValueError('core snapshot membership changed')
    for name, expected in CORE_PINS.items():
        value = read_regular(snapshot, name)
        if digest(value) != expected or 'core/' + name in data:
            raise ValueError(f'core snapshot identity or destination changed: {name}')
        data['core/' + name] = value
    graph = dict(manifest['imports'])
    if set(graph) & set(CORE_IMPORTS):
        raise ValueError('unexpected preexisting output package')
    graph.update(CORE_IMPORTS)
    production = 'project/vnext_typed/'
    generated = {
        production + 'MeterProduce.chibi': counted_producer(data[production + 'Produce.chibi'].decode()),
        production + 'MeterVerify.chibi': counted_verifier(data[production + 'Verify.chibi'].decode()),
        production + 'Counts.chibi': (HERE / 'Counts.chibi').read_text(),
        'WorkOutput.chibi': (HERE / 'WorkOutput.chibi').read_text(),
    }
    probe = (HERE / 'WorkProbe.chibi.in').read_text()
    probe = once(probe, 'WORK_CENSUS_LITERAL', json.dumps(data['inputs/source_census/Main.chibi'].decode(), ensure_ascii=False))
    main = data['Main.chibi'].decode()
    main = once(main, 'Main :: () -> int {', '''Main :: () -> int {
    work_report := RunWorkProbes();
    PrintWorkReport(work_report);
    assert(length(work_report.producer) == 2, "work-count registration ran");''')
    main = once(main, '    return 0;\n}\n\nAccepted ::',
                '    WorkRegressionPassed();\n    return 0;\n}\n\nAccepted ::')
    generated['Main.chibi'] = main + probe
    data.update({name: value.encode() for name, value in generated.items()})
    # Preserve the former receipt as provenance, never label added sources with
    # its old manifest. Existing closure/imports and all input literals survive.
    data['inputs/uncounted-source-manifest.json'] = manifest_bytes
    receipt = {
        'schema': 1, 'purpose': 'disposable test-only operation counters; no production admission',
        'files': [{'path': name, 'bytes': len(value), 'sha256': digest(value)} for name, value in sorted(data.items())],
        'imports': graph, 'base_source_pins': PINS, 'core_snapshot_pins': CORE_PINS,
        'root_imports': ['core:os', 'core:strings', 'core:target', 'project:vnext_typed'],
        'instrumentation': [{'path': name, 'sha256': digest((HERE / name).read_bytes())}
                            for name in ('instrument_prepared.py', 'Counts.chibi', 'WorkProbe.chibi.in', 'WorkOutput.chibi')],
        'compile': 'not_run', 'typed_tests': 'not_run', 'observed_counts': None,
        'max_blocks': 64, 'accepted_v4_digest': None,
        'stdout_contract': 'CW1; native Linux x86-64 only; report-end and regressions passed plus exit 0 required',
    }
    output.mkdir(parents=True, exist_ok=False)
    for name, value in sorted(data.items()):
        destination = output / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        with destination.open('xb') as stream:
            stream.write(value)
        if destination.read_bytes() != value:
            raise OSError(f'written bytes changed: {name}')
    with (output / 'source-manifest.json').open('x') as stream:
        json.dump(receipt, stream, indent=2, allow_nan=False)
        stream.write('\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('original', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    prepare(args.original, args.output)
