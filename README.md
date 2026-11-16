# AIO context lifecycle evaluation

Code, data, and analysis for *Evaluating Asynchronous I/O Context Lifecycles in Small-Batch On-Disk Graph Maintenance*.

## Recompute the results

From the extracted package, run with Python 3.10 or newer.

```sh
python scripts/reproduce.py
```

The offline replay checks file integrity and recomputes summaries for 322 formal runs, paired effects, stage timings, sensitivity checks, and quota calculations. It uses archived data and writes to [outputs/](outputs/).

Optional bootstrap intervals and figure generation use separate dependencies.

```sh
python -m pip install -r requirements-statistics.txt
python scripts/reproduce.py --bootstrap

python -m pip install -r requirements-figures.txt
python scripts/plot_results.py
```

## Find the source and data

| Location | Contents |
| --- | --- |
| [src/laptop/](src/laptop/) | Source snapshots for Experiments 1–3, including the Experiment 3 patch and helper |
| [src/native/](src/native/) and [src/variants/C8/](src/variants/C8/) | Desktop implementations for Experiments 4 and 5 |
| [src/diagnostics/](src/diagnostics/) | AIO component and quota diagnostics |
| [data/](data/) and [analysis/](analysis/) | Measurement records and statistical code |
| [protocols/](protocols/), [audits/](audits/), and [provenance/](provenance/) | Experimental settings, correctness checks, and source and input hashes |
| [validation/](validation/) | Archived validation records |

See the [data guide](DATA.md) for benchmark sources and input coverage, the [result file index](docs/CLAIMS_AND_EVIDENCE.md) for measurements behind the paper, and the [native guide](docs/NATIVE_REPRODUCTION.md) for builds and new Linux measurements. New performance runs require prepared inputs in addition to the files in this package.

The [license](LICENSE) and [third-party notices](THIRD_PARTY_NOTICES.md) describe the terms for the included code.
