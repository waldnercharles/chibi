import json
import unittest
from pathlib import Path

from shadow import candidates, shortlist


class ShadowTests(unittest.TestCase):
    def test_actions_remain_grounded_in_available_capabilities(self):
        catalog = json.loads((Path(__file__).parent / 'evals/catalog.json').read_text())
        catalog['capabilities'] = ['select']
        actions = candidates(catalog)
        self.assertEqual([a['operations'] for a in actions], [
            [{'op': 'select', 'target': 'entity:marker0'}],
            [{'op': 'select', 'target': 'entity:marker1'}]])

    def test_retrieval_does_not_invent_values_or_actions(self):
        catalog = json.loads((Path(__file__).parent / 'evals/catalog.json').read_text())
        actions = candidates(catalog)
        offered = shortlist(actions, 'Set Marker width to 42 and erase the project')
        self.assertLessEqual(len(offered), 5)
        self.assertTrue(all(item in actions for item in offered))
        self.assertFalse(any(op['op'] in {'set', 'delete'} for item in offered for op in item['operations']))


if __name__ == '__main__':
    unittest.main()
