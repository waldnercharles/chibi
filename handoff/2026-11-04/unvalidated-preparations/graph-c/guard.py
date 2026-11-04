"""Fixed graph-c runtime input fences. Source-only, unarmed until independent admission."""
from pathlib import Path
import hashlib, json, os, stat, sys, time, struct
H = Path('/workspace/shared/chibi-graph-c-runtime-20261104-revision01')
S = H / 'source'
ORIGINAL = Path('/workspace/shared/chibi-graph-c-20261104-revision01')
A = ORIGINAL / 'artifacts'
CLOCK = None
FILE_LIMIT = 64 * 1024 * 1024
OUTER_SHA256 = 'eb565285001cef43ed88cf73e5797cc83b726afcf6cf192cf5b4ba10f6078698'
CORE_SHA256 = 'aeb3759d27e523aeb0257e73790ffce8cca89a1e64feb02290a2442d903914b4'
GRAPH_SHA256 = '046fabf20b00f394cecf4307c7f1758ff17fad7694b31bc3da9dac889321120f'
COMPILER = '/workspace/shared/chibi-current-rebuild-20261104/stage4/chibi'
LLVM = '/workspace/scratch/be2d74211a85/chibi-tools-resume-20261103/llvm22/usr/lib/llvm-22/bin'
ARGV = [str(A/'graph-c'), COMPILER, LLVM]

def need(ok, message):
    if not ok:
        raise ValueError(message)

def sha(path):
    tick()
    value = hashlib.sha256(Path(path).read_bytes()).hexdigest()
    tick()
    return value

