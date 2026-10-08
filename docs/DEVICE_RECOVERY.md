# DirectX device recovery

MatrixGame uses DirectX 9. The game thread checks `TestCooperativeLevel` before advancing a battle. Recovery releases map resources once, resets a resettable device, then recreates resources before drawing resumes. A device that remains lost is checked again after a 50 ms wait; an inactive window waits for Windows messages or a 50 ms timeout instead of repeatedly yielding.

| Device result | Behavior |
| --- | --- |
| `D3D_OK` | Draw normally; recreate resources if focus loss previously released them. |
| `D3DERR_DEVICELOST` | Release ready resources once, skip simulation/drawing and wait before another check. No reset is attempted yet. |
| `D3DERR_DEVICENOTRESET` | Release ready resources, attempt reset, then restore resources after success. A reset returning `D3DERR_DEVICELOST` waits before retrying. |
| Other status or reset failure | Stop through an engine diagnostic containing the failing operation and hexadecimal HRESULT. Permanent failures do not retry indefinitely. |
| `Present` returns `D3DERR_DEVICELOST` | Allow the next tick to handle loss. Other presentation failures produce the same diagnostic in Debug and Release. |

The diagnostic font and cached UI fonts receive `OnLostDevice` before reset and `OnResetDevice` before text drawing resumes. Font names, faces and sizes retain their existing values. Notifications do not construct unused UI fonts. Font-notification failures name the affected font, operation and HRESULT. Microsoft documents these callbacks as necessary to release font state blocks and video-memory references before reset and restore them afterward: [OnLostDevice](https://learn.microsoft.com/en-us/windows/win32/direct3d9/id3dxfont--onlostdevice), [OnResetDevice](https://learn.microsoft.com/en-us/windows/win32/direct3d9/id3dxfont--onresetdevice).

Map teardown also releases side textures, effect and water buffers, minimap resources, geometry buffers and default-pool textures. Managed textures retain their existing DirectX lifetime. Device ownership and replacement are separate from reset: cached fonts remain tied to their original device for the process lifetime. This change does not establish reuse against a newly created device or change the DLL host's ownership.

## Supported transitions and limits

The existing windowed UI has a fixed client size. Focus loss normally minimizes it, clears held keys, releases cursor confinement and retires video resources; returning restores the window and recreates those resources. The window style does not offer border resizing, and the input handlers provide no Alt+Enter mode switch. Do not infer arbitrary resize or live window/fullscreen switching from focus recovery.

Startup configuration can select fullscreen; video-parameter application also considers desktop format and resolution. Exclusive-fullscreen focus changes, monitor changes, sleep/resume and physical driver resets require explicit hardware playtests. A windowed reset test does not establish them. Keep unsupported or untested transitions visible in validation reports rather than treating every GPU as covered.

## Automated and native checks

`game.device.*` exercises production recovery decisions with synthetic HRESULTs and callback recorders: operational frames, release/reset/restore order, repeated loss, transient reset loss, permanent failures, presentation loss and the wait interval. `game.font.*` exercises production font-cache ownership, the existing five definitions, repeated notifications, missing fonts, partial creation cleanup and failure diagnostics through synthetic COM fonts. These registered cases create no device or window and use no game assets.

The explicit native check draws with real D3DX fonts, resets a hidden windowed device and draws again three times. It prints the adapter and driver version. It needs a working DirectX device and is deliberately excluded from CTest and CI:

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.(device|font)\.'
.\build\mingw-debug-exe\tests\matrixgame_font_tests.exe manual.font.native_reset
.\tools\test.ps1 -Configuration Release -Filter '^game\.(device|font)\.'
.\build\mingw-release-exe\tests\matrixgame_font_tests.exe manual.font.native_reset
```

The native console executable does not embed the game's Rangers font resource, so it reports that missing registration and uses the system's font substitution. It validates reset and drawing lifetimes; it does not validate Rangers glyphs, the complete game renderer or a physical device-loss event. Failure returns a nonzero process status and never waits for a desktop dialog.

For visible focus checks, follow [Standalone playtesting](PLAYTESTING.md#focus-and-recovery). Record the compiler, Debug/Release configuration, cheats setting, configuration mode, client resolution, adapter, driver, transitions and exit status.
