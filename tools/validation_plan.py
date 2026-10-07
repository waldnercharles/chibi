"""Reviewed affected-contract checks and frozen Git identity; plans are not passes."""
import json
from pathlib import Path
import subprocess
import sys

from test_policy import admitted, TOOLS


FULL = 'make test-full'  # Quarantined: fails closed; never schedules the full suite.
INTEGRATION = ['make check-format']
QUARANTINED = [FULL, 'make test-render', 'make test-compiler-parity']


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
    parser.add_argument('--tools', action='store_true', help='Include the admitted essential C tooling aggregate')
    parser.add_argument('--tool', action='append', default=[], choices=sorted(TOOLS),
                        help='Plan one admitted complete tooling command; repeat as needed')
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
        if not admitted([selector]) or selector not in cpu:
            raise ValueError(f'selector is quarantined for agent-test: {selector}')
    reviewed = args.scope in ('focused', 'mixed')
    if reviewed and (not args.review_note.strip() or not (selectors or args.tools or args.tool)):
        raise ValueError('reviewed scope requires a review note and admitted selectors or tooling')
    base, head = commit(root, args.base), commit(root, args.head)
    if head != commit(root, 'HEAD') or git(root, 'status', '--porcelain', '--untracked-files=normal'):
        raise ValueError('check out the planned head with a clean worktree; save plans/logs outside the worktree')
    git(root, 'merge-base', '--is-ancestor', base, head)
    changes = git(root, 'diff', '--name-status', '--no-renames', base, head, '--').splitlines()
    focused = ['make test-tools'] if args.tools or args.tool else []
    for selector in selectors:
        focused.append(f'make agent-test AGENT_SELECTOR={selector}')
    return dict(
        schema=4, full_verification=False,
        identity=dict(base_ref=args.base, base=base, head_ref=args.head, head=head,
                      tree=git(root, 'rev-parse', head + '^{tree}'),
                      sources={ref: commit(root, ref) for ref in sorted(set(args.source))}),
        scope=args.scope, review_note=args.review_note, changes=changes,
        member_eligibility='operator-confirmed; not independently verified' if args.source else 'not-declared',
        classification='Reviewer declaration; paths/extensions do not prove impact or coverage.',
        iteration_commands=focused if reviewed else INTEGRATION,
        validation_basis='reviewed affected contracts' if reviewed else 'scope review required',
        execution_evidence='Local or CI results on this candidate; CI is required only by applicable server rules.',
        integration_commands=INTEGRATION,
        quarantined_commands=QUARANTINED,
        scope_specific_commands={'web/UI': 'make web'},
        pending=[
            'Independent review confirms the selected checks cover all affected contracts and combined interactions',
            'Run selected checks and formatting on this clean candidate; record configuration, exit codes and logs',
            'Build and verify affected native/web/release configurations where applicable',
            'Recheck source identity, explicit holds, required approvals and actual server protections before atomic landing',
        ] + ([] if reviewed else ['Resolve unknown scope and choose checks before landing']),
        unavailable_coverage='Full native/render DISABLED by the 60-second quarantine, NOT PASSED; unavailable affected coverage needs a reviewed admitted replacement or remains a blocker.',
        requirements='docs/testing.md#batch-validation-cut62; applicable server protections',
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
