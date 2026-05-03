# FAISS — vendored subset

This directory contains a **pruned subset** of [Facebook AI Similarity Search
(FAISS)](https://github.com/facebookresearch/faiss), used by texis to provide
the IVFPQ backend of `INDEX_VEC` (see `vec-ivfpq-integration-plan.md`).

## Upstream

- **Project**: facebookresearch/faiss
- **Version**: 1.12.0
- **License**: MIT — see `LICENSE` (verbatim copy of the upstream file).
- **Source of this copy**: copied from `rampart-langtools/extern/faiss/`,
  which itself is a verbatim vendoring of the upstream tarball.

The MIT license requires only that the copyright notice be retained.
That notice is preserved in `LICENSE` and at the head of every vendored
source file.

## What is vendored, and why

Only the files needed to build, query, and mutate an `IndexIVFPQ` backed
by `OnDiskInvertedLists` (mmap-mutable inverted lists).  Approximately
97 files; transitive include closure from this entry-point set:

- `IndexIVFPQ`, `IndexIVF`, `IndexPQ`, `IndexFlat`, `IndexFlatCodes`, `Index`
- `Clustering`, `MetricType`, `VectorTransform`
- `invlists/{InvertedLists, OnDiskInvertedLists, DirectMap, BlockInvertedLists, InvertedListsIOHook}`
- `impl/{ProductQuantizer, IDSelector, AuxIndexStructures, CodePacker, io, FaissAssert, FaissException, platform_macros, …}`
- `impl/code_distance/*` (header-only)
- `utils/{distances, hamming, Heap, sorting, partitioning, random, utils, …}`
- `utils/simdlib*` (CPU dispatch headers; no AVX-only required at runtime)

## What is deliberately excluded

The full upstream tree (~1500 files) is dominated by index types and tooling
that texis does not need.  The following are **not vendored**:

| Excluded | Reason |
|---|---|
| `gpu/` | CPU-only build; GPU is opt-in elsewhere if ever needed (see plan §0). |
| `python/` (and pybind11 deps) | No Python bindings here. |
| `c_api/` | Texisapi is C++; we call `IndexIVFPQ` directly. |
| `tests/`, `benchs/`, `tutorial/`, `demos/`, `perf_tests/`, `contrib/` | Upstream's own test/bench infrastructure. |
| `IndexHNSW*`, `IndexNNDescent*`, `IndexNSG*` | Different ANN families; INDEX_VEC's HNSW backend is usearch, not FAISS. |
| `IndexBinary*` | Binary vectors not in scope. |
| `IndexAdditive*`, `IndexRaBitQ*`, `IndexLattice`, `IndexLSH`, `Index2Layer` | Quantizer families not on the roadmap. |
| `Index*FastScan*`, `pq4_fast_scan*`, `simd_result_handlers` | 4-bit FastScan path is a phase-4 add. |
| `IndexIVFAdditive*`, `IndexIVFFlat`, `IndexIVFRaBitQ`, `IndexIVFSpectralHash`, `IndexIVFIndependentQuantizer`, `IndexIVFPQR`, `IndexIVFPQFastScan` | Other IVF variants. |
| `IndexRefine`, `IndexReplicas`, `IndexShards`, `MetaIndexes`, `IndexIDMap`, `IndexPreTransform`, `IndexRowwiseMinMax`, `IndexScalarQuantizer` | Composite/wrapper indexes. |
| `impl/index_read.cpp`, `impl/index_write.cpp`, `clone_index.{cpp,h}` | The upstream serializer deserializes every index type, dragging in ~150 files of unrelated index code.  We use FAISS's `IOWriter`/`IOReader` (`impl/io.h`) directly to serialize only `IndexIVFPQ` codebooks; `OnDiskInvertedLists` already persists itself. |
| `impl/mapped_io.{cpp,h}`, `invlists/InvertedListsIOHook.cpp` | Only needed by the dropped `index_read` path. |

## Patch policy

This vendoring is **byte-identical to upstream**.  Any local divergence
must be documented here with rationale.

| File | Divergence | Rationale |
|---|---|---|
| (none yet) | | |

## Updating the vendored copy

When pulling a newer upstream FAISS:

1. Fetch the new release into a scratch location.
2. Recompute the transitive `#include` closure from the seed set listed
   under "What is vendored" above (BFS over `#include "faiss/..."` and
   `#include <faiss/...>`).
3. `rsync -av --delete` only the closure files into this directory.
4. Diff against the prior version of `LICENSE` and any code we've patched
   (see "Patch policy" table).
5. Update the **Version** line above.
