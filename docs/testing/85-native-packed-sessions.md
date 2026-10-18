# Packed configuration across native sessions

`game.session.native_packed_lifetimes` extends the [native DLL session fixture](85-native-sessions.md) with a generated `CFG/robots.dat`. It uses the same actual `GetRobotInterface()->Run`, game initialization, Direct3D 9 device, movement, close dispatch and cleanup as the text-configuration case.

The helper loads the fixture's copies of the tracked data and interface text, stores their trees under the production `da` and `if` roots, and writes compressed structured storage. It then moves the loose `CFG/robots` directory out of the lookup path. No supplied package or packed configuration is read or modified. A successful session must load the generated packed trees; it cannot silently fall back to loose configuration.

The packed case runs two successful battles using the same borrowed host window and device. Each battle displays the introduction, moves a robot through the real loop, closes normally and returns zero. The second battle must initialize fresh state and move again after the first battle's cleanup. Both configurations check global aliases, form/object lists, Debug allocation tracking, unchanged borrowed-device references, window-procedure restoration and continued host rendering after each return.

The text case separately tests a missing-interface initialization failure between its successful battles. `game.session.native_packed_header_failure` tests the earlier failure boundary: after the first successful packed session, it replaces the generated storage with a four-byte invalid tag. Production must return 100 before any progress callback, preserve the caller's output state, report `Invalid packed configuration: cfg\robots.dat`, and retire all owned state. Restoring the valid packed file must allow another successful battle and fresh movement. This catches a stale pneumatic animation buffer left by the preceding successful cleanup.

`game.session.pneumatic_retirement` also calls `CMatrixRobot::DestroyPneumaticData` again after each successful packed battle. This explicit cleanup boundary must be harmless, and the following real initialization must rebuild the animation data. Before the pointer-retirement correction, this case crashes Debug; Release can appear to pass despite the double free. Both configurations remain required.

Enable `MATRIXGAME_NATIVE_SESSION_TESTS` in the existing DLL build as described in the native-session guide, then run:

```powershell
.\tools\test.ps1 -Configuration Debug -DLL -NoBuild -Filter '^game.session.(native_(run|packed)_lifetimes|native_packed_header_failure|pneumatic_retirement)$'
.\tools\test.ps1 -Configuration Release -DLL -NoBuild -Filter '^game.session.(native_(run|packed)_lifetimes|native_packed_header_failure|pneumatic_retirement)$'
```

These opt-in tests require a local Direct3D 9 HAL device and run inside an invisible borrowed child window. Packed/text equivalence is scoped to the declared introduction, movement and cleanup observations. It does not establish every original packed resource, language configuration, host implementation or visible presentation profile. The full baseline and remaining native validation retain their existing acceptance requirements.

`game.navigation.pneumatic_cleanup` covers the allocation/retirement boundary without a native device. It builds the real gait catalog from the synthetic pneumatic chassis mesh, checks pointer retirement, repeats cleanup, and repeats initialization. The existing five-chassis journeys continue to check pneumatic movement and gait timing under both RNG modes and 10/16 ms steps.
