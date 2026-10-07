# Development guide

The game targets Windows x86 with C++20 and DirectX 9. Preserve gameplay behavior
and existing data formats unless a change explicitly calls for a new contract.

## Source layout

- `MatrixGame/src/`: startup, simulation, UI, input, rendering and audio.
- `MatrixGame/src/Logic/`: navigation and AI support.
- `MatrixGame/CFG/`: game configuration.
- `MatrixLib/Base/`: configuration, storage, allocation and tracing.
- `MatrixLib/3G/`: DirectX rendering and resources.
- `MatrixLib/Bitmap/`: bitmap and PNG handling.
- `tools/`: setup, builds and repository checks.

## Build and verification

Use the pinned toolchain installed by `tools/setup-toolchain.ps1`.
See `docs/BUILD_WINDOWS.md` and `docs/TESTING.md` for the supported procedures.

```powershell
.\tools\build.ps1 -Configuration Debug
.\tools\build.ps1 -Configuration Release
.\tools\check-repository.ps1
```

Game resources are external to the source tree. Keep supplied resource files
unchanged and untracked; use synthetic fixtures for automated tests.

## Changes

- Keep changes focused and follow the surrounding style and `.clang-format`.
- Reproduce defects and add regression coverage for correctness fixes.
- Use explicit ownership and proper construction/destruction for C++ objects.
  Never byte-relocate objects with nontrivial members.
- Keep mechanical cleanup separate from gameplay changes.
- Build Debug and Release after changing game or build code. Record actual
  verification results, including relevant manual playtests.
- Update documentation when supported behavior or build requirements change.
- Write documentation about the game and its development procedures. Keep personal
  notes, machine-specific details and session history out of published material.
- Obtain maintainer approval before publishing changes or modifying repository settings.
