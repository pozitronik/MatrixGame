# Session lifetime

The standalone executable initializes one battle per process. The menu's Restart action reloads the current map's dynamic objects inside that session; it does not construct a new window or DirectX device. Relaunching the executable and restarting a battle exercise different lifetime paths. Repeated full initialization inside a host process is a separate contract and is not established by standalone launch tests.

## Ownership

| Resource | Owner and cleanup |
| --- | --- |
| Standalone window | The 3G layer creates the window and registers its class. `L3GDeinit` destroys the owned window and unregisters its class. |
| Host window | DLL initialization borrows the host window and replaces its window procedure. `L3GDeinit` restores that procedure; ownership remains with the host. |
| DirectX interfaces | Standalone initialization owns the initial references from `Direct3DCreate9` and `CreateDevice`. `L3GDeinit` releases the device reference before the Direct3D reference and resets both global pointers. DLL initialization borrows both interfaces without taking a reference; teardown disconnects those pointers without releasing host references. Other resources can retain their own device references. |
| Game heap and configuration | `CGame::Init` creates `g_MatrixHeap` and `g_MatrixData`. `CGame::Deinit` deletes game-owned objects and configuration before deleting the heap object. `CHeap` supplies allocation helpers and Debug tracking, not an arena that frees surviving allocations when its object is deleted. Cursor-name strings use `g_CacheHeap`; `CMatrixConfig::Clear` destroys them and resets their pointer and count before cache teardown. |
| Map | `CGame::Deinit` deletes `g_MatrixMap`. Map cleanup releases objects, effects, navigation, sides, terrain, skins, sound handles, cursor resources and stored device state. Restart rebuilds dynamic content from the current map while retaining the map instance. |
| Forms | Construction registers each `CForm` in a linked list; destruction unregisters it. `FormChange` calls the previous form's `Leave`, changes the active pointer, then calls the new form's `Enter`. Detach an active form before deleting it. The base destructor is not virtual: destroy through the concrete type. |
| Texture and model cache | `CacheInit` creates `g_CacheHeap` and `g_Cache`. Normal shutdown clears game users before clearing the cache; `CacheDeinit` deletes the cache and its heap and resets their pointers. Keep this ordering because map and cursor cleanup use cached resources. |
| Text fonts | `Text::GetFont` holds a function-static collection tied to the first device supplied. Its font wrappers release their interfaces at process shutdown. The embedded font registration is also retained for the process lifetime. There is no explicit cache reset for a replacement device. |
| Timer resolution | Standalone `WinMain` records a successful `timeBeginPeriod(1)` and pairs it with `timeEndPeriod(1)` on normal completion or a caught exception. The DLL loop begins the period and `CGame::SafeFree` ends it; the standalone ownership state does not change that host contract. |
| Cursor and keyboard | The map owns its software cursor resources. Activation can confine the system cursor to a fullscreen window; deactivation and standalone exit release confinement. Deactivation clears held keys. Verify visible cursor restoration separately from object cleanup. |

## Shutdown and failure boundaries

Standalone shutdown ends any acquired timer period, detaches and destroys the game form, calls `CGame::Deinit`, clears the cache, tears down the 3G layer, deletes the cache services and ends the base services. Form detachment happens while game state is still available. Cursor confinement is released after cleanup.

Engine, standard and unknown C++ exceptions release cursor confinement before displaying the diagnostic, then run the same standalone shutdown sequence after dismissal. Every cleanup stage is attempted even if an earlier stage throws; a cleanup exception makes the process return a failure status. `CGame::SafeFree` also tolerates a missing cache. Cleanup is not a recovery mechanism for native faults or corruption, and it does not establish reusable game state after an arbitrary partially completed allocation.

The lifecycle executable links production configuration, standalone teardown, cache, map, graphics and form implementations. Its diagnostic overlay substitute records messages and rejects rendering or device transitions. Registered cases cover repeated cursor cleanup and reload, empty configuration, form enter/leave ordering, constructor unwinding, three form lifetimes, partial startup up to an empty map, failure before cache creation, continued cleanup after C++ exceptions, owned versus borrowed reference release, and graphics configuration rejection before window creation. Debug also rejects remaining tracked heap allocations. These cases create no window or graphics device and load no game assets.

An opt-in native graphics case creates three standalone windows/devices, checks that teardown releases their initial COM references and destroys their windows, then attaches to a synthetic host window/device and checks that its procedure, window and reference counts survive teardown. It also calls teardown repeatedly. Run it on a Windows desktop with DirectX 9 after building the tests:

```powershell
.\build\mingw-debug-exe\tests\matrixgame_lifecycle_tests.exe manual.lifecycle.native_graphics
.\build\mingw-release-exe\tests\matrixgame_lifecycle_tests.exe manual.lifecycle.native_graphics
```

This case is excluded from CTest because it requires a graphics device. It loads no game resources or fonts. Successful native graphics cleanup does not establish replacement of a device with live font, map or texture resources.

Run the focused cases with `tools/test.ps1 -Configuration Debug -Filter '^game\.lifecycle\.'`, then repeat with `Release`. Follow [Standalone playtesting](PLAYTESTING.md#repeated-sessions-and-restart) for menu restart, result dialogs, process exit and cursor observations. Natural battle completion, real device recovery and arbitrary allocation failures require separate evidence.
