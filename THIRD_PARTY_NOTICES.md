# License scope and third-party notices

The root MIT license applies to author-added runtime/reclaim/driver modifications,
analysis and packaging code, and associated software documentation. Existing
copyright headers remain in place. It does not replace third-party licenses or
assign a new license to the article PDFs, third-party vectors, or index archives.

| Component | Provenance and retained license |
| --- | --- |
| Greator/DiskANN-derived implementation | iDC-NEU/Greator, commit `cbc85044ff3b208b6b1f26b7a3de6a3d2898bd8e`; `src/native/source/LICENSE` and `LICENSES/Greator-upstream.txt` |
| NSG-derived portions | `src/native/source/NOTICE.txt` and `LICENSES/NSG-NOTICE.txt`, MIT notice |
| Tessil robin-map/related headers | `src/native/source/include/tsl/LICENSE`, MIT; full original notice retained |
| CTPL thread pool header | `src/native/source/include/ctpl_stl.h`, Apache-2.0 notice; full license at `LICENSES/Apache-2.0.txt` |
| Bundled tcmalloc headers | Original headers and `src/native/source/include/tcmalloc/LICENSE.txt` retained; Apache-2.0 |
| libaio, TBB, OpenBLAS, LAPACKE and system libraries | External build dependencies; no installed packages or library binaries are redistributed in this artifact |

The pinned upstream Greator LICENSE begins with the legacy text `<<<<<<< HEAD`.
The same bytes were checked against the authoritative file at the pinned commit:
https://github.com/iDC-NEU/Greator/blob/cbc85044ff3b208b6b1f26b7a3de6a3d2898bd8e/LICENSE
The notice is preserved as received; it is not a license invented by this export.
An upstream cleanup can be tracked separately without changing frozen evidence.

Original measurement records are provided as the evidence supporting the article;
the selected code license does not purport to relicense external datasets.
See DATA.md for the boundary between numerical records and third-party assets.
