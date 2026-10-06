"""CUT62 plans fail closed using disposable Git repos; no graphics or builds."""
import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import agent


class ValidationPlanTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'repo'
        self.root.mkdir()
        self.git('init', '-q')
        self.change('base')
        self.git('branch', 'base')
        self.change('candidate')
        self.git('branch', 'source')
        self.args = ['--base', 'base', '--source', 'source', '--scope', 'focused',
                     '--confirm-eligible-members',
                     '--review-note', 'Reviewer checked graph callers and build effects',
                     '--selector=--graph-optimize']
        self.saved = Path(self.temp.name) / 'plan.json'

    def git(self, *args):
        return subprocess.check_output(['git', *args], cwd=self.root, text=True,
                                       stderr=subprocess.PIPE).strip()

    def change(self, value):
        (self.root / 'contract.md').write_text(value)
        self.git('add', 'contract.md')
        self.git('-c', 'user.name=CUT62 test', '-c', 'user.email=test@example.invalid',
                 '-c', 'core.hooksPath=/dev/null', 'commit', '-qm', value)

    def invoke(self, args=None):
        out, err = io.StringIO(), io.StringIO()
        with patch.object(agent, 'ROOT', self.root), \
                patch.object(agent, 'startup_health', side_effect=AssertionError('graphics probe')), \
                patch.object(agent, 'run', side_effect=AssertionError('executed a plan command')), \
                patch.object(sys, 'argv', ['agent.py', 'validation-plan', *(self.args if args is None else args)]), \
                contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = agent.main()
        return code, json.loads(out.getvalue()) if out.getvalue() else None, err.getvalue()

    def save(self):
        code, plan, _ = self.invoke()
        self.assertEqual(code, 0)
        self.saved.write_text(json.dumps(plan))
        return plan

    def verify(self):
        return self.invoke([*self.args, '--verify', str(self.saved)])

    def test_deterministic_identity_and_focused_coverage_remain_pending(self):
        plan = self.save()
        self.assertEqual(plan, self.invoke()[1])
        self.assertEqual(plan['identity']['head'], self.git('rev-parse', 'HEAD'))
        self.assertEqual(plan['identity']['tree'], self.git('rev-parse', 'HEAD^{tree}'))
        self.assertEqual(plan['changes'], ['M\tcontract.md'])
        self.assertEqual(plan['iteration_commands'], ['make agent-test AGENT_SELECTOR=--graph-optimize'])
        self.assertEqual(plan['integration_commands'][0], 'make test-full')
        self.assertTrue(any('DISABLED' in check and 'NOT PASSED' in check for check in plan['pending']))
        code, result, _ = self.verify()
        self.assertEqual(code, 0)
        self.assertEqual(result['status'], 'identity-current')
        self.assertFalse(result['full_verification'])
        self.assertTrue(result['pending'])

    def test_unknown_and_mixed_scope_always_fall_back_to_full(self):
        for scope in ['unknown', 'mixed']:
            with self.subTest(scope=scope):
                code, plan, _ = self.invoke([*self.args, '--scope', scope])
                self.assertEqual(code, 0)
                self.assertEqual(plan['iteration_commands'], plan['integration_commands'])
                self.assertFalse(plan['full_verification'])
        code, plan, _ = self.invoke(['--base', 'base'])
        self.assertEqual(code, 0)
        self.assertEqual(plan['scope'], 'unknown')  # .md is not proof of no native impact.

    def test_focused_scope_requires_review_and_checks(self):
        for args in [['--base', 'base', '--scope', 'focused'],
                     [*self.args, '--review-note', ' ']]:
            self.assertEqual(self.invoke(args)[0], 2)

    def test_member_eligibility_requires_fresh_operator_confirmation_in_every_scope(self):
        self.save()
        unconfirmed = [arg for arg in self.args if arg != '--confirm-eligible-members']
        for scope in ['focused', 'unknown', 'mixed']:
            code, plan, error = self.invoke([*unconfirmed, '--scope', scope])
            self.assertEqual(code, 2)
            self.assertIsNone(plan)
            self.assertIn('holds and required approvals', error)
        self.assertEqual(self.invoke([*unconfirmed, '--verify', str(self.saved)])[0], 2)
        self.assertEqual(self.verify()[1]['member_eligibility'], 'operator-confirmed; not independently verified')
        self.assertEqual(self.invoke(['--base', 'base'])[1]['member_eligibility'], 'not-declared')
        self.assertEqual(self.invoke(['--base', 'base', '--confirm-eligible-members'])[0], 2)

    def test_unknown_empty_and_combined_selectors_fail(self):
        for selector in ['', '--not-a-test', '--graph-optimize --materials', '--graph-optimize;false']:
            self.assertEqual(self.invoke([*self.args, '--selector=' + selector])[0], 2)

    def test_gpu_and_tools_commands_are_only_planned(self):
        code, plan, _ = self.invoke([*self.args, '--selector=--model-sampled-gpu', '--tools',
                                    '--review-note', '$(exit 1); reviewer data'])
        self.assertEqual(code, 0)
        self.assertIn('make agent-gpu-test AGENT_SELECTOR=--model-sampled-gpu', plan['iteration_commands'])
        self.assertIn('make test-tools', plan['iteration_commands'])
        self.assertFalse(plan['full_verification'])

    def test_changed_base_invalidates_even_when_tree_is_unchanged(self):
        self.save()
        self.git('branch', '-f', 'base', 'HEAD')
        self.assertEqual(self.verify()[0], 2)

    def test_changed_head_invalidates_even_when_tree_is_unchanged(self):
        self.save()
        self.git('-c', 'user.name=CUT62 test', '-c', 'user.email=test@example.invalid',
                 '-c', 'core.hooksPath=/dev/null', 'commit', '--allow-empty', '-qm', 'new metadata')
        self.assertEqual(self.verify()[0], 2)

    def test_changed_source_and_dirty_tree_invalidate(self):
        self.save()
        self.git('branch', '-f', 'source', 'base')
        self.assertEqual(self.verify()[0], 2)
        self.git('branch', '-f', 'source', 'HEAD')
        (self.root / 'contract.md').write_text('uncommitted edit')
        self.assertEqual(self.verify()[0], 2)
        self.git('restore', 'contract.md')
        (self.root / 'untracked.c').write_text('untracked source')
        self.assertEqual(self.verify()[0], 2)

    def test_wrong_checkout_git_failure_and_malformed_plan_fail(self):
        self.assertEqual(self.invoke([*self.args, '--head', 'base'])[0], 2)
        self.assertEqual(self.invoke([*self.args, '--base', 'missing-ref'])[0], 2)
        self.saved.write_text('{broken json')
        self.assertEqual(self.verify()[0], 2)
        plan = self.save()
        plan['full_verification'] = True
        self.saved.write_text(json.dumps(plan))
        self.assertEqual(self.verify()[0], 2)

    def test_existing_focused_runner_propagates_child_failure(self):
        with self.assertRaisesRegex(RuntimeError, 'child_exit_7'):
            agent.run([sys.executable, '-c', 'raise SystemExit(7)'], timeout=5)


if __name__ == '__main__':
    unittest.main()
