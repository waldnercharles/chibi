#!/usr/bin/env python3
"""Temporary command-level test quarantine; remove only after timing review."""
import argparse
import ctypes
import json
import math
import os
from pathlib import Path
import signal
import shlex
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[1]
LIMIT = 60
DEADLINE = 'CHIBI_TEST_DEADLINE'
PROCESS_GROUP = 'CHIBI_TEST_PROCESS_GROUP'
# Exact invocations, not selector prefixes. Historical times are not guarantees.
NATIVE = {
    ('--tilemap-project',): 'Reviewed first CPU measurement: fixed sibling Celeste project, pinned local git baseline56ca9d48<=64KiB, Room<=16KiB, Level<=4KiB,1472cells230oldtiles/fiveactors, three owned controller children<=10s each including two headless simulation frames<=15entities, no GPU/network; shared60s',
    ('--tilemap',): 'Independently reviewed seven fixed CPU grid/cache/point/SAT/bounds/native-binding/prototype cases, at most four parsed cells and three created ECS entities, one <=4KiB KDL and 64x16 PNG preparation, rejected overflow dimensions, no engine frames/GPU/process/network; shared60s',
    ('--web-storage',): 'First supervised measurement; independently reviewed thirteen C-owned browser storage and platform cases, tiny in-memory FS and ZIP fixtures executed under Node, no network or GPU, shared60s',
    ('--webgpu-staging',): 'Reviewed C-owned isolated headless Chrome: one64x64quad for120 production-main-loop frames, peak64MiB staging/reuse assertions, postdestroy orphan drain<=1s within startup+execution55s/shared60s; docs/testing.md',
    ('--studio-graph-visibility',): 'Reviewed actual installed Studio graph visibility, scene draft history and tilemap viewport bounds/layer picking; two temporary entities and four CPU picks, disposable three-file fixture and isolated prefs, ten hidden 1920x1080 frames at fixed 1/60, shared60s',
    ('--studio-source-edit',): 'Reviewed actual installed Studio edit/undo/redo, disposable3file212byteproject+isolatedprefs, fourhidden1920x1080frames, shared60s',
    ('--compile-installed-space-cadet',): 'Pinned installed SpaceCadet fixed36files/144741bytes compiler-only, exact8resource scope, shared60seconds',
    ('--player-installed-space-cadet-vm',): 'Pinned installed player VM, four hidden1280x720frames, exact8resources, shared60seconds',
    ('--player-installed-space-cadet-sljit',): 'Pinned installed player SLJIT, four hidden1280x720frames, exact8resources, shared60seconds',
    ('--interior-border',): 'Reviewed hidden 64x64 app: Cute interior border on a circle and a diamond, opaque and translucent, instanced and tiled draw paths with readback; GPU; docs/testing.md',
    ('--viewport-resize',): 'Reviewed hidden 64x64 app: an engine viewport takes its first requested size at once and later sizes after two draws; GPU; docs/testing.md',
    ('--sdf-visibility',): 'Reviewed hidden 96x80 app: mesh and SDF occlusion with hidden-surface rejection on and off, depth snapshot copies, readback; GPU; docs/testing.md',
    ('--scene3d',): 'Reviewed hidden 64x64 app: 3D renderer cases for lights, shadows, exposure, sky and ramps with readback; GPU; docs/rendering-3d.md',
    ('--scene3d-gles',): 'Reviewed hidden 64x64 app on GLES under xvfb-run: the same 3D renderer cases; GPU, Linux; docs/rendering-3d.md',
    ('--material-ramps',): 'Reviewed hidden 64x64 app: four color ramp table cases through 2D and material raster shaders; GPU; docs/testing.md',
    ('--shader-parameters-prepared',): 'Independently reviewed one tiny material/two-parameter preparation, typed codec and generation rejection; CPU only, no GPU; docs/testing.md',
    ('--source-systems',): 'First supervised measurement; independently reviewed eleven pico source system cases on VM/SLJIT, fixed pumps and at most 259 entities, CPU only; docs/testing.md',
    ('--new-asset',): 'One new-asset scaffolding case: one temporary content folder, three written assets and six refusals, CPU only; docs/testing.md',
    ('--asset-refactor',): 'One asset rename case: four in-memory prototypes, planned rewrites of a nested child and of a child that an including prototype adds, no files, CPU only; docs/testing.md',
    ('--source-edit',): 'Independently reviewed nine fixed source-edit cases, tiny inputs and checked transactions including extraction/inline/signature/binding/capture and layout, no files/world/runtime/GPU; docs/testing.md',
    ('--ecs-query-benchmark',): 'Reviewed three fixed ECS query workloads, 4096 entities x 2000 passes each, CPU only; run through benchmark_trials',
    ('--system-schedule',): 'First supervised measurement; independently reviewed fixed dependency/order cases, 62 small engine frames and serial/parallel 128 entities x 20 frames, CPU only; docs/testing.md',
    ('--source-presentation',): 'Independently reviewed five small source/palette cases, graph ownership/layout, invalid drafts, callable-local scope and readable formulas with retained calls/effects, plus two component presentation cases on two in-memory documents, CPU only; docs/testing.md',
}
TOOLS = {
    'tilemap-project': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--tilemap-project'],
    'tilemap': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--tilemap'],
    'studio-graph-visibility': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-graph-visibility'],
    'studio-source-edit': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-source-edit'],
    'compile-installed-space-cadet': [sys.executable, str(ROOT / 'tools/agent.py'), 'test', '--', '--compile-installed-space-cadet'],
    'player-installed-space-cadet-vm': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--player-installed-space-cadet-vm'],
    'player-installed-space-cadet-sljit': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--player-installed-space-cadet-sljit'],
    # Existing admitted source-edit and shader persistence cases, now installed SDK consumers.
    'source-edit': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--source-edit'],
    # One new-asset scaffolding case in a temporary content folder.
    'new-asset': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--new-asset'],
    # One asset rename case over in-memory prototypes.
    'asset-refactor': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--asset-refactor'],
    # Eighteen validate_project cases, each a scaffolded temporary project and a few built chibi_ctl runs.
    'validate-project': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--validate-project',
                         str(ROOT / os.environ.get('CHIBI_CTL', 'build/bin/chibi_ctl'))],
    'shader-parameters-prepared': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--shader-parameters-prepared'],
    # Reviewed fixed ECS query workloads; compare builds through benchmark_trials.
    'ecs-query-benchmark': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--ecs-query-benchmark'],
    # Reviewed four fixed scheduling cases, including verified source cohorts and mixed phase membership.
    'system-schedule': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--system-schedule'],
    # Same admitted eleven fixed source-system cases through installed public consumer, VM and SLJIT.
    'source-systems': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--source-systems'],
    # Reviewed six tiny immutable module generations, two backend worlds, aggregate/string ownership.
    'persistent-store': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--persistent-store'],
    # Reviewed three source presentation/default/layout and actual native palette composition cases, CPU only.
    'source-presentation': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--source-presentation'],
    # Reviewed fixed SDK/player install, managed migration and packaging fixtures; one shared deadline.
    'package': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--package'],
    # Reviewed nominal alias ownership and nested error-source case, both backends, bounded 16 preparations.
    'whole-alias': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--whole-alias'],
    # Four reviewed public integer edge/flags/live-register/budget cases; 28k fixed calls, 46 tiny functions.
    'whole-integer': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--whole-integer'],
    # One reviewed public scalar conversion case; 18 supported pairs, 12 values, both backends.
    'whole-conversion': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--whole-conversion'],
    # One reviewed public scalar Select case; six widths, two conditions, both backends.
    'whole-select': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--whole-select'],
    # Two reviewed bounded primitive math/ABI cases; 4594 invocations, VM and SLJIT.
    'whole-math': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--whole-math'],
    # Two reviewed owned snapshot cases; four value kinds, three owners/frames, both backends.
    'owned-tick': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--owned-tick'],
    # Two reviewed registry service rebinding and incremental attachment cases; four owners.
    'parallel-tick': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--parallel-tick'],
    # Three reviewed source reaction/rhythm/particle cases; fixed waits/emission and both backends.
    'source-native-features': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--source-native-features'],
    # Five reviewed source math/query cases; numeric samples, nested membership,
    # ten fractional rectangle/point poses with live declared-field/stale-handle edits,
    # 3D collider queries and one two-cell tilemap with two bounded dispatches per VM/SLJIT backend.
    'source-native-numeric-queries': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--source-native-numeric-queries'],
    # One reviewed source material case; four payload types, two backends, CPU shader stage only.
    'source-native-material': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--source-native-material'],
    # Reviewed CPU-only renderer cases: typed transaction/lifetime, sprite image sets, sky face layout
    # and halves, sky irradiance, the split-sum table, sky publication, and pure desktop key/stick
    # mapping; no window, GPU, fonts or files.
    'render-resources': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--render-resources'],
    # Two reviewed hidden-app 64x64 cases: Cute's interior border on a circle and a diamond,
    # opaque and translucent, through the instanced and tiled draw paths. The agent runs the
    # render-enabled test_tools of its selected build directory.
    'interior-border': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--interior-border'],
    # Three reviewed hidden-app cases: an engine viewport replaces its placeholder canvas on the
    # first requested size and waits two draws for later sizes.
    'viewport-resize': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--viewport-resize'],
    # Fourteen reviewed hidden-app scenes of meshes and SDFs hiding each other, compared with
    # hidden-surface rejection on and off, plus Cute's whole-canvas depth copy on its own.
    'sdf-visibility': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--sdf-visibility'],
    # Hidden-app 64x64 3D renders: rhythm, ambient, lights, shadows, exposure, the Environment
    # sky and its light, and color ramps.
    'scene3d': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--scene3d'],
    # The same renders on GLES, the WebGL 2 fallback's backend. GLES needs a display, so the
    # agent runs it under xvfb-run; Linux only.
    'scene3d-gles': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--scene3d-gles'],
    # Four reviewed hidden-app 64x64 cases: a ramp past 4 KB of the ramp table through a 2D draw
    # shader and the material raster, every program kind compiling the table, and the table
    # counted among a raster batch's samplers.
    'material-ramps': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--material-ramps'],
    # Two independently reviewed bounded reflected scene codec/lifetime cases, no file/process/GPU.
    'scene-prepared': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--scene-prepared'],
    # Five independently reviewed model/shader/mesh preparation, persistence and lifetime cases; no files, world or GPU.
    'model-runtime': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--model-runtime'],
    # One independently reviewed finite constant-folding case through the public authoring interface.
    'graph-authoring': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--graph-authoring'],
    # One independently reviewed full native catalog consumer, generated ABI call and typed observer metadata.
    'native-catalog': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--native-catalog'],
    # Two independently reviewed public graph ownership/topology cases, no process/world/GPU.
    'graph-model': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--graph-model'],
    # One independently reviewed bounded native comparison/conversion ABI and ownership case.
    'native-values': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--native-values'],
    # Two independently reviewed finite formatting and owned-value cases, no world/process/GPU.
    'text-format': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--text-format'],
    # One reviewed in-tree Box3D package case: a falling box comes to rest and three raycasts, CPU only.
    'box3d': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--box3d'],
    # Four reviewed in-tree Radiance .hdr cases: flat and run-length decoding, malformed files,
    # preparation from mounted content and import without replacement; one temporary folder, no GPU.
    'sky-files': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--sky-files'],
    # Nine reviewed native cases: retained spawn generations, a three-step physics world, prepared player handoff/rejection, named scene loading and conflicting-binding refusal, sound requests, declared components, per-step action state and recorded taps, and held actions across a pause.
    'native-runtime': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--native'],
    # 2D physics through ECS and Contact Begin events: motion, contacts, masks, queries and parallel collection.
    'physics': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--physics'],
    # 3D physics through ECS and Contact 3D events: shapes, gravity, velocities, teleports, followers, sensors, interpolation and parallel steps.
    'physics3d': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--physics3d'],
    # Transform hierarchy propagation: 2D and 3D composition, cascade order, moves and reparenting.
    'transforms': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--transforms'],
    'ui-authoring': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--ui-authoring'],
    'prototype-document': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--prototype-document'],
    # Five reviewed bounded field cases, including atomic typed argument codec handoff.
    'field-runtime': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--field-runtime'],
    # Reviewed fixed in-memory source identity/refactor and transactional publication cases.
    'language-navigation': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--navigation'],
    'language-publication': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--publication'],
    # Two reviewed scene lifecycle cases, isolated public SDK consumer, no GPU.
    'runtime-scene': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--scene'],
    # Reviewed whitespace-only CST formatter, finite cases and bounded production source manifest.
    'language-format': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                        '--format', '--corpus',
                        str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'corpus.txt')],
    # Reviewed finite producer process and runtime-only consumer with one shared deadline.
    'language-prepared': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                          '--prepared',
                          str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language')],
    # Reviewed public module consumer; finite ECS/behavior lifecycle on VM and SLJIT.
    'language-headless': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                         '--headless'],
    # Reviewed source-to-runtime event/system/binding cases with finite supplied-time steps.
    'language-program': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                        '--program'],
    # Reviewed finite public builder/VM/native contracts and verifier rejection cases.
    'language-execution': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                           '--execution'],
    # Reviewed finite semantic contracts with inline sources and an immutable catalog.
    'language-semantic': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                          '--semantic'],
    # Reviewed finite C syntax cases and a corpus capped at 256 files/8 MiB.
    'language': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                 '--syntax', '--corpus', str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'corpus.txt')],
    'web-storage': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--web-storage'],
    'webgpu-staging': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--webgpu-staging'],
    'native-tools': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools')],

}
# The admitted CPU selectors of each built driver that fit one shared deadline.
GROUPS = {
    'native': ['native-runtime', 'native-catalog', 'native-values', 'physics', 'physics3d', 'transforms',
               'box3d', 'sky-files', 'tilemap',
               'runtime-scene', 'scene-prepared', 'render-resources', 'persistent-store',
               'whole-alias', 'whole-integer', 'whole-conversion', 'whole-select', 'whole-math',
               'owned-tick', 'parallel-tick', 'source-native-features',
               'source-native-numeric-queries', 'source-native-material', 'source-presentation',
               'system-schedule', 'source-systems', 'source-edit', 'new-asset', 'asset-refactor',
               'shader-parameters-prepared',
               'language-prepared', 'ecs-query-benchmark'],
    'language': ['language', 'language-semantic', 'language-execution', 'language-program',
                 'language-headless', 'language-format', 'language-navigation', 'language-publication',
                 'field-runtime', 'prototype-document', 'ui-authoring', 'text-format', 'graph-model',
                 'graph-authoring', 'model-runtime'],
}
NOTICE = ('TEMPORARY TEST QUARANTINE: full native/render and '
          'unadmitted checks DISABLED, NOT PASSED. See docs/testing.md#temporary-60-second-policy.')


