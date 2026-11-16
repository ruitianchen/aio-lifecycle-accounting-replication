# Platform A source snapshots

| Directory | Experiment | Recorded groups |
| --- | --- | --- |
| [experiment_1/](experiment_1/) | Workload sensitivity | P1–P4 |
| [experiment_2/](experiment_2/) | Maintenance scheduling | C1 and C2 |
| [experiment_3/](experiment_3/) | Parallel reclamation | C7 |

Each snapshot has a `source_provenance.json` with source hashes, archived build commands, and original binary hashes. In Experiment 3, the reader patch adds 19 lines relative to Experiment 2, and `parallel_release.hpp` has 61 nonblank lines. The snapshot contains the patch, reader, and helper.
