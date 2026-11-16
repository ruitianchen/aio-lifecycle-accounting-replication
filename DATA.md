# Data and inputs

- [Formal runs](data/formal_windows.csv) contains 322 measurement records. `complete_s` measures the complete task, including final cleanup and file and directory synchronization. [Original tables](data/original_tables/) and [their hashes](data/lineage.json) document the source data.
- [C8](data/C8/) and [C9](data/C9/) contain the desktop measurements. Detailed AIO intervals come from qualification runs. Exclusive phase intervals use integer nanosecond endpoints from `CLOCK_MONOTONIC_RAW`. The C8 `package_overlap` flag identifies the run that overlapped installation of lm-sensors. The replay reports results with all blocks and with that block omitted.
- [Prepared desktop inputs](data/workload_C8_C9/) contains 73 non-vector files, including update traces, ground truth, true distances, and live labels. The [input hash list](configs/native_input_hashes.json) identifies all 83 required files. The three vector files and seven initial-index files are external.

## Benchmark sources

- [SIFT1M / TexMex](http://corpus-texmex.irisa.fr/)
- [DEEP / Big ANN Benchmarks](https://big-ann-benchmarks.com/neurips21.html)

The study uses 128-dimensional SIFT and 96-dimensional DEEP vectors with L2 distance and no additional normalization. The measurements with 500,000 and 990,000 initial vectors use 10,000 reserve vectors. Each complete maintenance task performs 10,000 deletions and 10,000 insertions.

The [external archive inventory](provenance/external_archives.json) records the sizes and hashes of the prepared inputs and retained indexes. Use the matching graph, vector files, trace, and ground truth together for new runs. The corresponding author can provide full raw telemetry upon reasonable request.