def admitted(arguments):
    return tuple(arguments) in NATIVE


def selector_arguments(name):
    if name not in ('TEST_ARGS', 'AGENT_SELECTOR'):
        raise ValueError('unsupported selector environment variable')
    # Tokenize quoted positional arguments without executing shell syntax.
    return shlex.split(os.environ.get(name, ''))


def absolute_deadline(seconds, env):
    if not math.isfinite(seconds) or not 0 < seconds <= LIMIT:
        raise ValueError('test deadline must be at most 60 seconds')
    deadline = time.monotonic() + seconds
    if DEADLINE in env:
        inherited = float(env[DEADLINE])
        if not math.isfinite(inherited):
            raise ValueError('invalid inherited test deadline')
        deadline = min(deadline, inherited)
    return deadline


def process_children():
    # Shared portable PID/parent snapshot. Exclude the probe itself: it has
    # already been reaped when we use the snapshot and is not owned test work.
    probe = subprocess.Popen(['ps', '-A', '-o', 'pid=,ppid='],
                             stdout=subprocess.PIPE, text=True)
    try:
        rows, _ = probe.communicate(timeout=1)
    finally:
        if probe.poll() is None:
            probe.kill()
        probe.wait()
    if probe.returncode:
        raise RuntimeError('test cleanup process inspection failed')
    children = {}
    own_process_seen = False
    for row in rows.splitlines():
        pid, parent = map(int, row.split())
        own_process_seen |= pid == os.getpid()
        if pid != probe.pid:
            children.setdefault(parent, []).append(pid)
    if not own_process_seen:
        raise RuntimeError('test cleanup cannot inspect its own process')
    return children


