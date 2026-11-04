"""Case38 bounded faults at the actual control-admission read boundary.

A valid admitted baseline is checked first. Each case changes one returned input
or raises the declared absent-input error. Files are not rewritten. The one
claim case invokes the actual exclusive writer twice on its own private file.
No process/native launch or synthetic acceptance is involved in these tables.
"""
import copy
import os
from pathlib import Path
import types

VARIANTS = ('source', 'artifact', 'outer', 'runtime', 'argv', 'environment', 'cwd',
            'affinity', 'path', 'descriptor_exec', 'acceptance_missing', 'receipt_missing')


def run_admission_tables(controller, support):
    plan, intent = controller.admission()
    baseline_read, baseline_os = controller.read, controller.os
    home = controller.H
    acceptance_path = Path(intent['acceptance_path'])
    artifact_path = Path(plan['artifact_admission']['path'])
    results = []
    for variant in VARIANTS:
        def fault_read(path):
            path = Path(path)
            if variant == 'acceptance_missing' and path == acceptance_path:
                raise FileNotFoundError('synthetic absent independent acceptance')
            if variant == 'receipt_missing' and path == Path(plan['source_admission']['path']):
                raise FileNotFoundError('synthetic absent independent source receipt')
            value = copy.deepcopy(baseline_read(path))
            if path == home/'CONTROL-PLAN.json':
                if variant == 'source': value['source_sha256']['control_binding.py'] = '0' * 64
                elif variant == 'artifact': value['fixture']['sha256'] = '0' * 64
                elif variant == 'outer': value['source_sha256']['outer_owner.py'] = '0' * 64
                elif variant == 'argv': value['fixture_argv_prefix'][1] = '--wrong'
                elif variant == 'environment': value['environment']['PATH'] = '/wrong'
                elif variant == 'cwd': value['working_root'] = str(home/'wrong')
                elif variant == 'affinity': value['cpu_affinity'] = [0]
            if path == home/'suite-intent.json' and variant == 'path':
                value['acceptance_path'] = str(home/'CONTROL-PLAN.json')
            if path == artifact_path and variant == 'runtime':
                assert value['files'], 'admitted runtime closure cannot be empty'
                value['files'][0]['sha256'] = '0' * 64
            return value
        controller.read = fault_read
        if variant == 'descriptor_exec':
            controller.os = types.SimpleNamespace(**vars(os))
            controller.os.supports_fd = set()
        error = None
        try:
            controller.admission()
        except (RuntimeError, ValueError, FileNotFoundError) as failure:
            error = repr(failure)
        finally:
            controller.read, controller.os = baseline_read, baseline_os
        assert error is not None, ('changed admission input accepted', variant)
        results.append(dict(variant=variant, rejected=True, reason=error, read_boundary_model=True))
    parent = dict(pid=11, ppid=10, pgid=11, sid=11, start_ticks=100)
    argv = b'/usr/bin/python3\0-I\0-S\0-B\0' + os.fsencode(home/'control_controller.py') + b'\0--worker\0'
    support.parent_guard(parent, parent, argv)
    for name, actual, command in (('parent_identity', dict(parent, start_ticks=101), argv),
                                   ('parent_argv', parent, argv + b'--extra\0')):
        error = None
        try:
            support.parent_guard(parent, actual, command)
        except ValueError as failure:
            error = str(failure)
        assert error is not None
        results.append(dict(variant=name, rejected=True, reason=error, model=True))
    claim = home/'model-exclusive.claim'
    controller.save(claim, dict(purpose='case38 exclusive claim negative'))
    error = None
    try:
        controller.save(claim, dict(purpose='must not replace the consumed claim'))
    except FileExistsError as failure:
        error = str(failure)
    assert error is not None and controller.read(claim)['purpose'] == 'case38 exclusive claim negative'
    results.append(dict(variant='claim_reused', rejected=True, reason=error, model=True))
    return results


if __name__ == '__main__':
    raise SystemExit('UNARMED: admission tables require a valid independently admitted baseline')
