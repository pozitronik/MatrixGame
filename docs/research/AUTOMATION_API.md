# External game automation: protocol proposal

This is the design report for [external command/observation research](https://github.com/pozitronik/MatrixGame/issues/99). It proposes an opt-in local API for the standalone Windows executable, implemented after the whole-game behavior baseline. It is not an available game feature. The inspected source reference is `540251d4662c590bad4684df01e0c521e6c4aee7`.

The API should let a client obtain structured observations, issue ordinary player commands and await meaningful state changes. It must not require repeated screenshot parsing. An AI controller can later use the same client as a regression runner, but this deliverable includes no model provider, assistant policy or playable AI.

## Recommendation

Use one local Windows named-pipe connection with versioned UTF-8 JSON messages. Keep transport/encoding outside engine logic. The engine thread validates and dispatches queued commands through the same command boundary as human input, then produces bounded value snapshots and events. Use an exclusive control lease so a client and a human cannot silently issue interleaved orders. A human gameplay action revokes that lease by default.

Expose two explicitly different launch profiles:

- **Player:** ordinary realtime play, existing player information and commands, no state edits or time stepping. Connecting never pauses or accelerates the game.
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

Windows supports overlapped pipe operations and an explicit remote-client rejection flag. Use byte mode with application framing, `FILE_FLAG_OVERLAPPED`, `PIPE_REJECT_REMOTE_CLIENTS` and a first-instance check; do not use `PIPE_NOWAIT` as the asynchronous mechanism. These choices follow the [CreateNamedPipe contract](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createnamedpipea) and [Microsoft's overlapped I/O example](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-server-using-overlapped-i-o).

Create a non-inheritable endpoint with an explicit DACL limited to the launching logon session and the necessary access rights. Do not use the default named-pipe descriptor as a same-user-only policy. The endpoint name should identify a unique process/session; publish it only through the opt-in launch output or a user-protected discovery file, and remove that file at shutdown. The relevant OS access rules are documented in [Named Pipe Security and Access Rights](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights). These are bounded local-control requirements, not a remote administration service.

## Existing command routes and equivalence

[`CFormMatrixGame::Keyboard` and `MouseKey`](../../MatrixGame/src/MatrixFormGame.cpp) handle input context before reaching the side. [`CMatrixSideUnit::OnLButtonDown` and `OnRButtonDown`](../../MatrixGame/src/MatrixSide.cpp) perform target/type checks, minimap conversion and formation-coordinate adjustment. `SelGroupToLogicGroup` creates/reuses a logic-group slot, and `PGOrder*` stores intent and sometimes immediately evaluates it. Directly writing robot coordinates, group orders or resources would bypass that behavior.

For example, explicit move starts with the `M` preorder; target confirmation converts world coordinates through `GLOBAL_SCALE_MOVE` and subtracts half the robot footprint. The same branch also moves selected flyers, with random offsets. `PGOrderMoveTo` alone would miss that flyer handling and some input-context behavior. A context click on an enemy building requests capture, while other enemy units can cause attack; a minimap click is converted to a landscape point. Treat `move`, `attack` and `context_action` as different commands.

Auto-order buttons/keys also manipulate interface flags. Repeating an auto-order can request stop rather than an idempotent setting. A stop key calls both `PGOrderStop` and `SelectedGroupBreakOrders`. The API must describe these existing semantics; it must not silently turn toggles into setters or remove autonomous behavior. [The fixture investigation](https://github.com/pozitronik/MatrixGame/issues/77) demonstrates a real guarded keyboard route but does not establish all of these paths.

Implementation should extract only the required shared dispatch boundary, retaining the existing guards, conversion, selection/group changes, sound/RNG calls and timing. Keep paired tests: native form/UI route versus structured command on equal fixtures, then compare snapshots and RNG evidence. Do not retain two hand-copied implementations of validation. The production extraction belongs after the baseline unless separately approved as a minimal necessary test seam.

### Proposed command catalog

All player commands operate on the session's current selection, like the existing controls. Selection is a separate acknowledged command; subsequent commands carry the resulting selection revision to detect intervening changes. A request cannot silently reselect units by passing an arbitrary actor list with every order.

| API operation | Shared production route | Validation and scope |
| --- | --- | --- |
| `select`, `selection.add`, `selection.clear` | `Select`, `SideSelectionCallBack`/group operations as appropriate to the selection gesture | Owned/selectable entity types, current mode and existing order/size rules. An entity-ID selector is enabled only after equivalence to a permitted selection gesture is proven; no unlimited group creation |
| `group.assign`, `group.recall` | Form number-group handling | Existing slot/modifier behavior, membership and removed-entity handling |
| `move`, `patrol` | Explicit preorder confirmation, then `PGOrderMoveTo`/`PGOrderPatrol` | Point coordinates, bounds, current selection, command mode, footprint conversion and flyer branch |
| `context_action` | `OnRButtonDown` semantic target dispatch | Preserve capture/attack/move precedence and minimap-versus-scene distinction |
| `attack`, `capture`, `repair`, `bomb` | Matching preorder route and `PGOrder*` | Target lifetime/type/ownership, weapon capability and cancellation rules; object and ground targets differ |
| `stop`, `auto_attack`, `auto_capture`, `auto_defend` | Form/UI order handler | Preserve toggle/flag/group changes and immediate evaluation behavior |
| `camera` | Camera's existing movement/zoom/rotation actions | Needed to expose/select objects through legitimate views; bounded parameters, no visibility bypass |
| `observe`, `subscribe`, `command_status`, `control.acquire`, `control.release` | API-owned read/control bookkeeping | No simulation mutation except acquiring/releasing future command authority |

The implementation leaf should deliver and prove a useful strategic subset together: selection, move, stop, context/explicit attack, observation, control and disconnect. Add the other catalog commands within that leaf only as their production equivalence is demonstrated; advertise a precise capability list and reject unsupported operations. If the remaining command scope needs additional leaves, propose them before claiming the API covers the entire game.

Construction/queue actions require the actual `CConstructor::RemoteBuild` and interface checks, not `ProduceRobot` with free resources. Direct-control held movement/fire needs lifecycle-safe press/release semantics. Both are cataloged extensions, not assumed MVP capabilities. Dialog confirmation, restart, surrender and application exit likewise need explicit reviewed operations; arbitrary console commands, file loading, memory writes, teleportation, resource grants and forced winners are excluded. The testing harness can exercise broader gameplay internally without waiting for all external commands.

## Information available to clients

There is no demonstrated single visibility predicate suitable for exporting the whole world. [`CMinimap::Draw`](../../MatrixGame/src/MatrixMinimap.cpp) iterates the logic list and draws live robots/buildings/cannons and flyers with type and side-colored icons; it does not apply a side-knowledge filter in that loop. `DrawRadar` uses a local radius. [`CMatrixMap::Trace`](../../MatrixGame/src/MatrixMapTrace.cpp), trace-invisibility flags and the scene's hover path govern different information. [`CIFaceList::CreatePersonal`](../../MatrixGame/src/Interface/CInterface.cpp) builds selected-unit presentation. These source facts do not prove that every internal enemy field is player-visible.

Use an explicit field provenance table, verified with the corresponding native surface before enabling the field:

| Observation | Player profile proposal | Diagnostic profile |
| --- | --- | --- |
| Session, mode, selected IDs, own resource totals, exposed queue/selection UI state | Semantic values already displayed or controlled by the player | Same values plus internal counters when needed |
| Minimap contacts | Only contact type, side/color and position at the minimap's displayed precision; include `source: minimap` | Exact positions/type/side may be exposed |
| Scene entities | Only entities/fields established by the current view, picking and displayed UI; include source and observation boundary | All registered scenario entities, including off-screen ones |
| Enemy health/design/intent | Omit unless the current player surface demonstrably exposes that field; distinguish unavailable from zero | Exact health, definitions and internal intent where registered |
| Path nodes, avoidance scores, AI targets, RNG state and queues hidden from UI | Never in the default player view | Explicit diagnostic fields, documented as privileged |
| Removed/lost contact | Report `contact_lost` when no longer observable; do not reveal an unseen destruction cause | Report actual lifetime removal with stable identity |

Minimap position quantization must follow the actual displayed map transform/resolution; simply exposing exact world floats for all its icons would increase information precision. Object commands still require scene-target eligibility where the human route requires a traceable object. Seeing an icon does not authorize an object-target command through the minimap. A client can request a legitimate camera move to inspect that area.

This proposal intentionally leaves unproven fields unavailable. The implementation must measure observation parity for own/enemy/neutral objects, occlusion, off-screen contacts, invisible/trace-invisible objects, radar range and removed entities. Enabling a richer tactical view or omniscient AI observation is a separate gameplay decision, not a serializer detail. No new fog-of-war rule is proposed.

## Framing, versioning and IDs

Each frame contains a four-byte unsigned little-endian payload length followed by exactly that many UTF-8 bytes containing one JSON object. Length is in bytes, not characters. Reject zero length, payloads over 1 MiB, invalid UTF-8/JSON, duplicate keys, nesting beyond 32 and non-finite numbers. The parser must enforce limits before allocating the declared payload. Partial reads/writes and multiple frames per read are normal. Truncation at disconnect discards the incomplete frame.

The initial handshake negotiates a major/minor protocol version, profile, capabilities, units and actual limits. An unknown major version fails; minor revisions may add optional response fields, but must not alter existing command semantics. Reject unknown request fields/operations to catch misspelled commands. Clients ignore unknown optional response fields. Selecting a JSON dependency or a bounded parser is implementation work requiring its own build/license assessment; do not write an ad hoc parser by splitting strings.

Requests have `id`, `session` (after hello), `op` and operation-specific fields. Request IDs are strictly increasing unsigned 64-bit decimal values encoded as strings (1 through 18446744073709551615, without leading zeroes). Track a per-connection high-water mark so an expired request ID can be rejected without an unbounded history. Session, entity and observation IDs are opaque strings, so clients do not depend on native pointer width or JSON numeric precision. Entity identities include a session generation and are never recycled within a session. Restarts invalidate every prior entity, selection revision and queued command.

Positions specify their coordinate space (`world`, `minimap` or another advertised space) and units. Commands must use an advertised targeting route. Internal movement cells and footprint offsets stay inside the shared dispatcher. Reject NaN/infinity, out-of-range values, invalid types and stale entities before entering engine code.

## Timing, acknowledgements and command ordering

Transport threads own only framing and bounded queues. They never call gameplay code or follow entity pointers. Validation, dispatch, snapshot creation and lifetime-ID updates occur on the engine thread. Encoding/writing can run on copied values off-thread. Choose the dequeue boundary alongside the harness's [time/RNG investigation](https://github.com/pozitronik/MatrixGame/issues/76); the current map loop and immediate `PGOrder*` effects prevent claiming arbitrary fixed-tick equivalence.

Separate these records:

- `received`: the bounded input queue accepted the request. It has not been validated against game state and is not an accepted game order.
- `command_result`: the engine boundary dispatched or rejected it, with an authoritative command sequence, observation boundary and selection revision. `dispatched` means the production route ran successfully; it does not promise arrival, capture or a kill. A legitimate no-op is not invented as a failure.
- State/event observations: later progress, order replacement, health/lifetime changes and battle result. Do not label snapshot-derived transitions as an exhaustive shot/event stream.

The server processes a connection's requests in receive order. Validate target/selection again at dispatch, since either may change after receipt. A batch is an ordered convenience, not an atomic transaction; report each command result. Start without future-time scheduling. Repeated identical request IDs on one connection return the prior receipt/result without executing again, using a bounded deduplication cache; reusing an ID with different content is an error. Once an ID ages out, reject older sequence numbers rather than reexecuting an uncertain command.

On reconnect, do not claim exactly-once delivery across a lost connection. The client obtains a fresh snapshot and must not blindly replay a mutating request whose result it lost. A retained status query can resolve a known command token while that session's bounded history exists; otherwise return `result_expired`.

Realtime clients can await observations/events without advancing time. A diagnostic `advance` operation is conditional on an advertised, approved execution profile from the harness. It accepts a bounded schedule, reports actual completed boundaries and stops on timeout/session end/error. It may not silently suppress presentation/audio, change the timestep, split RNG streams or execute whole seconds as one giant step. Until the profile is established, return `unsupported_capability`. This is the principal unresolved design dependency.

## Backpressure and failure behavior

Initial proposed limits are one connection and one controlling client, 64 pending requests, 32 dispatched commands per engine boundary, at most 10 periodic snapshots per second, 1 MiB per frame and 8 MiB total queued output. They are tunable implementation starting points, not measured safe performance claims. Include per-field/entity selection and bounded pagination when a world snapshot exceeds a frame; all pages must refer to one immutable observation boundary and expire explicitly.

Return `busy` before enqueue when input is full. Coalesce unsent replaceable snapshots and include monotonic observation sequence numbers. Never silently drop command results or required lifecycle events: if bounded output cannot retain them, disconnect with an explicit overflow reason when possible and revoke control. Neither a slow client nor pipe shutdown may block the game thread. Timeouts/queue bounds must be tested under a stopped reader and malformed/truncated input.

| Error | Meaning and effect |
| --- | --- |
| `invalid_request`, `unsupported_version`, `unsupported_capability` | No game dispatch; malformed framing closes the connection |
| `wrong_session`, `stale_entity`, `stale_selection` | State changed or the request refers to old state; no partial command mutation |
| `not_controller`, `not_observable`, `invalid_target`, `invalid_context` | No authority or the existing player rules disallow the request |
| `busy`, `output_overflow`, `result_expired` | Bounded service cannot accept/retain the operation or its history; no implied retry of a dispatched command |
| `session_ended`, `device_unavailable`, `advance_timeout` | Stop the requested wait/advance and report actual progress; do not manufacture a successful result |
| `internal_error` | Retire the affected request/session safely; retain a reproducible diagnostic without leaking memory addresses as protocol IDs |

An error before dispatch changes no gameplay. An exception during a nontransactional production command may already have changed state; report `outcome: unknown`, stop further mutation and require session recovery. Do not claim rollback that the engine does not provide.

## Human intervention, disconnect and shutdown

The default player policy is human priority. A native gameplay input revokes the client lease, rejects queued-but-undispatched commands and requires explicit reacquisition. Observations can continue. Already-dispatched orders remain ordinary game orders; disconnect must not issue an implicit stop or change the player's strategy. Release only held-input state owned by automation, not the human's keys. The MVP should avoid held controls until ownership is implemented and tested.

On a diagnostic disconnect, cancel pending advancement at the next safe boundary and remain paused under the diagnostic profile; never convert that session into an uncontrolled realtime run. Closing the game cancels I/O, joins transport work and retires snapshots/IDs before engine owners disappear. Test disconnect before dispatch, during advancement, during target destruction and during teardown. A connection must not keep the process alive indefinitely.

## Example complete exchange

This illustrative realtime exchange shows capability negotiation, control, observation, selection, movement, progress and release. IDs, coordinates and results are examples, not a measured battle trace. Each line is the JSON payload of a separate framed message. The example assumes these capabilities have passed human-route equivalence tests. `select` is allowed only for the scene-selectable entity in the returned observation; the world-point move uses the advertised landscape route.

Client requests and server replies interleave in order:

```jsonl
{"id":"1","op":"hello","versions":[{"major":1,"minor":0}],"profile":"player"}
{"id":"1","type":"hello","version":{"major":1,"minor":0},"session":"s1","profile":"player","capabilities":["observe","select","move","control"],"units":{"world":"engine_world_units"},"target_routes":["landscape"],"limits":{"frame_bytes":1048576,"pending_requests":64}}
{"id":"2","session":"s1","op":"control.acquire"}
{"id":"2","type":"control","session":"s1","lease":"l1","policy":"human_priority"}
{"id":"3","session":"s1","op":"observe","fields":["selection","scene"]}
{"id":"3","type":"observation","session":"s1","observation":"o1","selection_revision":"r0","selection":[],"scene":[{"id":"s1:e1","source":"scene","selectable":true,"side":"player","kind":"robot"}]}
{"id":"4","session":"s1","lease":"l1","op":"select","entities":["s1:e1"],"selection_revision":"r0"}
{"id":"4","type":"received","session":"s1","command":"c1"}
{"id":"4","type":"command_result","session":"s1","command":"c1","outcome":"dispatched","observation":"o2","selection_revision":"r1"}
{"id":"5","session":"s1","lease":"l1","op":"move","selection_revision":"r1","point":{"space":"world","x":320,"y":240},"target_route":"landscape"}
{"id":"5","type":"received","session":"s1","command":"c2"}
{"id":"5","type":"command_result","session":"s1","command":"c2","outcome":"dispatched","observation":"o3","selection_revision":"r1"}
{"id":"6","session":"s1","op":"observe","after":"o3","wait_ms":500,"fields":["selection","scene"]}
{"id":"6","type":"observation","session":"s1","observation":"o4","selection_revision":"r1","selection":["s1:e1"],"scene":[{"id":"s1:e1","source":"scene","selectable":true,"side":"player","kind":"robot","motion":"moving"}]}
{"id":"7","session":"s1","lease":"l1","op":"control.release"}
{"id":"7","type":"control","session":"s1","released":true}
```

`observe.after` waits at most the advertised wait limit and returns a newer completed observation or a timeout; it does not advance the simulation itself. Movement is not declared complete by this example. A scenario wanting arrival must wait for an agreed observable predicate with a bound and report failure if it is not met.

After release, a mutating request fails without reaching the game:

```json
{"id":"8","type":"error","session":"s1","code":"not_controller","dispatched":false}
```

For diagnostics, the analogous sequence is hello with an explicitly launched diagnostic profile, observe, submit commands, bounded `advance` under the negotiated profile, observe and release. The concrete advance payload and tick units remain intentionally unspecified until the time/RNG contract is accepted. An implementation must not advertise this capability before it can honor that contract.

## Client and validation deliverables

Provide a small documented command-line client and importable Python module: connect/hello, acquire/release, observe/wait, submit and query command status. Keep its wire layer separate from scenario assertions, with no AI framework dependency. A Windows pipe wrapper can use native APIs through the standard library's FFI; packaging and cancellation behavior need tests. Print JSON for shell composition and human-readable errors to standard error. Never require users to copy a private resource package into the client distribution.

Protocol tests should cover byte fragmentation/coalescing, UTF-8 byte lengths, duplicate fields, limits, request deduplication, stale session/entity/selection IDs, disconnect, slow readers, human takeover and partial command failure. Integration tests compare structured and native commands on equal production fixtures, including auto toggles, stop's extra effects, minimap targeting, flyer offsets, invalid targets and commands during a dialog/preorder. Observation tests prove that collecting data does not consume RNG or change lazy resource state and does not expose fields beyond the selected profile. The original x86 DLL ABI/build remains covered.

## Open decisions and handoff

The pipe transport, conservative information policy, human-priority lease and strategic MVP are recommendations for review. Final timing, bounded advancement and effects/audio treatment require reconciliation with the time/RNG and fixture investigations. Information parity needs native-surface measurements before enabling unproven fields. This report does not claim transport throughput, a working endpoint or a fair AI player.

The [API implementation leaf](https://github.com/pozitronik/MatrixGame/issues/100) owns delivery after the behavior gate. [AI gameplay research](https://github.com/pozitronik/MatrixGame/issues/101) can evaluate assistance, latency, intervention and fairness from this contract without implementing a player yet. No additional remote issues are created by this proposal.

Validation for this report is source tracing of form/side/minimap/picking/UI paths, checking the Windows IPC references, and parsing the example JSON payloads. A transport prototype is not required to establish these source boundaries; framing, performance and native information parity remain implementation validation work.
