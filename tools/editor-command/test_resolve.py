"""Exercise the real model-to-editor boundary with controlled model responses."""
import copy
import json
import unittest
from pathlib import Path

from contract import Observation, Operation, Proposal, typed_value
import resolve

CATALOG = json.loads((Path(__file__).parent / 'evals/catalog.json').read_text())
CATALOG['capabilities'] = list(Operation.model_fields['op'].annotation.__args__)


class CommandTests(unittest.TestCase):
    def run_model(self, command, answer, **packet):
        def model(instructions, prompt, schema, inspect):
            state = json.loads(prompt)
            return schema.model_validate({'decision': 'act', **answer(state, inspect)}), {'model': 'test'}
        return resolve.resolve({'catalog': copy.deepcopy(CATALOG), 'command': command, **packet}, model)

    def test_two_colors_preserve_distinct_alpha_channels(self):
        result = self.run_model('Change both colors', lambda *_: {'operations': [
            {'op': 'set', 'target': 'o0', 'field': 'f3', 'value': '#00ff00'},
            {'op': 'set', 'target': 'o0', 'field': 'f4', 'value': '#8000ff'}]})
        self.assertEqual([op['value'] for op in result['operations']], [0x00ff0044, 0x8000ff88])
        self.assertEqual(result['revision'], CATALOG['revision'])
        self.assertEqual(result['context']['targets'], ['prototype:boost'])

    def test_unsupported_partial_proposal_never_executes(self):
        result = self.run_model('Edit and unsupported action', lambda *_: {
            'unsupported': 'Cannot do the entire request',
            'operations': [{'op': 'set', 'target': 'o0', 'field': 'f5', 'value': 20}]})
        self.assertEqual(result['status'], 'unsupported')
        self.assertEqual(result['operations'], [])

    def test_color_opacity_is_separate_and_explicit(self):
        field = {'type': 'color', 'value': 0x12345644}
        self.assertEqual(typed_value(field, '#00ff00'), 0x00ff0044)
        self.assertEqual(typed_value(field, '#00ff00', 0), 0x00ff0000)
        with self.assertRaises(ValueError):
            typed_value(field, '#00ff0000')
        with self.assertRaises(ValueError):
            typed_value({'type': 'number'}, 2, 0)

    def test_stale_clarification_stops_before_inference(self):
        result = self.run_model('Open it', lambda *_: self.fail('Should not call model'), bindings={'target': 'deleted'})
        self.assertEqual(result['status'], 'failed')
        self.assertEqual(result['operations'], [])

    def test_clarification_uses_real_identity_and_resumes_with_binding(self):
        result = self.run_model('Open BoostEmitter', lambda *_: {'question': 'Which asset?', 'choices': ['o0', 'o3']})
        self.assertEqual([c['id'] for c in result['choices']], ['prototype:boost', 'behavior:boost'])
        def answer(state, _):
            self.assertEqual(state['reply']['target'], 'o3')
            return {'operations': [{'op': 'open', 'target': 'o3'}]}
        result = self.run_model('Open BoostEmitter', answer, bindings={'target': 'behavior:boost'})
        self.assertEqual(result['operations'], [{'op': 'open', 'target': 'behavior:boost'}])

    def test_followup_context_is_grounded_and_document_scoped(self):
        def answer(state, _):
            self.assertEqual(state['previous_targets'][0]['id'], 'o1')
            self.assertEqual(state['previous_targets'][0]['name'], 'TrailEmitter')
            return {'operations': [{'op': 'set', 'target': 'o1', 'field': 'f5', 'value': 23}]}
        self.assertTrue(self.run_model('Set its rate to 23', answer, context={'targets': ['prototype:trail']})['ok'])
        def stale(state, _):
            self.assertEqual(state['previous_targets'], [])
            return {'unsupported': 'No target'}
        self.run_model('Set its rate to 23', stale, context={'targets': ['prototype:trail'], 'document': 'another'})

    def test_unseen_properties_need_inspection(self):
        def answer(state, inspect):
            inspect(['o2'])
            return {'operations': [{'op': 'set', 'target': 'o2', 'field': 'f5', 'value': 'newly generated action'}]}
        result = self.run_model('Give the other object a new action name', answer)
        self.assertEqual(result['operations'][0]['value'], 'newly generated action')

    def test_retrieved_properties_do_not_authorize_unseen_fields(self):
        observation = Observation(CATALOG)
        detail = observation.inspect(['o0'], 'Change its rate')[0]
        self.assertTrue(detail['more_fields'])
        self.assertEqual([field['id'] for field in detail['fields']], ['f5'])
        color = Operation(op='set', target='o0', field='f3', value='#00ff00')
        with self.assertRaisesRegex(ValueError, 'Inspect'):
            observation.operations([color])
        observation.inspect(['o0'])
        self.assertEqual(observation.operations([color])[0]['value'], 0x00ff0044)

    def test_retrieval_keeps_matching_properties_and_directory_ambiguity(self):
        def answer(state, inspect):
            self.assertEqual(len(state['objects']), len(CATALOG['objects']))
            emitter = next(obj for obj in state['details'] if obj['id'] == 'o0')
            self.assertTrue({'f3', 'f4'} <= {field['id'] for field in emitter['fields']})
            return {'decision': 'ask', 'question': 'Which asset?', 'choices': ['o0', 'o3']}
        result = self.run_model('Change BoostEmitter color', answer)
        self.assertEqual(result['status'], 'needs_clarification')

    def test_atomic_validation_rejects_entire_invalid_batch(self):
        for bad in [
            {'op': 'set', 'target': 'o0', 'field': 'invented', 'value': 4},
            {'op': 'set', 'target': 'missing', 'field': 'f5', 'value': 4},
            {'op': 'set', 'target': 'o0', 'field': 'f5', 'value': '25'},
            {'op': 'set', 'target': 'o0', 'field': 'f5', 'value': True},
            {'op': 'set', 'target': 'o0', 'field': 'f5', 'value': float('nan')},
            {'op': 'set', 'target': 'o0', 'field': 'f6', 'value': 1.5},
            {'op': 'set', 'target': 'o0', 'field': 'f2', 'value': 900},
        ]:
            with self.subTest(bad=bad):
                result = self.run_model('Set BoostEmitter rate to 20', lambda *_: {'operations': [
                    {'op': 'set', 'target': 'o0', 'field': 'f5', 'value': 20}, bad]})
                self.assertEqual(result['status'], 'failed')
                self.assertEqual(result['operations'], [])

    def test_graph_connection_and_duplicate_reference(self):
        o = Observation(CATALOG)
        o.inspect(['o6', 'o7', 'o4'])
        self.assertEqual(o.operations([Operation(op='connect', target='o6', field='f0', to='o7', input='f0')]),
                         [{'op': 'connect', 'target': 'node:a', 'field': 'out', 'destination': 'node:b', 'pin': 'in'}])
        ops = o.operations([Operation(op='duplicate', target='o4', result='copy'),
                            Operation(op='set', target='o4', result_target='copy', field='f0', value=40)])
        self.assertEqual(ops[1]['result_target'], 'copy')
        with self.assertRaises(ValueError):
            o.operations([Operation(op='set', target='o4', result_target='missing', field='f0', value=40)])
        with self.assertRaises(ValueError):
            o.operations([Operation(op='connect', target='o7', field='f0', to='o6', input='f0')])

    def test_constraints_and_capabilities_are_authoritative(self):
        with self.assertRaises(ValueError):
            typed_value({'type': 'number', 'min': 0}, -1)
        catalog = copy.deepcopy(CATALOG)
        catalog['capabilities'] = ['open']
        o = Observation(catalog)
        o.inspect(['o0'])
        with self.assertRaises(ValueError):
            o.operations([Operation(op='set', target='o0', field='f5', value=5)])


if __name__ == '__main__':
    unittest.main()
