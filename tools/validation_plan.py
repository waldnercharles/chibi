"""CUT62: reviewable plans and Git identity checks, never gate certification."""
import json
from pathlib import Path
import subprocess
import sys


FULL = 'CHIBI_EXECUTION_PROFILE=hardware CHIBI_ALLOW_FULL_GPU=1 make test test-render'
INTEGRATION = [FULL, 'make check-format']


def add_parser(sub):
    parser = sub.add_parser('validation-plan', help=__doc__)
    parser.add_argument('--base', required=True, help='Freshly fetched main ref')
    parser.add_argument('--head', default='HEAD')
    parser.add_argument('--source', action='append', default=[], help='Reviewed source ref; repeat per PR')
    parser.add_argument('--confirm-eligible-members', action='store_true',
                        help='Operator checked every member review, PR/ticket hold and required approval; grants no authority')
    parser.add_argument('--scope', choices=['focused', 'unknown', 'mixed'], default='unknown')
    parser.add_argument('--review-note', default='', help='Reviewer, changed contract and why these checks cover it')
    parser.add_argument('--selector', action='append', default=[], help='Use --selector=--name; repeat as needed')
    parser.add_argument('--tools', action='store_true', help='Include make test-tools in focused work')
    parser.add_argument('--verify', type=Path, help='Compare a saved plan to current local refs; does not fetch or certify tests')


def git(root, *args):
    return subprocess.check_output(['git', *args], cwd=root, text=True, stderr=subprocess.PIPE).strip()


def commit(root, ref):
    return git(root, 'rev-parse', '--verify', '--end-of-options', ref + '^{commit}')


def make_plan(args, root, cpu, gpu):
    if args.source and not args.confirm_eligible_members:
        raise ValueError('declared members require --confirm-eligible-members after checking reviews, holds and required approvals')
    if args.confirm_eligible_members and not args.source:
        raise ValueError('eligibility confirmation requires declared --source members')
    selectors = sorted(set(args.selector))
    for selector in selectors:
        if selector not in cpu and selector not in gpu:
            raise ValueError(f'unclassified selector: {selector}')
    if args.scope == 'focused' and (not args.review_note.strip() or not (selectors or args.tools)):
        raise ValueError('focused scope requires a review note and selectors or --tools')
    base, head = commit(root, args.base), commit(root, args.head)
    if head != commit(root, 'HEAD') or git(root, 'status', '--porcelain', '--untracked-files=normal'):
        raise ValueError('check out the planned head with a clean worktree; save plans/logs outside the worktree')
    git(root, 'merge-base', '--is-ancestor', base, head)
    changes = git(root, 'diff', '--name-status', '--no-renames', base, head, '--').splitlines()
    focused = ['make test-tools'] if args.tools else []
    for selector in selectors:
        target = 'agent-test' if selector in cpu else 'agent-gpu-test'
        focused.append(f'make {target} AGENT_SELECTOR={selector}')
    return dict(
        schema=2, full_verification=False,
        identity=dict(base_ref=args.base, base=base, head_ref=args.head, head=head,
                      tree=git(root, 'rev-parse', head + '^{tree}'),
                      sources={ref: commit(root, ref) for ref in sorted(set(args.source))}),
        scope=args.scope, review_note=args.review_note, changes=changes,
        member_eligibility='operator-confirmed; not independently verified' if args.source else 'not-declared',
        classification='Reviewer declaration; paths/extensions do not prove impact or coverage.',
        iteration_commands=focused if args.scope == 'focused' else INTEGRATION,
        integration_commands=INTEGRATION,
        scope_specific_commands={'web/UI': 'make web', 'compiler parity': 'make test-compiler-parity'},
        pending=[
            'Recheck every member PR/ticket hold and required approval before admission/landing; confirmation grants no merge authority',
            'Independent source review of each pinned source and combined diff/conflict resolutions',
            'Full native/render gate and formatting on the immutable combined tree',
            'CI matrix: Debug + ASan/UBSan + SLJIT; RelWithDebInfo + SLJIT; scaling; no-SLJIT rejection; development application and host checks',
            'Required web build/UI verification and scope-specific backend/parity/load checks; release validation when applicable',
            'Owner records actual commands, flags, dependencies, device, exit codes, timings and logs; configured CI is not execution',
            'Fresh main/source identity check and current-head server requirements before one atomic landing',
        ],
        requirements='docs/testing.md#batch-validation-cut62; .github/workflows/ci.yml; .github/workflows/deploy-web.yml',
    )


def validation_plan(args, root, cpu, gpu):
    try:
        plan = make_plan(args, root, cpu, gpu)
        if args.verify:
            if json.loads(args.verify.read_text()) != plan:
                raise ValueError('stale or changed plan; review and regenerate before using evidence')
            # Identity equality never upgrades selectors or missing logs to a pass.
            print(json.dumps(dict(status='identity-current', full_verification=False,
                                  identity=plan['identity'], member_eligibility=plan['member_eligibility'],
                                  pending=plan['pending']), sort_keys=True))
            return 0
        print(json.dumps(plan, indent=2, sort_keys=True))
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f'Validation plan blocked: {error}', file=sys.stderr)
        return 2
