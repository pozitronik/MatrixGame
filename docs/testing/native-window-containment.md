# Containment for native test windows

`matrixgame_native_window_containment` is a test-only prerequisite for synthetic native session checks. It changes selected imports in the calling test executable, creates an invisible parent and restores the imports when its owner leaves scope. It installs no system hook and changes no other module's import table. It is intended for an x86 executable that links the engine directly; an independently loaded game DLL would need its own containment boundary.

The adapter places windows with the production game/loading classes below the invisible parent, retains child styles during subsequent style changes and shows them without activation. Before showing one of those windows it verifies its parent and child style; a violation terminates the test process. A test that borrows an existing rendering child still owns creation and retirement of that child. Window messages, device operations and game lifecycle code are not replaced by this helper.

For a valid `TME_LEAVE` request on a contained game child, `TrackMouseEvent` returns success without registering the physical desktop pointer. That pointer cannot enter an invisible child and would otherwise cancel posted client gestures. Explicitly posted `WM_MOUSELEAVE` still reaches the normal window procedure. Queries, cancellation, hover, invalid structures and requests for other windows retain the native API path. These fixtures establish declared message-driven input; physical pointer entry/leave timing remains a visible native check.

`ClipCursor` becomes a successful test-local operation. `SetCursor` retains an opaque previous handle, and `ShowCursor` maintains a separate display count for each thread so the engine's balancing loops still terminate. All three imported adapters are mandatory: construction fails before gameplay if any required import is absent. This deliberately excludes desktop clipping, native cursor appearance and actual cursor visibility from the evidence. It does not replace Direct3D device cursor calls or prove display/input behavior on a visible window.

The default constructor policy also requires the named Documents-folder import. That adapter selects the production current-directory fallback so native tests cannot write a game cache into the user's Documents directory. A direct-link fixture that already replaces `PathToOutputFiles` at link time can explicitly pass `require_documents=false`; this does not relax any cursor requirement. Missing imports, a second active instance or an invalid hidden parent reject construction. Partially completed construction restores the imports already changed.

One-button diagnostic message boxes are forwarded to the supplied test reporter and acknowledged without opening a dialog. Other message-box button contracts terminate the test instead of inventing a user's choice. Callers must make reported diagnostics fail their scenario and must install this helper before entering a production path that can display them. It is not an exception-recovery mechanism.

## Validation

Three default tests exercise previous-handle semantics, positive/negative display-count balancing and thread isolation without calling native cursor APIs:

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.session\.cursor_'
.\tools\test.ps1 -Configuration Release -Filter '^game\.session\.cursor_'
```

Three additional Windows checks are opt-in because they create an invisible window and adapt their own executable's imports. They verify that adapted cursor/clipping calls leave the actual cursor and clip rectangle unchanged, that rejection of a missing required Documents import rolls back cleanly before a subsequent successful construction, and that physical leave tracking stays absent while explicitly posted leave still arrives. They do not create a Direct3D device or launch a game.

```powershell
& '.\.tools\cmake-4.4.4-windows-i386\bin\cmake.exe' -S . -B build/mingw-debug-exe -DMATRIXGAME_NATIVE_WINDOW_TESTS=ON
& '.\.tools\cmake-4.4.4-windows-i386\bin\cmake.exe' --build build/mingw-debug-exe --target matrixgame_native_window_tests
.\tools\test.ps1 -Configuration Debug -NoBuild -Filter '^game\.session\.window_'
```

Repeat with Release and its build directory, or use the DLL test configuration and `-DLL`. Registration remains enabled in that build directory until `MATRIXGAME_NATIVE_WINDOW_TESTS=OFF` is configured. All optional checks carry the `native-process` label for the existing CI exclusion. A default CI build still compiles the helper and runs the three pure cursor contracts. Native game journeys require their own validation and do not follow from passing these checks.
