# Result files

| Measurement | Records |
| --- | --- |
| Complete-task times and context counts | [Formal runs](../data/formal_windows.csv) and [original result tables](../data/original_tables/) |
| Laptop parallel reclamation (Experiment 3) | [C7 result table](../data/original_tables/C7.csv) |
| Desktop Pool and Parallel comparison (Experiment 4) | [C8 run values](../data/C8/window_values.csv), [analysis](../data/C8/analysis.json), and [phase intervals](../data/C8/exclusive_segments.csv) |
| Desktop persistent and dynamic worker comparison (Experiment 5) | [C9 run values](../data/C9/window_values.csv), [analysis](../data/C9/analysis.json), and [phase intervals](../data/C9/exclusive_segments.csv) |
| Lock-scope and first-block sensitivity | [P1 result table](../data/original_tables/P1.csv) and [offline comparisons](../outputs/P1_lock_comparisons.csv) |
| Idle AIO quota and concurrency | [Resource scenarios](../data/figure_sources_A/resource_scenes.csv) |
| Replay summary and checks | [Offline replay report](../outputs/REPRODUCTION_REPORT.json) and [outputs/](../outputs/) |

Median time reduction is `100 * (1 - median(target) / median(reference))`. Paired bootstrap intervals estimate a transformed mean log ratio. Stage savings are means of paired reference-minus-target differences, which add to the mean complete-task difference.
