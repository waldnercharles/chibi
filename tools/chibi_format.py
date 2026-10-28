#!/usr/bin/env python3
"""Check/format every tracked .chibi using the shared native formatter."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = 'tools/format/exceptions.json'
FORMAT_INPUTS = ('tools/chibi_format.py', 'tools/format/build.py', 'tools/format/main.c',
                 'src/authoring/source_format.c', 'src/authoring/source_format.h',
                 'src/authoring/source_format_file.c', 'src/authoring/source_format_file.h',
                 'src/language/syntax.c', 'src/language/syntax.h',
                 'src/language/src/parser.c', 'src/language/src/scanner.c',
                 'src/support/array.h', 'src/support/xalloc.c', 'src/support/xalloc.h',
                 'src/language/src/tree_sitter/parser.h', 'src/language/src/tree_sitter/alloc.h',
                 'src/language/src/tree_sitter/array.h', 'third_party/CMakeLists.txt')


def git(root, *arguments):
    return subprocess.check_output(['git', '-C', str(root), *arguments])


def inventory(root):
    raw = git(root, 'ls-files', '-z', '--stage')
    entries = {}
    for record in raw.split(b'\0'):
        if not record:
            continue
        metadata, raw_path = record.split(b'\t', 1)
        mode, sha, stage = metadata.decode('ascii').split()
        path = os.fsdecode(raw_path)
        if stage != '0':
            raise ValueError(f'unmerged index entry: {path}')
        entries[path] = mode
    return entries


def safe_path(path):
    p = PurePosixPath(path)
    return bool(path) and not p.is_absolute() and '..' not in p.parts and str(p) == path


def validate_manifest(data, paths):
    if set(data) != {'version', 'exact_bytes', 'generated'} or data['version'] != 1:
        raise ValueError('unsupported formatter exception manifest')
    if not isinstance(data['exact_bytes'], dict) or not isinstance(data['generated'], dict):
        raise ValueError('formatter exception groups must be objects')
    if set(data['exact_bytes']) & set(data['generated']):
        raise ValueError('a path cannot be both generated and an exact-byte fixture')
    for kind in ('exact_bytes', 'generated'):
        for path, entry in data[kind].items():
            if not safe_path(path) or path not in paths or not isinstance(entry, dict):
                raise ValueError(f'stale or invalid formatter exception: {path}')
            keys = {'sha256', 'reason', 'test'} if kind == 'exact_bytes' else {'reason', 'check'}
            if set(entry) != keys or not isinstance(entry['reason'], str) or not entry['reason'].strip():
                raise ValueError(f'exception needs a narrow reason and {keys}: {path}')
            if kind == 'exact_bytes':
                digest = entry['sha256']
                if not isinstance(digest, str) or len(digest) != 64 or any(c not in '0123456789abcdef' for c in digest):
                    raise ValueError(f'invalid exact-byte SHA-256: {path}')
                if not isinstance(entry['test'], str) or not safe_path(entry['test']):
                    raise ValueError(f'exact-byte fixture needs its owning regression: {path}')
            else:
                if (not isinstance(entry['check'], list) or not entry['check'] or
                        any(not isinstance(x, str) or not x for x in entry['check'])):
                    raise ValueError(f'generated source needs a generator check command: {path}')


def formatter():
    spec = importlib.util.spec_from_file_location('chibi_format_build', ROOT / 'tools/format/build.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.build()


def generated_checks(root, entries, generated):
    """Generator check bugs cannot mutate the user's live working tree."""
    errors = []
    if not generated:
        return errors
    with tempfile.TemporaryDirectory(prefix='chibi-format-generated-') as directory:
        snapshot = Path(directory)
        # Full tracked inputs, no untracked build products or external symlinks.
        for name, mode in entries.items():
            source = root / name
            if mode not in ('100644', '100755') or source.is_symlink() or not source.is_file():
                errors.append(f'{name}: generator snapshot requires regular tracked inputs')
                continue
            target = snapshot / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(source.read_bytes())
            target.chmod(0o755 if mode == '100755' else 0o644)
        if errors:
            return errors
        (snapshot / '.git').write_text('formatter generator snapshot boundary\n')
        before = {name: (snapshot / name).read_bytes() for name in entries}
        commands = dict.fromkeys(tuple(entry['check']) for entry in generated.values())
        for command in commands:
            result = subprocess.run(command, cwd=snapshot, check=False, capture_output=True)
            if result.returncode:
                errors.append(f'generator consistency command failed: {command!r}')
        for name, original in before.items():
            path = snapshot / name
            if path.is_symlink() or not path.is_file() or path.read_bytes() != original:
                errors.append(f'{name}: generator consistency check modified a tracked input/output')
    return errors