def proc_children():
    path = Path(f'/proc/self/task/{os.getpid()}/children')
    return [int(pid) for pid in path.read_text().split()]


def cleanup_reader(subreaper):
    # Verify the selected discovery capability before launching test work.
    if subreaper:
        try:
            proc_children()
            return proc_children
        except (OSError, ValueError):
            pass
    try:
        process_children()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        raise RuntimeError('test cleanup unavailable: no usable proc children reader or ps; no test started') from error
    return lambda: process_children().get(os.getpid(), [])


def cleanup_children(read_children):
    # This dedicated subreaper owns all its children, including double-forked or
    # setsid descendants adopted when their parent/nested supervisor is killed.
    # Kill parents, reap, then repeat for newly adopted children until none remain.
    while True:
        children = read_children()
        for pid in children:
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        try:
            pid, _ = os.waitpid(-1, os.WNOHANG)
        except ChildProcessError:
            return
        if not pid:
            time.sleep(.001)


def cleanup_subtree(child):
    # Portable nested cleanup cannot kill the shared ancestor process group.
    # Freeze the live child tree before killing it so parent death cannot hide
    # descendants by reparenting them while we discover the owned subtree.
    if child.poll() is not None:
        return
    owned, pending = [child.pid], [child.pid]
    try:
        while pending:
            for pid in pending:
                try:
                    os.kill(pid, signal.SIGSTOP)
                except ProcessLookupError:
                    pass
            children = process_children()
            pending = []
            frontier = list(owned)
            seen = set(owned)
            for parent in frontier:
                for pid in children.get(parent, []):
                    if pid not in seen:
                        seen.add(pid)
                        frontier.append(pid)
                        pending.append(pid)
            owned.extend(pending)
    finally:
        for pid in reversed(owned):
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        child.wait()


