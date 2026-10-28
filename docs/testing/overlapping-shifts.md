# Overlapping shifts and bounded copies

The memory regressions exercise production sorting, order rotation, spline trimming and optional dead-memory archives with synthetic data. They need no game packages, window, native Direct3D device or interactive playtest.

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^(game|base)\.memory\.'
.\tools\test.ps1 -Configuration Release -Filter '^(game|base)\.memory\.'
```

| Contract | Observation |
| --- | --- |
| `game.memory.object_insertion` | Public visibility enumeration after every insertion, compared with a stable sort of independent keys; overlapping left and right shifts, equal keys and same-frame duplicate rejection |
| `game.memory.surface_insertion` | Real surface loading, group enumeration and sorted traversal; independently sorted color identities, equal keys and duplicate rejection |
| `game.memory.order_rotation` | Empty through full pools, repeated rotations, exact parameter bits and phases, reference counts, adjacent object bytes, unchanged simulation time and RNG count; the old overread requires the sanitizer guard |
| `game.memory.trajectory_trim` | Both spline constructors, retaining seven overlapping segment records and comparing interpolation with the original suffix |
| `game.memory.trajectory_current` | Trimming before the current segment, preserving current position, subsequent distance-based movement and the retained endpoint after each cut |
| `game.memory.trajectory_boundaries` | No-op cuts, exactly one surviving segment and the existing endpoint behavior |
| `base.memory.pointer_compaction`, `base.memory.class_compaction` | Heterogeneous records, middle/head/tail removal, original-address reuse, payload preservation and untouched bytes beyond the occupied suffix |
| `base.memory.pointer_eviction`, `base.memory.class_eviction` | Byte-capacity eviction preserves the two newest large records and removes the oldest |
| `base.memory.pointer_bounds` | Incomplete signatures, invalid recorded sizes, malformed archive headers including a zero-length prefix before another record, and real instrumented frees with positive and oversized payload assertions |

The surface case enables an explicit storage/state-only observer on the existing surrogate device. Production vertex/index buffers and their bindings are exercised; texture-factor writes identify traversal order. All terrain-surface pass counts are zero. Primitive drawing remains forbidden, so this establishes ordering and storage behavior, not rendered pixels or driver compatibility. The observer is disabled by default for every other scenario.

`matrixgame_dead_tracker_tests` links a separately compiled `MatrixLibDeadTrackers`, with `FORCE_ENABLE_MEM_SPY`, `DEAD_PTR_SPY_ENABLE` and `DEAD_CLASS_SPY_ENABLE` enabled consistently in the library and test. The normal library and game do not acquire those definitions. This avoids inferring optional-code coverage from a normal Debug build. The tests inspect existing archive storage to detect writes beyond the newly occupied suffix; no production accessors or object layouts are added. Byte-capacity eviction is covered; exhaustion of the independent 100,000-entry metadata capacity is not claimed.

Ordinary value tests cannot establish the absence of every invalid read. In an AddressSanitizer build, the order test also poisons storage immediately after the order array while calling the production rotation, then restores access before checking state and destroying the robot. This makes the old full-pool read beyond the array observable even though the final copied order overwrites the extra destination bytes. The console prints whether this additional guard was compiled in. Ordinary Debug/Release tests retain their value and ownership checks without requiring sanitizer headers or runtime libraries.

Restoring either old insertion-list `memcpy`, either overlapping spline `memcpy`, the endpoint self-copy, or the old order-rotation extent passes the ordinary Debug/Release value checks. Those results preserve the expected values but do not establish memory safety. The sanitizer controls distinguish the four overlapping copies and the full-pool overread; the endpoint self-copy is separately preserved legacy behavior. Comparing adjacent order storage establishes that it was not modified, while poisoning it detects the invalid read.

For diagnostic omission checks, restore one old operation at a time and require the relevant test to fail. Overlapping `memcpy` operations are diagnosed by AddressSanitizer; restoring either archive's old suffix length also fails the ordinary untouched-tail assertion. Use explicit `/Od /Oi` options after other MSVC target options when demonstrating the original `memcpy` call, and inspect the actual compilation command. Microsoft requires `/Oi` for reliable overlap diagnosis and warns that `/O2` can hide it; see [the AddressSanitizer overlap documentation](https://learn.microsoft.com/en-us/cpp/sanitizers/error-memcpy-param-overlap). Supplemental sanitizer results must identify the compiler, configuration and options separately from supported-toolchain CI.

## Preserved trajectory boundaries

`CTrajectory::CutBefore(1)` currently reduces the count before choosing the source segment, retaining the first segment rather than the last. Its length-cache invalidation is also unreachable. These are independent legacy boundary defects: this correction changes the copy primitive while preserving segment selection and interpolation. The endpoint test explicitly records that behavior rather than silently treating it as a corrected endpoint contract. A future correction needs its own bounded tests and assessment of gameplay effects; these memory regressions do not approve a new trajectory policy.
