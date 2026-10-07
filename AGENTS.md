# Development guide

MatrixGame is the planetary battle engine from Space Rangers 2, developed as a standalone Windows game. The engine uses C++20 and DirectX 9 with an x86 target. Preserve gameplay and resource formats unless a change explicitly defines a new contract.

## Reading map

Read documents when their subject is relevant rather than loading the entire repository at once.

| Document | Responsibility | When to read |
| --- | --- | --- |
| `README.md` | Project overview and quick start | Setting up or running the game |
| `CONTRIBUTING.md` | Issue taxonomy, triage, branches, PRs, review and releases | Before contribution-tracking or integration work |
| `docs/BUILD_WINDOWS.md` | Toolchains, resources and build options | Building or changing build configuration |
| `docs/TESTING.md` | Regression coverage and playtest procedures | Planning or reporting validation |
| `.github/labels.json` | Canonical label names and descriptions | Classifying work |
| This file | Repository-specific implementation and operating rules | Working on the source |

`CONTRIBUTING.md` is authoritative for the contribution workflow. Existing issues or branches are not a substitute for reading it.

## Working rules

- Keep the work within the requested scope. Separate newly discovered work into a proposed follow-up instead of silently expanding the change.
- Write new code, comments, commits and contribution text in English, without emojis. Preserve existing localization data. Use one line per prose paragraph; do not reflow unrelated text.
- Inspect the checkout before starting. Preserve existing work. If an affected file or branch changes unexpectedly, establish ownership before editing or committing it.
- Reuse the contributor's checkout for serial work. Finish or commit the current task before changing branches. Use separate worktrees only for genuinely concurrent tasks; never modify another contributor's checkout.
- Base normal work on the current integration tip, following `CONTRIBUTING.md`. Refresh remote information before relying on it; do not invent a remote branch that has not been established.
- Obtain explicit maintainer authorization before pushes, PRs, issue updates, reviews, merges, releases or repository-setting changes. Prepare a reviewable result locally first. An instruction to investigate or draft is not permission to publish. An authorization already granted remains valid within its stated scope.
- Before a remote write, confirm the target repository and active account. The private `YOUR.GITHUB.NAME` file, when present, records the account to use; do not copy an identity from another project.
- Report the checks actually performed and any relevant coverage gap. Public text should contain reproducible project facts, not personal paths, session history or setup diaries.
- Use the commit conventions in `CONTRIBUTING.md`. Do not add model attribution, session links or model co-author trailers.

## Workspace hygiene

Tracked changes belong in the current checkout. Tool-private state belongs in the contributor's ignored directory under the primary project root. Do not create task-owned clones or supporting files elsewhere without authorization for a concrete location. Installed toolchains and SDKs are shared prerequisites, not disposable task artifacts.

Use these private subdirectories consistently:

| Directory | Contents |
| --- | --- |
| `artefacts/` | Task-owned logs, captures, temporary scripts, copied sources and draft bodies |
| `worktrees/` | Exceptional concurrent Git worktrees |
| `temp/` | Temporary comparison or validation output |
| `tools/` | Reusable private utilities with a short description of their purpose |
| `issues-tracker/` | Append-only contribution provenance |
| `notes/` | Durable private decisions and handoff notes |

Use the build helper's normal output directories and reuse them. Do not keep an archive of ordinary previous builds. Keep supplied resource files unchanged and untracked; use synthetic fixtures in automated checks. Do not upload game packages, private configuration copies or extracted assets.

Remove disposable files when their purpose ends, and no later than completion or handoff. Remove finished worktrees through Git after preserving their useful commits, then prune their metadata. Clean up task-owned processes and containers if used, without pruning shared caches or prerequisites. Inspect private directories and registered worktrees before reporting completion; identify anything deliberately retained. If removal is blocked by execution policy, report the exact paths for manual cleanup instead of trying another deletion mechanism.

Linux development is outside the current scope. If it is authorized later, use a complete checkout on the native WSL filesystem at an agreed location, not a partial mirror or a build against a Windows-mounted source tree.

