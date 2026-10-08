# Session lifetime

The standalone executable initializes one battle per process. The menu's Restart action reloads the current map's dynamic objects inside that session; it does not construct a new window or DirectX device. Relaunching the executable and restarting a battle exercise different lifetime paths. Repeated full initialization inside a host process is a separate contract and is not established by standalone launch tests.

## Ownership

| Resource | Owner and cleanup |
| --- | --- |
| Standalone window | The 3G layer creates the window and registers its class. `L3GDeinit` destroys the owned window and unregisters its class. |
| Host window | DLL initialization borrows the host window and replaces its window procedure. `L3GDeinit` restores that procedure; ownership remains with the host. |
| DirectX interfaces | Standalone initialization creates `g_D3D` and `g_D3DD`; DLL initialization borrows them without taking a reference. Their release code in `L3GDeinit` is currently disabled. Process exit reclaims standalone resources; explicit device release and in-process device replacement remain unverified. |
| Game heap and configuration | `CGame::Init` creates `g_MatrixHeap` and `g_MatrixData`. `CGame::Deinit` deletes game-owned objects and configuration before deleting the heap. Cursor-name strings use `g_CacheHeap`; `CMatrixConfig::Clear` destroys them and resets their pointer and count before cache teardown. |
| Map | `CGame::Deinit` deletes `g_MatrixMap`. Map cleanup releases objects, effects, navigation, sides, terrain, skins, sound handles, cursor resources and stored device state. Restart rebuilds dynamic content from the current map while retaining the map instance. |
| Forms | Construction registers each `CForm` in a linked list; destruction unregisters it. `FormChange` calls the previous form's `Leave`, changes the active pointer, then calls the new form's `Enter`. Detach an active form before deleting it. The base destructor is not virtual: destroy through the concrete type. |
| Texture and model cache | `CacheInit` creates `g_CacheHeap` and `g_Cache`. Normal shutdown clears game users before clearing the cache; `CacheDeinit` deletes the cache and its heap and resets their pointers. Keep this ordering because map and cursor cleanup use cached resources. |
| Text fonts | `Text::GetFont` holds a function-static collection tied to the first device supplied. Its font wrappers release their interfaces at process shutdown. The embedded font registration is also retained for the process lifetime. There is no explicit cache reset for a replacement device. |
| Timer resolution | Standalone `WinMain` pairs `timeBeginPeriod(1)` with `timeEndPeriod(1)` on normal completion. The DLL loop begins the period and `CGame::SafeFree` ends it. Exception-path balancing is not established by the synthetic teardown cases. |
| Cursor and keyboard | The map owns its software cursor resources. Activation can confine the system cursor to a fullscreen window; deactivation and standalone exit release confinement. Deactivation clears held keys. Verify visible cursor restoration separately from object cleanup. |

## Shutdown and failure boundaries

Normal standalone shutdown ends the timer period, calls `CGame::Deinit`, detaches and destroys the game form, clears the cache, tears down the 3G layer, deletes the cache services and ends the base services. The form's current `Leave` implementation does not use the deleted map; retain that constraint if this ordering changes.

The standalone exception handlers do not all execute that sequence. The engine-exception handler clears cached resources and invokes `L3GDeinit`; the standard and unknown-exception handlers report the failure and return. The final standalone cursor release runs afterward. These paths terminate the process and do not establish reusable in-process state. A synthetic successful `CGame::Deinit` is not evidence that every startup exception reaches it.

The lifecycle executable links production configuration, game teardown, cache, map and form implementations. Its diagnostic overlay substitute records messages and rejects rendering or device transitions. Cases cover repeated cursor cleanup and reload, empty configuration, form enter/leave ordering, constructor unwinding, three form lifetimes and teardown at five synthetic startup stages up to an empty map. Debug also rejects remaining tracked heap allocations. These cases create no window or graphics device and load no game assets.

Run the focused cases with `tools/test.ps1 -Configuration Debug -Filter '^game\.lifecycle\.'`, then repeat with `Release`. Follow [Standalone playtesting](PLAYTESTING.md#repeated-sessions-and-restart) for menu restart, result dialogs, process exit and cursor observations. Natural battle completion, real device recovery and arbitrary allocation failures require separate evidence.