def wait_until(child, deadline):
    # Popen.wait adds a KeyboardInterrupt grace period; do not grant test work
    # extra runtime before cleanup on an interrupt.
    while child.poll() is None:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise subprocess.TimeoutExpired(child.args, 0)
        time.sleep(min(.005, remaining))
    return child.returncode


def enable_subreaper():
    # Linux additionally owns detached/orphaned descendants. Other supported
    # POSIX hosts retain the portable process-group deadline path.
    if not sys.platform.startswith('linux'):
        return False
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(36, 1, 0, 0, 0):  # PR_SET_CHILD_SUBREAPER
        raise OSError(ctypes.get_errno(), 'cannot own test descendants')
    return True


def inherited_group(environment):
    return environment.get(PROCESS_GROUP) == str(os.getpgrp())


def supervise(commands):
    subreaper = enable_subreaper()
    read_children = cleanup_reader(subreaper)
    os.environ[PROCESS_GROUP] = str(os.getpgrp())
    child = None
    deadline = absolute_deadline(LIMIT, os.environ)
    try:
        for command in commands:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return 124
            child = subprocess.Popen(command, cwd=ROOT)
            try:
                result = wait_until(child, deadline)
            except subprocess.TimeoutExpired:
                print('FAIL: test command exceeded its aggregate deadline', file=sys.stderr)
                return 124
            if time.monotonic() >= deadline:
                return 124
            if result:
                return result if result > 0 else 128 - result
        return 0
    finally:
        # The caller reaches the same deadline and sends SIGTERM as we start
        # cleanup. signal.signal runs a pending handler before it installs
        # the new one, so retry until discovery and polling are protected.
        while True:
            try:
                signal.signal(signal.SIGTERM, signal.SIG_IGN)
                signal.signal(signal.SIGINT, signal.SIG_IGN)
                break
            except (KeyboardInterrupt, TerminationRequested):
                pass
        if subreaper:
            cleanup_children(read_children)
        elif child is not None:
            cleanup_subtree(child)


