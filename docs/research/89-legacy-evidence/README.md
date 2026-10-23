# Legacy implementation ideas: evidence and disposition

This investigation follows the [legacy inventory](../88-legacy-code-inventory/README.md). Its source reference is `fad309b43e6051862af732a912ea5f7bb6c7f113`; the earlier inventory examined `540251d`. Inventory IDs remain stable so later work can trace a conclusion back to its original location and history. This report changes neither production behavior nor the accepted behavior reference.

## Findings

Several old notes describe working code or deliberate compatibility boundaries. The build-stack `STUB` comments mark calls into the interface, the vector loader already reconstructs triangles, and mutable sound callback arguments belong to the original host interface. Removing the comments is different from removing those operations.

Disabled implementations are useful historical evidence, but their presence does not establish a supported alternative. The old robot loader is not a saved-battle format; the package writer is an unfinished archive-authoring implementation; the road-network builder belongs to content creation. Retain their intent before considering deletion, and do not revive them as part of cleanup.

The original inventory's defect and reachability claims need individual reconciliation. A defect reproduced by an opt-in test remains unresolved until a correction or explicit disposition is reviewed. A comment reporting a crash is not itself a reproduction. Conversely, the absence of a current production caller does not prove that deleting a public library method preserves the intended library contract.

## Scope and evidence rules

The [disposition table](disposition.md) covers the inventory's 58 groups. It separates current source findings, existing executable evidence, completed corrections, and bounded investigations that still require an experiment or product decision. Historical file positions belong to the original inventory; current evidence names functions and files at the revision above. Detailed evidence is split into [defects and behavior notes](defects.md), [historical gameplay alternatives](gameplay.md), and [build variants and deletion boundaries](builds.md). The supplemental [player-command finding](80-command-findings.md) and [cheat creation/lifetime findings](80-cheat-findings.md) record their own source revisions and pending #80 dispositions.

Four corrections materially change the old cleanup advice. Package enumeration is now guarded and used by map discovery; the horizontal place-bound defect has a reviewed compatibility option; current selection fixtures consume methods previously listed as unused; and the prepared native-session tests use road-network serialization. The accepted outcome-testing scope also includes trace-only result paths. Preserve these consumers when reviewing removal candidates, even where their new tests await integration.

No original resources are distributed with this report. Native frame measurements, audible acceptance and original-host behavior are not inferred from synthetic fixtures. Results from unmerged branches are not credited to the integrated source reference. This investigation does not authorize removal of code, structural refactoring, or a gameplay correction.

## Timing: smoothing and frame limiting are separate decisions

`L3GRun` in `MatrixLib/3G/3g.cpp` measures elapsed time, applies the diagnostic four-times multiplier, converts to whole milliseconds and caps the result at 100 ms before advancing reminders and the active form. The commented 16-frame averaging block is inactive. `CMatrixConfig::ReadParams` still accepts `MaxFps`, but the engine does not read `g_MaxFPS` to limit rendering. These remain current findings, not merely statements copied from the old inventory.

Restoring the historical average would change the time values seen by movement, weapons, effects and side scheduling. Restoring a render limiter would change frame cadence and consequently the raw tick schedule. Neither is a mechanical optimization. Investigate them separately: otherwise a smoother picture could conceal a simulation change, or a lower frame rate could be mistaken for a less expensive frame.

The existing [measurement runner](../../MEASUREMENTS.md) measures three bounded synthetic workloads: crowd route planning, selection refresh and side scheduling. It does not execute the complete render loop. Its results cannot demonstrate that smoothing improves native frame pacing or that a texture-state cache reduces GPU work. The accepted [time investigation](../76-battle-determinism/README.md) supplies the comparison boundary for timing-dependent behavior.

