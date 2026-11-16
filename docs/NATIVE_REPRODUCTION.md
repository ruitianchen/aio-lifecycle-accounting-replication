# Native builds and measurements

The commands below target the desktop C8/C9 sources. For Experiments 1–3, see the [laptop snapshots and build records](../src/laptop/README.md).

## Build

Use x86-64 Linux with AVX2 and the Ubuntu development packages `build-essential`, `libaio-dev`, `libtbb-dev`, `libopenblas-dev`, and `liblapacke-dev`.

```sh
python scripts/build_native.py --variant C9 --execute
python scripts/build_native.py --variant C8 --execute
```

These commands build `overall`, `search`, and `pq_oracle`. Use `--output <new-directory>` for an additional build. `--deps-prefix <directory>` uses headers and libraries from an extracted package tree at `usr/include` and `usr/lib/x86_64-linux-gnu`.

## Prepare inputs and run a window

Copy [the example configuration](../configs/native_window.example.json) and set the binary, fixture, seven-file initial index prefix, and fresh output directory. Paths are relative to the configuration. Preserve the layout of [the prepared inputs](../data/workload_C8_C9/) in the fixture and supply the matching `base.bin`, `full.bin`, `query.bin`, and initial index. See [DATA.md](../DATA.md) for coverage and [the input hash list](../configs/native_input_hashes.json) for file identities.

```sh
python scripts/run_native_window.py configs/native_window.example.json
python scripts/run_native_window.py configs/native_window.example.json --verify-inputs
```

The first command prints the plan. The second also checks the input files. Adding `--execute` runs a new window on native Linux. The timer covers driver launch through cleanup and synchronization of the index files and their directory. Index restoration occurs before timing. Keep the configured output path fresh and use a CPU affinity list only when it matches the host topology.

## Qualify the outputs

The [protocols](../protocols/) and [audits](../audits/) describe common qualification, checkpoints, block order, and output checks. Use the build's `search` and `pq_oracle` alongside the supplied validation and evaluation helpers.

```text
search float <index-prefix> 0 1 0 3 4 <fixture>/query.bin <fixture>/gt10.fbin 10 <new-result-prefix> l2 100
python src/native/dependencies/evaluate.py --prefix <index-prefix> --fixture <fixture> --stage 10 --result-prefix <new-result-prefix> --out <new-quality-json>
pq_oracle <index-prefix> <fixture>/full.bin <initial-snapshot-prefix>
```

Check all seven index files, logical I/O counts, and final AIO, lease, and control-operation counts. The duplicate-neighbor exception requires byte-identical vectors and PQ codes plus matching sorted float64 distance signatures after reopening the index. The window helper measures a single run. Apply the common qualification checks before using new runs in a formal comparison.

## Component diagnostics

[Diagnostic sources](../src/diagnostics/) cover empty-context timing, wait-site sampling, and AIO quota testing. [Wait-site cases](../configs/waitsite_cases.json) record the sampled conditions. Recompute the archived diagnostic results with `python scripts/reproduce_diagnostics.py`.