## Source layout

- `MatrixGame/src/`: startup, simulation, interface, input, rendering and audio.
- `MatrixGame/src/Logic/`: navigation and AI support.
- `MatrixGame/CFG/`: tracked game configuration.
- `MatrixLib/Base/`: configuration, buffers, storage, allocation and tracing.
- `MatrixLib/3G/`: DirectX resources and rendering support.
- `MatrixLib/Bitmap/`: bitmap and PNG handling.
- `ThirdParty/CMakeLists.txt`: external dependency builds.
- `tools/`: supported setup and build helpers.

## Build and verification

Use the pinned tools described in `docs/BUILD_WINDOWS.md` rather than whichever compiler or CMake happens to be first on PATH.

```powershell
.\tools\setup-toolchain.ps1
.\tools\build.ps1 -Configuration Debug
.\tools\build.ps1 -Configuration Release
.\tools\check-repository.ps1
```

The CMake options select EXE/DLL and cheats; `_DEBUG`, `ASSERT_OFF` and `BUILD_EXE` change compiled behavior. Check the actual configuration and binary before concluding that a path was exercised. Debug and Release use different assertion and allocation instrumentation.

For game or build changes, validate affected Debug and Release standalone configurations. Check MSVC compatibility when compiler behavior, headers, packing, dependencies or ABI are affected. Use focused regressions and relevant playtests; broaden testing only when the risk or a failure warrants it. Document a missing check clearly and use CI for toolchain coverage when possible. Do not launch an interactive game unexpectedly.

Re-derive claims from source and current evidence rather than repeating an issue, comment or old private note. When replacing tooling, enumerate its responsibilities and verify the complete build/run layout, including resources and imported runtime DLLs.

## Coding and review

Standard C++20 facilities and RAII are appropriate when they clarify ownership and preserve behavior. Use proper construction, movement and destruction for nontrivial objects; do not byte-relocate them through `realloc` or `memcpy`. Preserve serialization layouts, host callbacks, packing and object-lifetime contracts. Keep mechanical cleanup separate from functional changes and follow the surrounding style and `.clang-format`.

Review correctness, regression evidence, error handling, bounds, determinism, initialization, teardown and reentrancy where relevant. Consider cost on per-frame, per-unit and pathfinding paths: unnecessary allocation, copying, repeated scans or broad cache invalidation matter more there than in one-time initialization. Treat a demonstrated, material cost with a reasonable fix as blocking; treat speculative optimization as a suggestion.

Review the actual PR diff against its current merge base and identify introduced or worsened behavior. Give concrete fixes or diff-anchored suggestions when practical. Self-review by an author, their subagents or another account they control does not satisfy independent external review. Verify review and CI results for the current revision before merging, as required by `CONTRIBUTING.md`.

## Epic work

Use an integration-only umbrella branch and reviewable leaf PRs as described in `CONTRIBUTING.md`. Respect dependencies, resolve blocking review notes, and verify the combined result before proposing integration into `dev`. Do not treat a merged leaf as proof that the epic is integrated or complete.

Parallel implementation requires task authorization and separate owned worktrees. A read-only self-review needs no worktree and provides no independent approval. Honor any contributor or subagent limit. Do not assume authorization to publish, merge or change another contributor's PR merely because an epic was assigned.

## Private provenance

Keep one `<number>.issue` or `<number>.pr` file per touched GitHub item in the assigned private `issues-tracker/` directory. Start with the title and append dated events when creating an item, first pushing its branch, publishing a review or observing completion. Record actions actually taken; do not reconstruct uncertain history. GitHub is authoritative for current state, and the tracker only records provenance. Durable findings belong in private notes. Check the tracker before claiming prior involvement.

Codex uses `.codex/`; Claude Code uses `.claude/`. Other tools must use an agreed ignored directory before writing private state. Keep all such state under the primary project root, including when working in a temporary worktree. Leave harness-managed memory at its required location, and do not move or clean another tool's state. Private identities, trackers and notes never belong in published documentation.
