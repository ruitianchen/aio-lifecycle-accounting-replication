import unittest
from analysis_core import interval_metrics, exact_sign_p, describe_pair, holm_adjust


class AnalysisTests(unittest.TestCase):
    def test_overlapping_intervals_separate_sum_and_union(self):
        x = interval_metrics([(0, 4), (1, 2), (3, 5)])
        self.assertEqual(x['sum'], 7)
        self.assertEqual(x['union'], 5)
        self.assertEqual(x['peak'], 2)

    def test_shared_endpoints_and_zero_length_do_not_inflate_overlap(self):
        x = interval_metrics([(0, 1), (1, 2), (1, 1)])
        self.assertEqual(x['union'], 2)
        self.assertEqual(x['peak'], 1)

    def test_median_ratio_is_not_median_paired_percentage(self):
        x = describe_pair([10, 20], [8, 22])
        self.assertEqual(x['median_ratio_reduction_pct'], 0)
        self.assertAlmostEqual(x['paired_median_pct'], 5)

    def test_exact_two_sided_direction_test(self):
        self.assertEqual(exact_sign_p([1]*6), 0.03125)
        self.assertEqual(exact_sign_p([1, 1, 1, -1, -1, -1]), 1)
        self.assertEqual(exact_sign_p([0, 1, 1, 1, 1, 1]), 0.0625)

    def test_holm_adjustment_keeps_family_and_input_order(self):
        self.assertEqual(holm_adjust([0.04, 0.01, 0.03]), [0.06, 0.03, 0.06])


if __name__ == '__main__':
    unittest.main(verbosity=2)