def check_tree(root, binary, entries, write=False):
    paths = sorted(path for path in entries if path.endswith('.chibi'))
    manifest = json.loads((root / MANIFEST).read_text())
    validate_manifest(manifest, paths)
    errors = generated_checks(root, entries, manifest['generated'])
    changed, exact, produced = [], 0, 0
    outputs = {}
    for name in paths:
        path = root / name
        if entries[name] not in ('100644', '100755') or path.is_symlink() or not path.is_file():
            errors.append(f'{name}: tracked .chibi must be a regular file')
            continue
        before = path.read_bytes()
        generated = name in manifest['generated']
        produced += generated
        result = subprocess.run([str(binary), '--stdin', str(path)], input=before, capture_output=True)
        if name in manifest['exact_bytes']:
            entry = manifest['exact_bytes'][name]
            if not (root / entry['test']).is_file():
                errors.append(f'{name}: owning regression is missing')
            if hashlib.sha256(before).hexdigest() != entry['sha256']:
                errors.append(f'{name}: exact historical fixture changed; review migration oracle, do not format it')
            if result.returncode != 1 or b'Cannot format source with syntax errors' not in result.stderr:
                errors.append(f'{name}: expected current-syntax rejection changed; review/remove exception')
            exact += 1
            continue
        if result.returncode:
            errors.append(f'{name}: {result.stderr.decode(errors="replace").strip()}')
            continue
        # A formatter change must remain stable across the whole authored corpus.
        again = subprocess.run([str(binary), '--stdin', str(path)], input=result.stdout, capture_output=True)
        if again.returncode or again.stdout != result.stdout:
            errors.append(f'{name}: formatter is not idempotent')
            continue
        if before != result.stdout:
            if generated:
                errors.append(f'{name}: generator emits noncanonical source; fix the generator and regenerate')
                continue
            changed.append(name)
            outputs[name] = (before, result.stdout)
    # Never rewrite a subset when another source or exception failed validation.
    if write and not errors:
        for name, (before, after) in outputs.items():
            path = root / name
            if path.is_symlink() or path.read_bytes() != before:
                errors.append(f'{name}: source changed during formatting')
                break
        if not errors:
            for name, (before, after) in outputs.items():
                path = root / name
                temporary = None
                try:
                    with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.chibi-format-', delete=False) as stream:
                        temporary = Path(stream.name)
                        stream.write(after)
                    temporary.chmod(path.stat().st_mode & 0o777)
                    if path.is_symlink() or path.read_bytes() != before:
                        raise ValueError(f'{name}: source changed during formatting')
                    temporary.replace(path)
                finally:
                    if temporary is not None:
                        temporary.unlink(missing_ok=True)
    for message in errors:
        print('format: ' + message, file=sys.stderr)
    for name in changed:
        print(('formatted ' if write and not errors else 'not canonical: ') + name)
    print(f'Checked {len(paths)} tracked .chibi files: {exact} exact-byte fixtures, '
          f'{produced} generated, {len(changed)} formatting differences.')
    return 1 if errors or (changed and not write) else 0


def staged(binary, entries):
    # The native formatter and orchestration must themselves be the staged
    # versions. Do not silently test index source using unstaged formatter code.
    checked_inputs = set(FORMAT_INPUTS)
    build_stamp = binary.with_suffix('.json')
    if build_stamp.is_file():
        identity = json.loads(build_stamp.read_text())
        runtime = Path(identity['runtime_root'])
        for input_path in identity.get('inputs', {}):
            input_path = Path(input_path)
            if input_path == runtime or runtime in input_path.parents:
                continue  # external dependency ownership, even when cached below ROOT
            try:
                checked_inputs.add(input_path.relative_to(ROOT).as_posix())
            except ValueError:
                pass  # pinned external tree-sitter inputs are checked by the builder
    for name in sorted(checked_inputs):
        if name not in entries or git(ROOT, 'show', ':' + name) != (ROOT / name).read_bytes():
            raise ValueError(f'{name}: stage/stash formatter changes before checking the index')
    with tempfile.TemporaryDirectory(prefix='chibi-format-index-') as directory:
        snapshot = Path(directory)
        (snapshot / '.git').write_text('formatter snapshot boundary\n')
        manifest = json.loads(git(ROOT, 'show', ':' + MANIFEST))
        tests = {entry['test'] for entry in manifest.get('exact_bytes', {}).values()}
        selected = list(entries) if manifest['generated'] else [p for p in entries if p.endswith('.chibi') or Path(p).name == '.chibi-format' or p == MANIFEST or p in tests]
        for name in selected:
            target = snapshot / name
            target.parent.mkdir(parents=True, exist_ok=True)
            if entries[name] not in ('100644', '100755'):
                raise ValueError(f'{name}: staged formatter snapshot needs regular files')
            target.write_bytes(git(ROOT, 'show', ':' + name))
            target.chmod(0o755 if entries[name] == '100755' else 0o644)
        return check_tree(snapshot, binary, entries)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--check', action='store_true', help='read-only full working-tree check (default)')
    mode.add_argument('--write', action='store_true', help='format authored files, never generated or exact-byte fixtures')
    mode.add_argument('--staged', action='store_true', help='read-only full index check; never re-stage or overwrite files')
    args = parser.parse_args()
    try:
        entries = inventory(ROOT)
        binary = formatter()
        return staged(binary, entries) if args.staged else check_tree(ROOT, binary, entries, write=args.write)
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError, subprocess.CalledProcessError) as error:
        print(f'format: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
