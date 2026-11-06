#!/usr/bin/env python3
"""Compile a sample folder (package dirs of .chibi files) with the recovered compiler and run one entry on the VM.
usage: vm.py SAMPLE_DIR PACKAGE FUNCTION [backend]"""
import json, os, subprocess, sys, tempfile
R = '/Users/cwaldner/Personal/chibi/.worktrees/builtin-math-main/chibi'
F = R + '/.chibi/math-main/fnptr'
host = F + '/runtime/chibi-compiler-host-macos-arm64'
artifact = F + '/stage1/compiler.chibip'
sample, package, function = sys.argv[1:4]
backend = sys.argv[4] if len(sys.argv) > 4 else 'sljit'

def entry(symbol):
    h = 14695981039346656037
    for b in ('package-symbol:' + symbol).encode():
        h = ((h ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return '0x%016x' % (h or 1)

paths, texts = [], []
for root, _, files in os.walk(sample):
    for name in sorted(files):
        if name.endswith('.chibi'):
            full = os.path.join(root, name)
            paths.append(os.path.relpath(full, sample))
            texts.append(open(full).read())
work = tempfile.mkdtemp(dir=F)
target = json.loads(subprocess.check_output([host, '--target-json']))
request = ['/project', ['core'], ['/sdk/core'], paths, texts, False] + target
open(work + '/compile.json', 'w').write(json.dumps(request))
r = subprocess.run([host, '--artifact', artifact, '--entry', '0x140a9546d9e8c296', '--input', work + '/compile.json',
                    '--backend', 'sljit', '--budget', '10000000000', '--timeout-ms', '600000',
                    '--output-bytes', work + '/program.chibip'], capture_output=True, text=True)
if r.returncode:
    d = subprocess.run([host, '--artifact', artifact, '--entry', entry('project:package_compiler:1:Compile:callable'),
                        '--input', work + '/compile.json', '--backend', 'sljit', '--budget', '10000000000',
                        '--timeout-ms', '600000'], capture_output=True, text=True)
    print('compile failed:', d.stdout.strip()[:1500], d.stderr.strip()[-300:]); sys.exit(1)
open(work + '/in.json', 'w').write('[]')
r = subprocess.run([host, '--artifact', work + '/program.chibip', '--entry', entry('project:%s:1:%s:callable' % (package, function)),
                    '--input', work + '/in.json', '--backend', backend, '--budget', '2000000000', '--timeout-ms', '60000'],
                   capture_output=True, text=True)
print('exit', r.returncode, 'stdout', r.stdout.strip()[:300], 'stderr', r.stderr.strip()[-300:])
