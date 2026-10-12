# External game automation: protocol proposal

This is the design report for [external command/observation research](https://github.com/pozitronik/MatrixGame/issues/99). It proposes an opt-in local API for the standalone Windows executable, implemented after the whole-game behavior baseline. It is not an available game feature. The inspected source reference is `4f91fcd1ecf9cd9db0ff985b775bcaf6087adc68`.

The API should let a client obtain structured observations, issue ordinary player commands and await meaningful state changes. It must not require repeated screenshot parsing. An AI controller can later use the same client as a regression runner, but this deliverable includes no model provider, assistant policy or playable AI.

## Recommendation

Use one local Windows named-pipe connection with versioned UTF-8 JSON messages. Keep transport/encoding outside engine logic. The engine thread validates and dispatches queued commands through the same command boundary as human input, then produces bounded value snapshots and events. Use an exclusive control lease so a client and a human cannot silently issue interleaved orders. A human gameplay action revokes that lease by default.

Expose two explicitly different launch profiles:

- **Player:** ordinary realtime play, existing player information and commands, no state edits or time stepping. Connecting does not change pause, focus, keep-alive or speed flags. The existing inactive-window policy still stops simulation; a foreground console client cannot assume the game continues in the background.
- **Diagnostic:** a deliberately launched test session with privileged observations and the harness's bounded advancement capability, when available. It reports that profile in every handshake and trace. Fixture setup and destruction actions belong to a separate test capability, never to the player command set.

Neither profile should activate in the original host DLL. Do not extend its callback ABI or borrow its host window/device for an unrequested control server.

## Why this transport

| Option | Benefits | Costs and limits |
| --- | --- | --- |
| Named pipe | Local Windows IPC, OS access control, separate client process, usable from native code and scripting clients | Requires careful framing, cancellation and a small Windows client wrapper |
| Loopback TCP with JSON | Familiar to many languages and tools | Needs port discovery and application authentication; loopback alone is not a user boundary |
| HTTP/WebSocket server | Convenient for browser dashboards | Adds server/protocol surface before a dashboard exists; no current requirement justifies it |
| Shared memory | Potentially low copying overhead | Synchronization, lifecycle and versioning complexity; no measurement demonstrates a need |
| Standard input/output or command files | Simple batch experiments | Poor fit for attaching to the current GUI executable, cancellation and live events; useful for an offline runner later |

The transport choice is a recommendation, not a claim from a benchmark. Start with explicit limits and measure command latency/snapshot cost before adding faster encodings or more clients.

Windows supports overlapped pipe operations and an explicit remote-client rejection flag. Use byte mode with application framing, `FILE_FLAG_OVERLAPPED`, `PIPE_REJECT_REMOTE_CLIENTS` and `FILE_FLAG_FIRST_PIPE_INSTANCE` with `nMaxInstances = 1`; do not use `PIPE_NOWAIT` as the asynchronous mechanism. These choices follow the [CreateNamedPipe contract](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createnamedpipea) and [Microsoft's overlapped I/O example](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-server-using-overlapped-i-o).

Create a non-inheritable endpoint with an explicit DACL limited to the launching logon session and explicit client rights `FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE`. Clients request those same rights, not `FILE_GENERIC_WRITE`, whose inclusion of `FILE_CREATE_PIPE_INSTANCE` would permit creating a server instance. Do not use the default named-pipe descriptor as a same-user-only policy. The endpoint name should identify a unique process/session; publish it only through the opt-in launch output or a user-protected discovery file, and remove that file at shutdown. The relevant OS access rules are documented in [Named Pipe Security and Access Rights](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights). These are bounded local-control requirements, not a remote administration service.

## Existing command routes and equivalence

[`CFormMatrixGame::Keyboard` and `MouseKey`](../../../MatrixGame/src/MatrixFormGame.cpp) handle input context before reaching the side. [`CMatrixSideUnit::OnLButtonDown` and `OnRButtonDown`](../../../MatrixGame/src/MatrixSide.cpp) perform target/type checks, minimap conversion and formation-coordinate adjustment. `SelGroupToLogicGroup` creates/reuses a logic-group slot, and `PGOrder*` stores intent and sometimes immediately evaluates it. Directly writing robot coordinates, group orders or resources would bypass that behavior.

For example, explicit move starts with the `M` preorder; target confirmation converts world coordinates through `GLOBAL_SCALE_MOVE` and subtracts half the robot footprint. The move handlers also contain flyer-offset branches, but current player selection traces exclude flyers and `SelGroupToLogicGroup` assigns only live robots. The only current `OrderFlyer` caller is building reinforcement. Do not advertise direct player flyer control or mixed robot/flyer groups; retain dormant branch behavior if a future reviewed selection route makes it reachable. Those branches use offsets around `m_TraceStopPos`, even when the robot target was converted from the minimap.

A context click captures any live non-owned building, neutral included; otherwise it attacks live non-owned units or `IsSpecial()` objects; remaining targets become movement. Preserve this precedence and the existing minimap target conversion. `move`, `attack` and `context_action` are different commands.

An auto-order key whose corresponding interface flag is set clears all three auto flags and issues `PGOrderStop`; when clear, it selects that auto mode. `ShowOrderState` recomputes flags from the whole selection, so even the first press after selecting a mixed group can stop it. The API reproduces the keyboard toggle, not an idempotent setting. Its `stop` operation reproduces the stop key: `PGOrderStop` followed by `SelectedGroupBreakOrders`. The stop button omits the latter call and is not the reference route. [The fixture investigation](https://github.com/pozitronik/MatrixGame/issues/77) covers real keyboard acceptance, replacement and toggle-to-stop on a bounded synthetic fixture.

Commands act on `PLAYER_SIDE` through `GetPlayerSide()` only. Before any order route calling `SelGroupToLogicGroup`, reject a null/empty or ineligible selection with `invalid_context`; that function dereferences `GetCurGroup()` without a null check. This API precondition prevents invalid entry, without modifying native gameplay.

Implementation should extract only the required shared dispatch boundary, retaining the existing guards, conversion, selection/group changes, sound/RNG calls and timing. Keep paired tests: native form/UI route versus structured command on equal fixtures, then compare snapshots and RNG evidence. Do not retain two hand-copied implementations of validation. The production extraction belongs after the baseline unless separately approved as a minimal necessary test seam.

### RNG and execution inputs for equivalence

Selection is itself a random consumer: `CMatrixSideUnit::Select` chooses a voice through `g_MatrixMap->Rnd(0, 6)` even if audio is unavailable. The non-positional `CSound::Play(ESound, ...)` overload draws volume and pan only when `SoundBridge::available()`. `PGOrderMoveTo` and `PGOrderPatrol` draw chassis acknowledgements when their sound block exists, or `IRND(2)` for repeated orders. `PGOrderRepair` draws `IRND(2)` in either acknowledgement branch. Repeated attacks also have a conditional `IRND(2)` under the attack-sound flag. An Automation command must have the same acknowledgement policy as its human reference; treating every non-human source as silent breaks that equivalence.

Paired tests fix the seed/mode before map construction, effective difficulty/configuration and sound mappings, audio availability, command order, frame schedule, camera, render/effect policy and floating-point control word. Record resource identities without publishing assets. Direct3D creates the standalone device without `D3DCREATE_FPU_PRESERVE`; device-free tests cannot assume the same x87 precision. Compare headless and native profiles only under an explicitly justified contract from the time/RNG work; do not adopt an unreviewed generic tolerance. The API reports these execution inputs in diagnostic manifests, while player observations expose only the runtime mode fields below.

### Proposed command catalog

All player commands operate on the session's current selection, like the existing controls. Selection is a separate acknowledged command; subsequent commands carry the resulting selection revision to detect intervening changes. A request cannot silently reselect units by passing an arbitrary actor list with every order.

| API operation | Shared production route | Validation and scope |
| --- | --- | --- |
| `select`, `selection.add`, `selection.clear` | `Select`, `SideSelectionCallBack`/group operations as appropriate to the selection gesture | Owned/selectable entity types, current mode and existing order/size rules. An entity-ID selector is enabled only after equivalence to a permitted selection gesture is proven; no unlimited group creation |
| `group.assign`, `group.recall` | Form number-group handling | Existing slot/modifier behavior, membership and removed-entity handling. Recall is one number-key press through the shared handler, including `DOUBLEPRESS_TIME_DELTA` and camera centering on a rapid repeat; it is not an idempotent selection operation. Preserve the shared key/time history and report the camera effect |
| `move`, `patrol` | Explicit preorder confirmation, then `PGOrderMoveTo`/`PGOrderPatrol` | Point coordinates, bounds, current selection, command mode and footprint conversion; flyers are not an advertised selectable type |
| `context_action` | `OnRButtonDown` semantic target dispatch | Preserve capture/attack/move precedence and minimap-versus-scene distinction |
| `attack`, `capture`, `repair`, `bomb` | Matching preorder route and `PGOrder*` | Target lifetime/type/ownership, weapon capability and cancellation rules; object and ground targets differ |
| `stop`, `auto_attack`, `auto_capture`, `auto_defend` | Form keyboard order handler | Stop includes selected-unit order breaking; auto operations use the selection-wide flags and toggle-to-stop rule above |
| `camera` | Camera's existing movement/zoom/rotation actions | Needed to expose/select objects through legitimate views; bounded parameters, no visibility bypass |
| `observe`, `subscribe`, `command_status`, `control.acquire`, `control.release` | API-owned read/control bookkeeping | No simulation mutation except acquiring/releasing future command authority |

The implementation leaf should deliver and prove a useful strategic subset together: selection, move, stop, context/explicit attack, observation, control and disconnect. Add the other catalog commands within that leaf only as their production equivalence is demonstrated; advertise a precise capability list and reject unsupported operations. If the remaining command scope needs additional leaves, propose them before claiming the API covers the entire game.

Construction/queue actions require the actual `CConstructor::RemoteBuild` and interface checks, not `ProduceRobot` with free resources. Direct-control held movement/fire needs lifecycle-safe press/release semantics. Both are cataloged extensions, not assumed MVP capabilities. Dialog confirmation, restart, surrender and application exit likewise need explicit reviewed operations; arbitrary console commands, file loading, memory writes, teleportation, resource grants and forced winners are excluded. The testing harness can exercise broader gameplay internally without waiting for all external commands.

## Information available to clients

The player-profile contact baseline is the global minimap, not per-side fog of war. [`CMinimap::Draw`](../../../MatrixGame/src/MatrixMinimap.cpp) iterates the logic list and draws every live robot/building/cannon and each flyer (without a flyer live-state check), with type and side-colored icons. It has no side-knowledge filter. Scene `OBJECT_STATE_INVISIBLE`/`TRACE_INVISIBLE` flags are not a per-side knowledge model. `DrawRadar` uses a local radius. [`CMatrixMap::Trace`](../../../MatrixGame/src/MatrixMapTrace.cpp), trace-invisibility flags and the scene's hover path govern different information. [`CIFaceList::CreatePersonal`](../../../MatrixGame/src/Interface/CInterface.cpp) builds selected-unit presentation. These source facts do not prove that every internal enemy field is player-visible.

Use an explicit field provenance table, verified with the corresponding native surface before enabling the field:

| Observation | Player profile proposal | Diagnostic profile |
| --- | --- | --- |
| Session, mode, selected IDs, own resource totals, exposed queue/selection UI state | Semantic values already displayed or controlled by the player | Same values plus internal counters when needed |
| Minimap contacts | Global contacts under the exact minimap type/liveness rules, with only type, side/color and position at its displayed precision; include `source: minimap` | Exact positions/type/side may be exposed |
| Scene entities | Only entities/fields established by the current view, picking and displayed UI; include source and observation boundary | All registered scenario entities, including off-screen ones |
| Enemy health/design/intent | Omit unless the current player surface demonstrably exposes that field; distinguish unavailable from zero | Exact health, definitions and internal intent where registered |
| Path nodes, avoidance scores, AI targets, RNG state and queues hidden from UI | Never in the default player view | Explicit diagnostic fields, documented as privileged |
| Removed/lost contact | Report `contact_lost` when no longer observable; do not reveal an unseen destruction cause | Report actual lifetime removal with stable identity |

Minimap position quantization must follow the actual displayed map transform/resolution; simply exposing exact world floats for all its icons would increase information precision. Object commands still require scene-target eligibility where the human route requires a traceable object. Seeing an icon does not authorize an object-target command through the minimap. A client can request a legitimate camera move to inspect that area.

This proposal intentionally leaves unproven fields unavailable. The implementation must measure observation parity for own/enemy/neutral objects, occlusion, off-screen contacts, invisible/trace-invisible objects, radar range and removed entities. Enabling a richer tactical view or omniscient AI observation is a separate gameplay decision, not a serializer detail. No new fog-of-war rule is proposed.

## Framing, versioning and IDs

Each frame contains a four-byte unsigned little-endian payload length followed by exactly that many UTF-8 bytes containing one JSON object. Length is in bytes, not characters. Reject zero length, payloads over 1 MiB, invalid UTF-8/JSON, duplicate keys, nesting beyond 32 and non-finite numbers. The parser must enforce limits before allocating the declared payload. Partial reads/writes and multiple frames per read are normal. Truncation at disconnect discards the incomplete frame.

The initial handshake negotiates a major/minor protocol version, profile, operations, units and actual limits. `versions` lists versions the client supports; the server selects a mutually supported pair or returns `unsupported_version`. Minor revisions may add optional request and response fields without changing existing semantics. Validate requests against the selected minor version; a client that negotiates down omits newer fields. Reject unknown request fields/operations after negotiation. For `hello` only, ignore unknown optional top-level fields so a newer client can negotiate down; mandatory extensions require a different major version. Framing, duplicate-key and size checks still apply to hello. Clients ignore unknown optional response fields. Selecting a bounded JSON implementation remains build/license assessment work.

Requests have `id`, `session` (after hello), `op` and operation-specific fields. Every mutation, including camera/selection and `control.release`, also carries `lease`; `control.acquire` requests one without already possessing it. `hello.operations` lists exact operation names, such as `control.acquire` and `control.release`, not ambiguous capability families. Only advertised operations may be submitted. Diagnostic `advance` additionally requires a named `execution_profile` in the handshake.

Request IDs are unsigned 64-bit decimal values encoded as strings (1 through 18446744073709551615, without leading zeroes). On one connection, a **new** ID must exceed the high-water mark. A retry of an ID retained in the deduplication cache returns its cached receipt/latest result and never executes again; equivalent content means the same parsed object values, ignoring JSON whitespace and object-key order. Reusing a cached ID with different content is `invalid_request`. An uncached ID at or below the high-water mark returns `result_expired`; it is never a new command. Reserve the ID once a valid envelope is accepted, including an immediately rejected request, so retries cannot change its meaning.

Deduplication is connection-scoped, for client request/cancellation uncertainty while the pipe remains open; the reliable transport alone does not define application retry behavior. It intentionally does not promise reconnection replay. Retain pending entries until terminal results, cap their admission at the pending-request limit, and evict only completed entries from a cache bounded by the advertised entry and byte limits. The high-water mark survives eviction for that connection. A too-large cache entry is rejected before dispatch, not executed without a retry record. On reconnect IDs start afresh, but session command tokens remain queryable while bounded session history retains them.

Session, entity and observation IDs are opaque strings independent of pointer width or JSON numeric precision. Entity identities include a session generation and are never recycled within a session. Restarts invalidate every prior entity, selection revision and queued command. A new session requires a new handshake/connection; the old endpoint ends its session before teardown.

Positions specify their coordinate space (`world`, `minimap` or another advertised space) and units. Commands must use an advertised targeting route. Internal movement cells and footprint offsets stay inside the shared dispatcher. Reject NaN/infinity, out-of-range values, invalid types and stale entities before entering engine code.

## Timing, acknowledgements and command ordering

Transport threads own only framing and bounded queues. They never call gameplay code or follow entity pointers. Validation, dispatch, snapshot creation and lifetime-ID updates occur on the engine thread. Encoding/writing can run on copied values off-thread. The proposed realtime service boundary is immediately after native message dispatch and the exit-flag check in `L3GRun`, but before its inactive/no-form early continue. Process control/status work there even when simulation is inactive; do not call `Takt` to service a request. Native input in that iteration precedes automation. This is a proposed narrow integration point, not existing API code. The [time/RNG investigation](https://github.com/pozitronik/MatrixGame/issues/76) must still approve the diagnostic advancement schedule; immediate `PGOrder*` effects prevent claiming arbitrary fixed-tick equivalence.

Separate these records:

- `received`: the bounded input queue accepted a mutating gameplay request and assigned a session command token. It has not been validated against game state and is not an accepted game order.
- `command_result`: every queued mutation gets one terminal result: `outcome: dispatched`, `rejected` (with `code`/`reason`) or `unknown` after a partial failure, with its command token, observation boundary and selection revision. `dispatched` means the production route ran successfully; it does not promise arrival, capture or a kill. A legitimate no-op is not invented as a failure.
- State/event observations: later progress, order replacement, health/lifetime changes and battle result. Do not label snapshot-derived transitions as an exhaustive shot/event stream.

Handshake, control and read-only operations return their named reply or an error directly; they do not receive a gameplay command token or `received`. Envelope/limit failures return `type: error, outcome: rejected` before queue admission. After a gameplay request receives `received`, all rejection paths, including a revoked/mismatching lease, use `command_result` with `outcome: rejected`. The engine checks authority/context in receive order before entering gameplay. Validate target/selection again at dispatch, since either may change after receipt. Batching and future-time scheduling are not MVP operations. Retry behavior follows the connection high-water/cache rules above; retries never enqueue a second command.

On reconnect, do not claim exactly-once delivery across a lost connection. The client obtains a fresh snapshot and must not blindly replay a mutating request whose result it lost. A retained status query can resolve a known command token while that session's bounded history exists; otherwise return `result_expired`.

Realtime clients can await observations/events without advancing time. A diagnostic `advance` operation is conditional on an advertised, approved execution profile from the harness. It accepts a bounded schedule, reports actual completed boundaries and stops on timeout/session end/error. It may not silently suppress presentation/audio, change the timestep, split RNG streams or execute whole seconds as one giant step. Until the profile is established, return `unsupported_capability`. This is the principal unresolved design dependency.

### Activity, pause and battle lifecycle

`L3GRun` skips simulation when `GFLAG_APPACTIVE` is clear or there is no current form. Deactivation clears that flag unless keep-alive is set; exclusive mode also minimizes. Only cheat input currently enables keep-alive. The API neither enables it implicitly nor changes those rules. A script can control a foreground game without activating a client window; interactive use from another window must expect an inactive simulation.

Every observation includes `runtime` with `app_active` (the engine flag), `form_ready`, `paused`, `dialog`, `settings_open`, `automatic_mode`, `keep_alive`, `speed_multiplier` (1 or 4), `device_ready`, `simulation_step` and `game_time_ms`. Report actual values rather than interpreting connection success as active play. Read/control service continues at the proposed boundary when inactive, using the last completed gameplay snapshot plus current engine-thread lifecycle state; tag that snapshot's step/time so it cannot look like new simulation progress. Never query mesh/resource caches to refresh it. Settings-window or device recovery may stop simulation despite an active application.

| State | Dispatch and wait policy |
| --- | --- |
| Inactive window or no form | Observations/control bookkeeping remain available. Reject gameplay with `invalid_context`, reason `inactive` or `no_form`; do not queue orders until focus returns. No automatic keep-alive |
| Paused battle, otherwise eligible | Orders may dispatch through their normal route, including immediate `PGOrder*` evaluation. Pause is not a promise that all command side effects stop. Observation waits use wall-clock deadlines; no implicit unpause |
| Menu/confirmation dialog, console or settings window open | Reject strategic gameplay with `invalid_context` naming that context. S/R/E/Esc have menu meanings; U currently leaks through the native menu handler. This stricter API eligibility policy is explicit and does not change native handling |
| Automatic player-side mode | Reject control acquisition and gameplay with `invalid_context: automatic_mode`; revoke an existing lease if it becomes active. Observation remains available. Demo maps can activate this without cheats |
| Four-speed or keep-alive enabled | Report flags and include them in equivalence manifests. Do not set, clear or conceal them on connect/disconnect |
| Device unavailable | Service bookkeeping; reject gameplay with `device_unavailable`. No advancement capability may conceal missing rendering/effect consumers |
| Battle result reached | Publish `battle_result`, reject further strategic mutations, retain a final snapshot. A later restart/exit publishes `session_ended` and invalidates IDs |

`observe.after` waits for an observation newer than the specified observation ID, not necessarily a simulation step. Only an actual command/state change or completed frame creates a new observation; repeated inactive polling does not manufacture progress. The reply includes the runtime step/time so clients can distinguish lifecycle changes from advancement. `wait_ms` is 0..`hello.limits.wait_ms_max` (proposed maximum 30000); timeout returns `observation_timeout` with current runtime state and the last observation ID. Paused/inactive waits expire in wall time. Terminal session state ends outstanding waits immediately. No observer wait blocks message dispatch or the engine thread.

Diagnostic advancement must separately define whether it works without focus and with a native device; until that execution profile is approved it remains unsupported. Moving the realtime service point ahead of the inactive gate does not authorize moving simulation there.

## Backpressure and failure behavior

Initial proposed limits are one connection and one controlling client, 64 pending requests, 32 dispatched commands per engine boundary, at most 10 periodic snapshots per second, 1 MiB per frame and 8 MiB total queued output, a 30000 ms maximum observation wait, and a deduplication cache capped at 1024 entries/16 MiB (including pinned pending entries). Session command-result history has the same entry/byte cap; eviction makes `command_status` return `result_expired`. They are tunable implementation starting points, not measured safe performance claims. Include per-field/entity selection and bounded pagination when a world snapshot exceeds a frame; all pages must refer to one immutable observation boundary and expire explicitly.

Return `busy` before enqueue when input is full. Coalesce unsent replaceable snapshots and include monotonic observation sequence numbers. Never silently drop command results or required lifecycle events: if bounded output cannot retain them, disconnect with an explicit overflow reason when possible and revoke control. Neither a slow client nor pipe shutdown may block the game thread. Timeouts/queue bounds must be tested under a stopped reader and malformed/truncated input.

| Error | Meaning and effect |
| --- | --- |
| `invalid_request`, `unsupported_version`, `unsupported_capability` | No game dispatch; malformed framing closes the connection |
| `wrong_session`, `stale_entity`, `stale_selection` | State changed or the request refers to old state; no partial command mutation |
| `not_controller`, `not_observable`, `invalid_target`, `invalid_context` | No authority, native player rules or the explicitly documented API context policy disallow the request |
| `busy`, `output_overflow`, `result_expired` | Bounded service cannot accept/retain the operation or its history; no implied retry of a dispatched command |
| `session_ended`, `device_unavailable`, `observation_timeout`, `advance_timeout` | Stop the requested wait/advance and report actual progress; do not manufacture a successful result |
| `internal_error` | Retire the affected request/session safely; retain a reproducible diagnostic without leaking memory addresses as protocol IDs |

An error before dispatch changes no gameplay. An exception during a nontransactional production command may already have changed state; report `outcome: unknown`, stop further mutation and require session recovery. Do not claim rollback that the engine does not provide.

## Human intervention, disconnect and shutdown

The default player policy is human priority. Any native key/button/wheel action delivered to game input, including selection, group recall, camera controls, pause/menu actions and settings entry, revokes the lease before its handler runs. Pointer motion alone does not revoke it unless it drives an active drag, mouse-look or edge-scroll; those do. Focus loss also revokes the lease. OS events unrelated to player input, such as paint/resize notifications, do not. Reject queued-but-undispatched gameplay requests with terminal `not_controller` results and require explicit reacquisition. Observations can continue. Already-dispatched orders remain ordinary game orders; disconnect must not issue an implicit stop or change the player's strategy. Release only held-input state owned by automation, not the human's keys. The MVP should avoid held controls until ownership is implemented and tested.

On a diagnostic disconnect, cancel pending advancement at the next safe boundary and remain paused under the diagnostic profile; never convert that session into an uncontrolled realtime run. Closing the game cancels I/O, joins transport work and retires snapshots/IDs before engine owners disappear. Test disconnect before dispatch, during advancement, during target destruction and during teardown. A connection must not keep the process alive indefinitely.

### Unsolicited events

Server events have `type: event`, `session`, a monotonically increasing session `event` ID, `kind`, `observation` and a kind-specific payload; they have no request `id`. Deliver event order as observed on the engine thread. Mandatory lifecycle/control events do not require a subscription; optional contact/progress events require an advertised `subscribe` operation. Unknown optional event fields are ignored under the negotiated minor version. A reconnect starts with a snapshot, not an assumed complete event replay.

```jsonl
{"type":"event","session":"s1","event":"e1","kind":"lease_revoked","observation":"o5","lease":"l1","reason":"human_input"}
{"type":"event","session":"s1","event":"e2","kind":"contact_lost","observation":"o6","entity":"s1:e2","source":"minimap"}
{"type":"event","session":"s1","event":"e3","kind":"battle_result","observation":"o7","result":"victory"}
{"type":"event","session":"s1","event":"e4","kind":"session_ended","observation":"o7","reason":"restart"}
```

These are independent shape examples, not events inserted into the request exchange below. `lease_revoked` precedes queued rejections for that lease. `contact_lost` does not disclose an unobserved destruction cause. `battle_result` describes the player-facing outcome (`victory` or `defeat`); a quit without an outcome emits session end without inventing one. Session end cancels outstanding waits/queued requests, retains already-dispatched results only for bounded delivery and closes the connection before engine owners disappear. On an abrupt process failure the client sees disconnect; delivery cannot be guaranteed.

## Example complete exchange

This illustrative realtime exchange shows capability negotiation, control, observation, selection, movement, progress and release. IDs, coordinates and results are examples, not a measured battle trace. Each line is the JSON payload of a separate framed message. The example assumes these capabilities have passed human-route equivalence tests. `select` is allowed only for the scene-selectable entity in the returned observation; the world-point move uses the advertised landscape route.

Client requests and server replies interleave in order:

```jsonl
{"id":"1","op":"hello","versions":[{"major":1,"minor":0}],"profile":"player"}
{"id":"1","type":"hello","version":{"major":1,"minor":0},"session":"s1","profile":"player","operations":["observe","select","move","control.acquire","control.release","command_status"],"units":{"world":"engine_world_units"},"target_routes":["landscape"],"limits":{"frame_bytes":1048576,"pending_requests":64,"commands_per_boundary":32,"snapshots_per_second":10,"output_bytes":8388608,"wait_ms_max":30000,"dedup_entries":1024,"dedup_bytes":16777216,"history_entries":1024,"history_bytes":16777216}}
{"id":"2","session":"s1","op":"control.acquire"}
{"id":"2","type":"control","session":"s1","lease":"l1","policy":"human_priority"}
{"id":"3","session":"s1","op":"observe","fields":["selection","scene"]}
{"id":"3","type":"observation","session":"s1","observation":"o1","runtime":{"app_active":true,"form_ready":true,"paused":false,"dialog":null,"settings_open":false,"automatic_mode":false,"keep_alive":false,"speed_multiplier":1,"device_ready":true,"simulation_step":"10","game_time_ms":100},"selection_revision":"r0","selection":[],"scene":[{"id":"s1:e1","source":"scene","selectable":true,"side":"player","kind":"robot"}]}
{"id":"4","session":"s1","lease":"l1","op":"select","entities":["s1:e1"],"selection_revision":"r0"}
{"id":"4","type":"received","session":"s1","command":"c1"}
{"id":"4","type":"command_result","session":"s1","command":"c1","outcome":"dispatched","observation":"o2","selection_revision":"r1"}
{"id":"5","session":"s1","lease":"l1","op":"move","selection_revision":"r1","point":{"space":"world","x":320,"y":240},"target_route":"landscape"}
{"id":"5","type":"received","session":"s1","command":"c2"}
{"id":"5","type":"command_result","session":"s1","command":"c2","outcome":"dispatched","observation":"o3","selection_revision":"r1"}
{"id":"6","session":"s1","op":"observe","after":"o3","wait_ms":500,"fields":["selection","scene"]}
{"id":"6","type":"observation","session":"s1","observation":"o4","runtime":{"app_active":true,"form_ready":true,"paused":false,"dialog":null,"settings_open":false,"automatic_mode":false,"keep_alive":false,"speed_multiplier":1,"device_ready":true,"simulation_step":"11","game_time_ms":116},"selection_revision":"r1","selection":["s1:e1"],"scene":[{"id":"s1:e1","source":"scene","selectable":true,"side":"player","kind":"robot","motion":"moving"}]}
{"id":"7","session":"s1","lease":"l1","op":"control.release"}
{"id":"7","type":"control","session":"s1","released":true}
```

`observe.after` waits at most the advertised wait limit and returns a newer completed observation or a timeout; it does not advance the simulation itself. Movement is not declared complete by this example. A scenario wanting arrival must wait for an agreed observable predicate with a bound and report failure if it is not met.

After release, a well-formed mutating request can enter the transport queue, but lease validation rejects it at the engine boundary:

```jsonl
{"id":"8","session":"s1","lease":"l1","op":"move","selection_revision":"r1","point":{"space":"world","x":400,"y":240},"target_route":"landscape"}
{"id":"8","type":"received","session":"s1","command":"c3"}
{"id":"8","type":"command_result","session":"s1","command":"c3","outcome":"rejected","code":"not_controller","observation":"o4","selection_revision":"r1"}
```

For diagnostics, the analogous sequence is hello with an explicitly launched diagnostic profile, observe, submit commands, bounded `advance` under the negotiated profile, observe and release. The concrete advance payload and tick units remain intentionally unspecified until the time/RNG contract is accepted. An implementation must not advertise this capability before it can honor that contract.

## Client and validation deliverables

Provide a small documented command-line client and importable Python module: connect/hello, acquire/release, observe/wait, submit and query command status. Keep its wire layer separate from scenario assertions, with no AI framework dependency. A Windows pipe wrapper can use native APIs through the standard library's FFI; packaging and cancellation behavior need tests. Print JSON for shell composition and human-readable errors to standard error. Never require users to copy a private resource package into the client distribution.

Protocol tests should cover byte fragmentation/coalescing, UTF-8 byte lengths, duplicate fields, limits, request deduplication, stale session/entity/selection IDs, disconnect, slow readers, human takeover and partial command failure. Integration tests compare structured and native commands on equal production fixtures, including auto toggles, stop's extra effects, minimap targeting, neutral-building context capture, invalid/empty selections and commands during pause/dialog/preorder/automatic mode. Confirm that current selectors reject flyers; dormant flyer offsets are not an MVP reachability claim. Test group recall both sides of the double-press window, including its camera effect. Cover foreground/inactive service, unchanged keep-alive/speed flags, wall-clock wait expiry, result/session events, and the documented API rejection of menu-mode auto orders. Exercise version downgrade with optional hello fields and reject newer request fields after downgrade; cover cached identical/conflicting IDs, evicted IDs, cache byte pressure and reconnect uncertainty. Observation tests prove that collecting data does not consume RNG or change lazy resource state and does not expose fields beyond the selected profile. The original x86 DLL ABI/build remains covered.

## Open decisions and handoff

The pipe transport, conservative information policy, human-priority lease and strategic MVP are recommendations for review. Final timing, bounded advancement and effects/audio treatment require reconciliation with the time/RNG and fixture investigations. Information parity needs native-surface measurements before enabling unproven fields. This report does not claim transport throughput, a working endpoint or a fair AI player.

The [API implementation leaf](https://github.com/pozitronik/MatrixGame/issues/100) owns delivery after the behavior gate. [AI gameplay research](https://github.com/pozitronik/MatrixGame/issues/101) can evaluate assistance, latency, intervention and fairness from this contract without implementing a player yet. No additional remote issues are created by this proposal.

Validation for this report is source tracing of form/side/minimap/picking/UI paths, checking the Windows IPC references, and parsing the example JSON payloads. A transport prototype is not required to establish these source boundaries; framing, performance and native information parity remain implementation validation work.