def read(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            need(key not in result, 'duplicate JSON key: ' + key)
            result[key] = value
        return result
    return json.loads(Path(path).read_text(), object_pairs_hook=unique)

def save(path, value):
    tick()
    with Path(path).open('x', encoding='utf-8') as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write('\n'); stream.flush(); os.fsync(stream.fileno())
    tick()


def set_clock(intent, reserve):
    global CLOCK
    need(intent['end_wall'] - intent['start_wall'] == 125 and
         intent['end_mono'] - intent['start_mono'] == 125, 'original clock pair differs')
    CLOCK = (intent['end_wall'] - reserve, intent['end_mono'] - reserve)
    tick()


def tick():
    if CLOCK is not None and (time.time() >= CLOCK[0] or time.monotonic() >= CLOCK[1]):
        raise TimeoutError('original graph-c cutoff reached')


def fact(path):
    path = Path(path); st = path.stat()
    return dict(path=str(path), realpath=str(path.resolve()), bytes=st.st_size,
                mode=stat.S_IMODE(st.st_mode), sha256=sha(path),
                link=os.readlink(path) if path.is_symlink() else None)

def fresh_tree(root):
    rows = []
    for path in [root] + sorted(root.rglob('*')):
        tick()
        st = path.lstat()
        need(stat.S_ISREG(st.st_mode) or stat.S_ISDIR(st.st_mode) or stat.S_ISLNK(st.st_mode), 'special source input')
        row = dict(path=str(path.relative_to(root)), mode=stat.S_IMODE(st.st_mode),
                   kind='link' if path.is_symlink() else 'directory' if path.is_dir() else 'file',
                   inode=st.st_ino, device=st.st_dev, bytes=st.st_size,
                   mtime_ns=st.st_mtime_ns, ctime_ns=st.st_ctime_ns)
        if path.is_symlink():
            row.update(target=os.readlink(path), resolved=fact(path) if path.is_file() else None)
        elif path.is_file():
            row['sha256'] = sha(path)
        rows.append(row)
    return rows

def admitted_tree(root):
    rows = []
    for path in sorted(root.rglob('*')):
        tick()
        st = path.lstat()
        row = dict(path=str(path.relative_to(root)), mode=stat.S_IMODE(st.st_mode),
                   kind='link' if path.is_symlink() else 'directory' if path.is_dir() else 'file')
        need(stat.S_ISREG(st.st_mode) or stat.S_ISDIR(st.st_mode) or stat.S_ISLNK(st.st_mode), 'special C input')
        if path.is_symlink():
            row.update(target=os.readlink(path), resolved=fact(path) if path.is_file() else None)
        elif path.is_file():
            row.update(bytes=st.st_size, sha256=sha(path), inode=st.st_ino, device=st.st_dev,
                       mtime_ns=st.st_mtime_ns, ctime_ns=st.st_ctime_ns)
        rows.append(row)
    return rows

def closure(plan):
    return {name: read(row['path']) for name, row in plan['closure_manifests'].items()}

def fence(plan, runtime_after=False):
    need(sha(H / 'plan.json') == plan['_digest'], 'plan changed')
    for name, digest in plan['pins'].items():
        need(sha(H / name) == digest, 'packet pin changed: ' + name)
    for row in plan['closure_manifests'].values():
        need(fact(row['path']) == row, 'read-only admitted manifest changed')
    need({p.name for p in H.iterdir()} <= set(plan['packet_names']), 'unexpected packet growth')
    prior = closure(plan)
    for row in prior['tools']:
        need(fact(row['path']) == row, 'admitted tool/runtime changed: ' + row['path'])
    roots = {row['root'] for row in prior['c_roots']}
    for row in prior['roots']:
        if row['path'] not in roots:
            continue
        path = Path(row['path']); st = path.stat()
        need(path.is_dir() and str(path.resolve()) == row['realpath'] and
             stat.S_IMODE(st.st_mode) == row['mode'] and st.st_dev == row['device'] and
             st.st_ino == row['inode'], 'admitted C root identity changed')
    for row in prior['c_roots']:
        need(admitted_tree(Path(row['root'])) == row['inventory'], 'admitted C tree changed: ' + row['root'])
    for path in prior['search']['absent']:
        need(not os.path.lexists(path), 'search absence changed: ' + path)
    for row in prior['search']['directories']:
        path = Path(row['path'])
        entries = [dict(name=p.name, mode=stat.S_IFMT(p.lstat().st_mode),
                        link=os.readlink(p) if p.is_symlink() else None) for p in sorted(path.iterdir())]
        need(str(path.resolve()) == row['realpath'] and entries == row['entries'], 'search directory changed: ' + str(path))
    root = Path('/usr/lib/python3.13'); actual = set()
    for directory, folders, files in os.walk(root, followlinks=False):
        for name in files:
            actual.add(str((Path(directory) / name).relative_to(root)))
        for name in list(folders):
            path = Path(directory) / name
            if path.is_symlink():
                actual.add(str(path.relative_to(root))); folders.remove(name)
    need(actual == {row['path'] for row in prior['python']}, 'Python inventory changed')
    for row in prior['python']:
        tick()
        path = root / row['path']
        data = os.readlink(path).encode() if path.is_symlink() else path.read_bytes()
        mode = '120000' if path.is_symlink() else '100755' if path.stat().st_mode & 0o111 else '100644'
        need(mode == row['mode'] and len(data) == row['bytes'] and hashlib.sha256(data).hexdigest() == row['sha256'], 'Python input changed')
    generated = source_guard(runtime_after)
    need(source_inventory(ORIGINAL/'source') == read(ORIGINAL/'source-baseline.json'), 'completed compile source changed')
    for item in plan['readonly_evidence']:
        need(fact(item['path']) == item, 'read-only proof input changed: ' + item['path'])
    need(fact(A/'graph-c') == plan['artifact'], 'separately admitted graph-c ELF changed')
    for row in prior['graph_tools']:
        wanted = {key:value for key,value in row.items() if key != 'stat'}
        need(fact(row['path']) == wanted, 'Graph tool bytes changed')
        st = Path(row['path']).stat()
        need(all(getattr(st, 'st_' + key) == value for key,value in row['stat'].items()), 'Graph tool stat changed')
    for path in prior['graph_loader']['absent']:
        need(not os.path.lexists(path), 'Graph loader absence changed')
    for path,names in prior['graph_loader']['search_directory_names'].items():
        need(sorted(p.name for p in Path(path).iterdir()) == names, 'Graph loader search changed')
    for row in read(H / 'supplemental-utilities.json')['utilities'].values():
        expected = row['fact']; wanted = {key:value for key,value in expected.items() if key != 'stat'}
        need(fact(expected['path']) == wanted, 'supplemental utility changed')
        st = Path(expected['path']).stat()
        need(all(getattr(st, 'st_' + key) == value for key,value in expected['stat'].items()), 'supplemental utility stat changed')
    mapped = {line.split()[-1] for line in Path('/proc/self/maps').read_text().splitlines()
              if len(line.split()) >= 6 and line.split()[-1].startswith('/')}
    known = {row['realpath'] for row in prior['tools'] + prior['graph_tools']} | {str(root / row['path']) for row in prior['python']}
    need(mapped <= known, 'actual Python/runtime mapping outside admitted closure')
    return generated

def source_inventory(root=S):
    rows = []
    for path in [root] + sorted(root.rglob('*')):
        tick()
        st = path.lstat(); kind = 'link' if path.is_symlink() else 'directory' if path.is_dir() else 'file'
        need(kind != 'file' or stat.S_ISREG(st.st_mode), 'special source path')
        row = dict(path=str(path.relative_to(root)), mode=stat.S_IMODE(st.st_mode), kind=kind, inode=st.st_ino, device=st.st_dev)
        if kind != 'directory': row.update(bytes=st.st_size, mtime_ns=st.st_mtime_ns, ctime_ns=st.st_ctime_ns)
        if kind == 'file': row['sha256'] = sha(path)
        if kind == 'link': row['target'] = os.readlink(path)
        rows.append(row)
    return rows

def source_guard(runtime_after=False):
    actual = source_inventory(); baseline = read(H / 'source-baseline.json')
    if not runtime_after:
        need(actual == baseline, 'source or scratch differs from cold baseline')
        return []
    cache = read(H / 'runner-cache.json'); relative = str(Path(cache['program']).relative_to(S))
    expected = {'.chibi/graph-process': ('directory', 0o755), '.chibi/build-runner': ('directory', 0o755),
                str(Path(relative).parent): ('directory', 0o700), relative: ('file', 0o755)}
    generated = [row for row in actual if row['path'] in expected]
    need({row['path'] for row in generated} == set(expected), 'genuine nested cache/empty process-parent missing')
    need([row for row in actual if row['path'] not in expected] == baseline, 'unexpected source mutation or scratch residue')
    for row in generated:
        need((row['kind'], row['mode']) == expected[row['path']], 'generated path type/mode differs')
    elf_guard(Path(cache['program']))
    return generated

def elf_guard(program):
    data = program.read_bytes()
    need(0 < len(data) <= FILE_LIMIT and data[:6] == b'\x7fELF\x02\x01' and struct.unpack_from('<HH', data, 16) == (2, 62), 'not bounded Linux x86-64 ELF EXEC')
    offset = struct.unpack_from('<Q', data, 32)[0]; size, count = struct.unpack_from('<HH', data, 54)
    need(size == 56 and count < 128 and offset + size * count <= len(data), 'invalid ELF headers')
    segments = [struct.unpack_from('<IIQQQQQQ', data, offset + i * size) for i in range(count)]
    need([data[p[2]:p[2]+p[5]] for p in segments if p[0] == 3] == [b'/lib64/ld-linux-x86-64.so.2\0'], 'unexpected interpreter')
    dynamic = [p for p in segments if p[0] == 2]; need(len(dynamic) == 1, 'dynamic segment missing')
    entries = []
    for at in range(dynamic[0][2], dynamic[0][2]+dynamic[0][5], 16):
        tag, value = struct.unpack_from('<qQ', data, at)
        if tag == 0: break
        entries.append((tag, value))
    need(not any(tag in (15, 29) for tag,value in entries), 'generated search path forbidden')
    strings = [value for tag,value in entries if tag == 5]; need(len(strings) == 1, 'string table missing')
    loads = [p for p in segments if p[0] == 1 and p[3] <= strings[0] < p[3]+p[5]]; need(len(loads) == 1, 'string table unmapped')
    base = loads[0][2]+strings[0]-loads[0][3]
    needed = [data[base+value:data.index(b'\0',base+value)].decode() for tag,value in entries if tag == 1]
    need(needed == ['libc.so.6'], 'unadmitted generated runtime')
    return dict(interpreter='/lib64/ld-linux-x86-64.so.2', needed=needed)

def artifact_guard(expected=None):
    need(A.is_dir() and not A.is_symlink() and sorted(p.name for p in A.iterdir()) == ['graph-c'], 'completed compile artifact inventory changed')
    need((A / 'graph-c').is_file() and not (A / 'graph-c').is_symlink() and stat.S_IMODE((A / 'graph-c').stat().st_mode) == 0o755, 'top-level artifact type/mode differs')
    rows = fresh_tree(A)
    if expected is not None: need(rows == expected, 'independently inspected artifact changed')
    elf_guard(A / 'graph-c')
    return rows

def focused_c_guard(plan):
    proof = plan['focused_c_proof']
    need(isinstance(proof, dict) and set(proof) == {'files', 'accepted_source_files'}, 'focused C proof has not been independently bound')
    need(proof['accepted_source_files'] == read(H / 'source-provenance.json')['accepted_delta'], 'focused C source differs')
    for row in proof['files'].values(): need(fact(row['path']) == row, 'focused C evidence changed')
    for name in ['compile_terminal', 'run_terminal']:
        need(read(proof['files'][name]['path'])['status'] == 'passed', 'focused C phase did not pass')
    for name in ['compile_owner', 'run_owner']:
        receipt = read(proof['files'][name]['path'])
        need(receipt['status'] == 'pass' and receipt['cleanup_echild'] is True and receipt['deadline_respected'] is True, 'focused C ownership gate did not pass')
    need('independent_admission' in proof['files'], 'focused C independent final admission missing')
    report = read(proof['files']['independent_admission']['path'])
    need(report['status'] == 'accepted' and report['source_files'] == proof['accepted_source_files'], 'focused C admission scope differs')
    root = Path('/workspace/shared/chibi-array-cookie-fix-20261104')
    need(report['packet_path'] == str(root), 'focused C packet differs')
    for name,digest in report['evidence_sha256'].items():
        need(not Path(name).is_absolute() and '..' not in Path(name).parts and sha(root/name) == digest, 'focused C admitted evidence changed')
    need(fresh_tree(root/'artifacts') == report['artifacts'], 'focused C admitted artifact bytes/stat changed')

def identity(pid):
    fields = Path('/proc/'+str(pid)+'/stat').read_text().rsplit(')', 1)[1].split()
    return dict(pid=pid, ppid=int(fields[1]), pgid=int(fields[2]), sid=int(fields[3]), start_ticks=int(fields[19]))

def environment():
    work = H/'work'
    return dict(PATH='/usr/bin:/bin', LANG='C', LC_ALL='C', HOME=str(work/'home'),
                TMPDIR=str(work/'tmp'), XDG_CACHE_HOME=str(work/'cache'),
                CHIBI_COMPILER_CACHE=str(work/'compiler-cache'), CHIBI_ASSET_COMPILER_CACHE=str(work/'asset-cache'),
                CHIBI_CORE=str(S/'packages/core'), CC='/usr/bin/gcc-14', CXX='/usr/bin/false', CHIBI_TEST_WORKERS='2',
                LD_LIBRARY_PATH='/workspace/scratch/be2d74211a85/chibi-tools-resume-20261103/llvm22/usr/lib/x86_64-linux-gnu:'
                                '/workspace/scratch/be2d74211a85/chibi-tools-resume-20261103/llvm22/usr/lib/llvm-22/lib')

def admission(plan, intent):
    if (H/'runtime-intent.json').exists():
        current = sha(H/'runtime-intent.json')
        need(current == plan.setdefault('_intent_sha256',current)
             and read(H/'runtime-intent.json') == intent, 'exclusive runtime intent changed')
    need(sys.flags.isolated and sys.flags.no_site and sys.dont_write_bytecode
         and Path('/proc/self/environ').read_bytes() == b'', 'isolated empty-environment Python required')
    need(plan['runtime_enabled'] is True, 'UNARMED: runtime source/control/artifact admission absent')
    need(plan['total_seconds'] == 125 and plan['outer_cleanup_seconds'] == 5
         and plan['cutoffs'] == [110,115,120,125], 'fixed original runtime budget differs')
    need(plan['argv'] == ARGV and plan['cwd'] == str(S) and plan['environment'] == environment(), 'fixed graph-c launch differs')
    need(plan['cpu_affinity'] == [0,1] and sorted(os.sched_getaffinity(0)) == [0,1], 'fixed affinity differs')
    need(plan['artifact']['sha256'] == GRAPH_SHA256 and sha(A/'graph-c') == GRAPH_SHA256, 'admitted graph-c artifact differs')
    need(sha(H/'outer_owner.py') == OUTER_SHA256 and sha(H/'graph_c_attribution.py') == CORE_SHA256, 'reviewed owners changed')
    need(sha(H/'plan.json') == intent['plan_sha256'], 'intent plan changed')
    acceptance_path = Path(intent['acceptance_path'])
    need(acceptance_path.is_absolute() and not acceptance_path.is_symlink()
         and not acceptance_path.resolve().is_relative_to(H), 'independent acceptance path differs')
    need(sha(acceptance_path) == intent['acceptance_sha256'], 'runtime acceptance changed')
    accepted = read(acceptance_path)
    need(accepted['status'] == 'accepted' and accepted['phase'] == 'graph-c-runtime'
         and accepted['plan_sha256'] == intent['plan_sha256'], 'exact runtime acceptance absent')
    need(accepted['not_before_epoch'] <= intent['start_wall'] and intent['end_wall'] <= accepted['not_after_epoch'], 'window excludes original interval')
    for key, name in [('controller_sha256','run.py'),('inner_owner_sha256','inner_owner.py'),
                      ('outer_owner_sha256','outer_owner.py'),('core_sha256','graph_c_attribution.py')]:
        need(accepted[key] == sha(H/name), 'runtime accepted source differs: '+name)
    for key, phase, status in [('controls_admission','graph-c-owner-controls','passed'),
                               ('runtime_artifact_admission','graph-c-recursive-runtime','accepted')]:
        item = plan[key]
        need(isinstance(item,dict) and fact(item['path']) == item and accepted[key] == item, 'missing exact '+key)
        proof = read(item['path'])
        need(proof['phase'] == phase and proof['status'] == status, key+' scope/status differs')
        need(isinstance(proof['files'],list) and proof['files'], key+' lacks exact evidence files')
        for evidence in proof['files']:
            need(fact(evidence['path']) == evidence, key+' evidence changed: '+evidence['path'])
        for name in ('inner_owner.py','run.py','guard.py','graph_c_attribution.py'):
            need(proof['source_sha256'][name] == sha(H/name), key+' source differs: '+name)
        if key == 'controls_admission':
            need(proof['case_manifest_sha256'] == sha(H/'controls/manifest.json'), 'controls manifest differs')
    runtime = read(plan['runtime_artifact_admission']['path'])
    need(runtime['artifact'] == plan['artifact'] and runtime['cwd'] == str(S)
         and runtime['environment'] == environment() and runtime['argv'] == ARGV
         and runtime['source_baseline_sha256'] == sha(H/'source-baseline.json')
         and runtime['runner_cache_sha256'] == sha(H/'runner-cache.json')
         and runtime['source_mapping_sha256'] == sha(H/'source-mapping.json')
         and runtime['dependency_manifest_sha256'] == sha(H/'runtime-dependencies.json')
         and runtime['output_policy_sha256'] == sha(H/'output-policy.json'), 'recursive runtime binding incomplete')
    tick()

def output_guard(stdout, stderr):
    policy = read(H/'output-policy.json')
    need(policy['source_derived'] is True, 'output policy lacks source provenance')
    pairs = [(bytes.fromhex(row['stdout_hex']),bytes.fromhex(row['stderr_hex'])) for row in policy['variants']]
    need((stdout,stderr) in pairs, 'unrecognized graph-c stdout/stderr; preserve for independent review')

def work_guard():
    for name in ('home','tmp','cache','compiler-cache','asset-cache'):
        path = H/'work'/name
        need(path.is_dir() and not path.is_symlink() and not list(path.iterdir()), 'private work/cache residue: '+name)