def run_commands(commands, seconds=LIMIT, env=None, output=None):
    """One inherited absolute deadline and one isolated descendant owner."""
    environment = dict(os.environ if env is None else env)
    # A caller-supplied environment may not erase an enclosing command's limit.
    if DEADLINE in os.environ:
        environment[DEADLINE] = str(min(float(environment.get(DEADLINE, 'inf')),
                                      float(os.environ[DEADLINE])))
    deadline = absolute_deadline(seconds, environment)
    environment[DEADLINE] = str(deadline)
    started = time.monotonic()
    if deadline <= started:
        return 124
    owns_group = not inherited_group(environment)
    child = subprocess.Popen([sys.executable, str(Path(__file__).resolve()),
                              '_supervise', json.dumps(commands)], cwd=ROOT,
                             env=environment, start_new_session=owns_group,
                             stdout=output, stderr=output)
    try:
        try:
            result = wait_until(child, deadline)
        except subprocess.TimeoutExpired:
            return 124
        if time.monotonic() >= deadline:
            return 124
        return result if result >= 0 else 128 - result
    finally:
        if child.poll() is None:
            # Let the supervisor unwind first; Linux also reaps descendants
            # which escaped the portable process group.
            child.terminate()
        child.wait()
        if owns_group:
            # Also used on macOS: nested managed wrappers stay in this group.
            # Arbitrary descendants that call setsid need Linux subreaping.
            try:
                os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        print(f'Test command wall time: {time.monotonic() - started:.3f}s; ceiling {seconds}s; '
              'includes in-test fixture setup; external configuration/build excluded.', file=output or sys.stdout, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['inventory', 'disabled', 'unit', 'short', 'tool', 'group', 'admit', 'native', 'admit-env', 'native-env', 'agent-env', '_supervise'])
    parser.add_argument('arguments', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    arguments = args.arguments
    if arguments[:1] == ['--']:
        arguments = arguments[1:]
    if args.action == '_supervise':
        return supervise(json.loads(arguments[0]))
    print(NOTICE, flush=True)
    if args.action == 'inventory':
        for command, evidence in NATIVE.items():
            print('ADMITTED (60s ceiling):', ' '.join(command), '-', evidence)
        for name in TOOLS:
            print('ADMITTED (60s ceiling): python3 tools/test_policy.py tool', name)
        print('ADMITTED: make test / test-short (C execution-policy units, one deadline)')
        print('ADMITTED: make test-tools (all C tooling and process integrations, one deadline)')
        for name in GROUPS:
            print(f'ADMITTED (60s ceiling): python3 tools/test_policy.py group {name}')
        print('Retired script suites are absent. Other native commands remain quarantined; see docs/test-quarantine.md.')
        return 0
    if args.action == 'disabled':
        print('DISABLED:', ' '.join(arguments), file=sys.stderr)
        return 2
    if args.action in ('unit', 'short'):
        if len(arguments) != 1:
            parser.error('unit/short requires the native C tooling test binary')
        command = [str(Path(arguments[0]).resolve())]
        if args.action == 'unit':
            command.append('--unit')
        return run_commands([command])
    if args.action == 'group':
        if len(arguments) != 1 or arguments[0] not in GROUPS:
            parser.error('unknown test group')
        return run_commands([TOOLS[name] for name in GROUPS[arguments[0]]])
    if args.action == 'tool':
        if len(arguments) != 1 or arguments[0] not in TOOLS:
            parser.error('unadmitted tooling command')
        if arguments[0] in ('player-installed-space-cadet-vm', 'player-installed-space-cadet-sljit', 'studio-source-edit',
                            'studio-graph-visibility', 'interior-border', 'viewport-resize', 'sdf-visibility',
                            'scene3d', 'scene3d-gles',
                            'material-ramps'):
            return run_commands([[sys.executable, str(ROOT / 'tools/agent.py'),
                                  'gpu-test', '--', '--' + arguments[0]]])
        return run_commands([TOOLS[arguments[0]]])
    if args.action in ('admit-env', 'native-env', 'agent-env'):
        expected = 1 if args.action == 'admit-env' else 2
        if len(arguments) != expected:
            parser.error('invalid environment-selector invocation')
        name = 'AGENT_SELECTOR' if args.action == 'agent-env' else arguments[0]
        selectors = selector_arguments(name)
        if not admitted(selectors):
            print('DISABLED: unadmitted native invocation: ' + ' '.join(selectors), file=sys.stderr)
            return 2
        if args.action == 'admit-env':
            return 0
        if args.action == 'agent-env':
            if arguments[1] not in ('test', 'gpu-test'):
                parser.error('invalid agent test mode')
            return run_commands([[sys.executable, str(ROOT / 'tools/agent.py'),
                                  '--build-dir', arguments[0], arguments[1], '--', *selectors]])
        return run_commands([[arguments[1], *selectors]])
    selectors = arguments[1:] if args.action == 'native' else arguments
    if not admitted(selectors):
        print('DISABLED: unadmitted native invocation: ' + ' '.join(selectors), file=sys.stderr)
        return 2
    if args.action == 'admit':
        return 0
    return run_commands([arguments])


class TerminationRequested(BaseException):
    pass


def terminate(_signal, _frame):
    raise TerminationRequested()


if __name__ == '__main__':
    signal.signal(signal.SIGTERM, terminate)
    signal.signal(signal.SIGINT, terminate)
    try:
        sys.exit(main())
    except (KeyboardInterrupt, TerminationRequested):
        sys.exit(130)
    except (OSError, ValueError, RuntimeError) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        sys.exit(2)