The bounded timing experiment is: use one recorded battle, resolution, renderer settings and input schedule; collect three fresh-process native frame traces after the documented warm-up; record the actual sequence of simulation deltas as well as present intervals; compare current behavior with smoothing-only and limiter-only experimental builds. Report median, p90 and p99 present intervals, capped and zero simulation steps, accumulated simulation time and first gameplay-trace divergence. An improvement claim requires comparable work and repeated evidence. Retain the current behavior until the maintainer decides whether a measured alternative belongs in product scope. No native timing experiment is claimed here.

## Rendering hypotheses

M7 contains several different notes: avoiding redundant texture/index setup, retaining projected-shadow geometry across object changes, light quantization for stencil regeneration, and adding building projected shadows. The first two are cost hypotheses; the last adds visible behavior. Current `CMatrixMapObject::Init` still invalidates projected-shadow resources, and `CMatrixBuilding::RNeed` still contains the disabled projected-geometry block. The separate shadow controls introduced after the original inventory do not make those implementations complete. In contrast, `CVOShadowStencil::Build` already multiplies normalized light components by 255 and caches their quantized values immediately below its old TODO; that comment does not establish an unimplemented optimization.

For state caching, measure calls and changed values separately, then compare frame captures with animated, transparent and side-colored objects. For projected geometry, count rebuilds while an object changes model/state and verify bounds, animation and shadow ownership. For quantized light, compare regeneration count and visible shadow error at threshold crossings. Reuse the native measurement procedure; synthetic side-tick timings are not evidence for these hypotheses. The optimization work package owns any implementation after baseline acceptance. Building projected shadows need an explicit visual contract before implementation.

## Mechanical modernization and compatibility

M12 groups unrelated changes. Replacing `starts_with` has a small equivalence boundary, including empty prefixes. Replacing `codecvt` requires preserving UTF-8/UTF-16 and malformed-input behavior, not merely compiling on a newer standard library. Replacing printf-style formatting must preserve callers' format interpretation and failure behavior while addressing its independently identified bounds problem. Renaming `Takt` or moving namespace declarations affects a large call surface without improving gameplay evidence; perform such work separately from functional changes.

The `rfind` expressions in `FileExistA` and `FileExistW` use unsigned `npos + 1` to obtain zero when no extension separator exists. That expression is unusual but does not establish an overflow defect. Test the path-selection contract before changing its representation. The `CBlockParUnit` assignment note concerns the destination's type and lifetime; it is not permission to replace ownership with byte copying.

M14's include from `MatrixLib/3G/VectorObject.cpp` into `MatrixGame/src/MatrixSampleStateManager.hpp` is an actual dependency inversion to consider in the presentation-boundary work. Moving the include alone does not remove the dependency. The baseline gate still applies to relocating responsibilities.

## Cleanup constraints

The cleanup candidate list describes exact kinds of removable material and protected live boundaries. It is a plan for a separately reviewed deletion change, not approval to remove every function in the old unreferenced-symbol list. That list was derived from four MinGW configurations at an earlier revision; current tests, host consumers and compiler-generated references must be checked again.

Keep explanatory tables, warnings and reachable labels. In particular, the `if (0)` animation block in `MatrixRobot.cpp` contains a `goto` target; treating all constant-false blocks as unreachable would remove live control flow. Keep active Debug, cheats, EXE/DLL and compiler branches. A retired exporter guard may be removable while its non-exporter body remains required.

Do not replace MatrixLib's explicit CMake source list with globbing merely because M16 suggests it. Removing an obsolete file and changing source discovery are separate changes with different validation. An explicit list makes the reviewed compilation boundary visible.

## Remaining decisions and completion boundary

Research conclusions may retain a bounded unresolved question; they must name the experiment, acceptance criteria, owner and compatibility risk. New implementation issues require maintainer authorization. Existing work packages provide ownership for overlapping ideas; this report does not duplicate them as a second implementation backlog.

The behavior baseline, native-execution ledger and exact-revision acceptance remain separate obligations. This report neither closes those obligations nor accepts an optimization or historical feature on the strength of comments alone.
