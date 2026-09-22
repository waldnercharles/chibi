import unittest
from evaluate import matches


class EvaluationTests(unittest.TestCase):
    def test_equivalent_numbers_and_local_copy_references(self):
        expected = [{'op': 'duplicate', 'target': 'a', 'result': 'copy:0'},
                    {'op': 'set', 'target': 'a', 'field': 'width', 'result_target': 'copy:0', 'value': 110.00000000000001}]
        actual = [{'op': 'duplicate', 'target': 'a', 'result': 'copy'},
                  {'op': 'set', 'target': 'a', 'field': 'width', 'result_target': 'copy', 'value': 110}]
        self.assertTrue(matches(expected, actual))
        del actual[1]['result_target']
        self.assertFalse(matches(expected, actual))

    def test_wrong_colors_booleans_and_duplicate_operations_fail(self):
        op = {'op': 'set', 'target': 'a', 'field': 'color', 'value': 0x8000ff88}
        self.assertFalse(matches([op], [{**op, 'value': 0x8000ff89}]))
        self.assertFalse(matches([{**op, 'value': True}], [{**op, 'value': 1}]))
        self.assertFalse(matches([op, {**op, 'target': 'b'}], [op, op]))


if __name__ == '__main__':
    unittest.main()
