# Demo and cheat-toggle session lifetimes

These opt-in checks extend the [synthetic native DLL session](85-native-sessions.md) for O8/O9 and AI-03. They call the production initializer, form, scheduler, renderer and cleanup through `GetRobotInterface()->m_Run`. The fixture supplies a hidden borrowed window, a real Direct3D 9 HAL device and generated resources. It neither drives desktop input nor replaces the production session implementation.

## Checked transitions

| Case | First session | Following sessions |
| --- | --- | --- |
| `game.session.native_cheat_lifetimes` | Dismiss the introduction; type NEED4SPEED and KEEPALIVE through the form, releasing every key; check both flags against the compiled cheat option; issue Move, observe physical movement and close normally | Fail ordinary initialization after map loading begins; restore the missing interface; initialize an ordinary battle, dismiss its introduction, accept a fresh Move and close |
| `game.session.native_demo_lifetimes` | Initialize `synthetic-DeMo.map`; require automatic control, full-auto and flying camera together, no introduction and finite camera state; advance at least 250 map milliseconds and press/release Escape | The same ordinary failure and successful reentry sequence |
| `game.session.native_demo_win` | Initialize the same demo; kill the sole opposing robot with production instant-death damage after at least 250 map milliseconds; observe the underlying pending win and let the normal status scheduler terminate without a result dialog | The same ordinary failure and successful reentry sequence |
| `game.session.native_demo_loss` | Initialize the same demo; kill the sole player robot through that damage receiver; observe eliminated player status without a win and let the normal status scheduler terminate without a result dialog | The same ordinary failure and successful reentry sequence |

Each case also has a packed counterpart: `native_cheat_packed_lifetimes`, `native_demo_packed_lifetimes`, `native_demo_packed_win` and `native_demo_packed_loss`, with the same `game.session.` prefix. These load generated `CFG/robots.dat` with the loose configuration parked; their intervening failure corrupts the packed header, before the first progress or tick callback, then restores it for successful ordinary reentry. The text cases instead fail after terrain loading has begun. This covers two distinct cleanup boundaries rather than treating packed generation as evidence of packed execution.

The mixed-case filename checks the initializer's case-insensitive demo-name rule. All three demo flags must be absent in each ordinary successful session; the introduction must return. Demo Run returns 1, an ordinary close returns 0 and the deliberately failed initialization returns 100. The failure must preserve the caller's result buffer and record the missing-interface diagnostic. Each lifetime retains the existing checks for retired map/configuration/cache/form/graphics aliases, object lists, text-buffer ownership, the restored borrowed window procedure, the device reference count and a usable device after return. Debug also checks the engine allocation tracker.

At the first progress callback of each initialization that reaches it, the test requires four-speed and KEEPALIVE to be clear. Only a cheat-enabled build observes their reset after the typed words set them; without cheats, the same words must remain inert and neither flag becomes set. An invalid packed header reaches no callback. The initializer resets application flags before the callback; this is a fresh-initialization assertion, not a claim that map Restart resets those flags. The later ordinary battle also checks them after initialization. Key release is checked immediately after each typed key. The separate held-input case remains responsible for closing a session with a key still held.

## Running

Use the pinned Windows tools and native-session prerequisites from the [host fixture guide](85-native-sessions.md#running). Native registration is disabled by default; enabling it explicitly selects real device execution rather than a successful skip on an unavailable adapter.

```powershell
.\tools\build.ps1 -Configuration Debug -DLL -WithoutResources -TestsOnly
& '.\.tools\cmake-4.4.4-windows-i386\bin\cmake.exe' -S . -B build/mingw-debug-dll -DMATRIXGAME_NATIVE_SESSION_TESTS=ON
.\tools\test.ps1 -Configuration Debug -DLL -Filter '^game.session.native_(cheat_(packed_)?lifetimes|demo_(packed_)?(lifetimes|win|loss))$'
```

Repeat with Release and `build/mingw-release-dll`. Repeat both configurations with `-Cheats` on the build and test helpers; the engine and fixture expectations must use the same option. Restore the ordinary configuration by rebuilding without `-Cheats` afterwards. To run all host-lifetime controls, use `'^game.session.'` as the filter. The native option persists in the chosen build directory until explicitly configured OFF.

Each CTest process has a 30-second deadline; the host additionally rejects 5,000 callbacks without completing its sequence. Output identifies the configuration profile, lifetime, result, callback count, movement, demo mode and whether typed toggles were observed. Normal completion removes the generated deployment. Debug allocation failures can still display the engine's leak reporter until the process timeout; the native-session guide describes this boundary and how interrupted runs can leave diagnostic directories.

Two negative controls distinguish startup and reset from merely entering the host callback. Preserving four-speed/KEEPALIVE through `static_init` makes both cheat-enabled text/packed toggle cases fail during later initialization. Omitting FULLAUTO from demo startup makes both text/packed Escape profiles fail. Production source is restored after each experiment; the mutations are not alternate game modes or registered passing cases.

## Scope and remaining evidence

The host contract selects ParkMiller and a time-based seed inside Run. The fixture records the seed interval and uses bounded invariants; it does not inject a deterministic seed, map timestep or clock. The native loop is not a 10/16 ms synthetic schedule. Its speed-cap arithmetic and inactivity rules retain the separate controlled-loop checks.

The demo contains two unarmed robots and no bases. It establishes filename-driven startup, the flying-camera flag with finite camera state, result/Escape continuation and ordinary reentry. It does not distinguish flying-camera execution from a strategy camera that remains finite, or establish eligible reinforcements, camera fight switching or autonomous combat during loaded demo startup; those require their own integrated scene. Direct instant-death damage triggers outcome scheduling without claiming a weapon/collision journey. Finite camera values do not establish exact angle trajectories or visible image quality. Original-host DLL loading, original demo resources, rendered/audible presentation and the remaining native procedures retain their separate baseline obligations.
