# Native baseline validation ledger

The [native validation task](https://github.com/pozitronik/MatrixGame/issues/191) owns execution records transferred from implementation packages. Execution is deferred until the maintainer is ready or adequate reviewed pre-baseline test tooling is available. The [complete baseline](https://github.com/pozitronik/MatrixGame/issues/69) remains open until the required evidence and its other acceptance criteria are satisfied. Implementation issues can close after their own tests/tooling and procedure handoffs are complete.

This initial ledger organizes required evidence; it reports no newly executed playtests. Earlier records have not yet been audited for reuse here. The [completion checklist](baseline-completion.md) proposes the bounded P1-P4 selection; review of those scenario details remains separate from the agreed implementation/execution split.

## Pending evidence groups

Before executing a group, expand it into per-case records for the exact procedure variants and required profiles. A group can pass only when every required record has reviewed evidence or an explicit maintainer disposition. Keep automated test development and missing procedure/setup work with the originating issue.

| Group | Catalog and implementation owner | Procedure source | Preparation | Execution state |
| --- | --- | --- | --- | --- |
| NV-01 | PRES-01, CMD-01/02/05; presentation #87 with commands #80 | [Native steps 1-4](87-presentation.md#native-procedures), [selection/display/input procedures](../PLAYTESTING.md) | Audit earlier evidence and record applicability for P1's device-failure observations. Automated C5 evidence is supplied by `manual_fire_interface_focus`, `manual_track_lifecycle` and `manual_hovercraft_lifecycle`; native focus and visual alignment remain pending. | Pending |
| NV-02 | PRES-02, NAV-03; presentation #87 with navigation #81 | Native steps 1/6 and [audio profiles](../PLAYTESTING.md#prepare-the-configuration-cases) | Prepare P2's moving/removed sound-emitter and successful carrier/icon scenes. | Pending |
| NV-03 | PRES-03; presentation #87 | [Release preparation](../RELEASING.md), native startup/package procedures, [plane correction consumers](305-plane-builders.md#observable-consumers-and-native-evidence) | Audit earlier evidence; prepare exact invalid-configuration, damaged-package and CalcVis inputs/output expectations without bundling private resources. D7's mathematical correction does not establish native visibility output or Debug flyer targeting; use a disposable map copy because CalcVis rewrites its input. | Pending |
| NV-04 | PRES-04; presentation #87 with commands #80 and outcomes #85 | Native step 5 and [settings verification](../SETTINGS.md#verification) | Production menu/form/window input isolation, pause and save/apply/cancel assertions are documented in the [presentation guide](87-presentation.md#settings-input-pause-and-persistence). Settings-driven display rollback integration remains implementation work; review the native setup for rollback, rendering, localization and actual input delivery. | Pending |
| NV-05 | AI-03, MODE-01, OUT-02/03; AI #84, commands #80 and outcomes #85 | A8 demo versus AUTO and C7 timing; [O8-O10 and proposed cases NV-05-1/2](85-outcome-completion.md#timing-automatic-mode-and-native-handoff) | Deliver exact launch, scene and transition procedures for the selected cases. The proposed case inventory is not yet runnable setup. | Pending |
| NV-06 | OUT-02/03; outcomes #85 | [Outcome coverage](85-battle-outcomes.md), [lifecycle procedures](../PLAYTESTING.md) and [proposed cases NV-06-1 through NV-06-4](85-outcome-completion.md#timing-automatic-mode-and-native-handoff) | The [standalone lifecycle suite](85-native-exe-lifecycle.md) supplies exact automated Text/cheats-off setup for result-to-selector/relaunch, failure recovery and pending-result Restart/Exit. Its hidden-window execution does not establish visible loading/focus or the remaining Packed, active-Move and original-host variants. The [R1/I1 failure procedure](85-native-failures.md) supplies the separate visible failed-Restart and late text-interface setup; [contained special outcomes](85-native-special-outcomes.md) cover destruction/results/following-session logic. O10 still owns the remaining O4-O8 procedure/setup handoff. O5/O8 retain automated quit coverage; only native execution transfers here. In-process restart, standalone relaunch and fresh initialization are distinct; original-host reentry stays NV-07. | Pending |
| NV-07 | HOST-01: original x86 host boundary; baseline #69 (original implementation owner #86 is closed) | [Testing](../TESTING.md) and the [behavior catalog](../research/75-behavior-baseline/README.md) | #69 owns preparing or assigning the host procedure: enumerate real-host checks, supported setup, resource availability and expected observations. | Pending |

P5's clear-rectangle/plane defects stay with their implementation/disposition owners. A failure found during any native group links to its defect and blocks acceptance of that case; moving the execution record here does not turn the defect into postponed validation.

## Per-case record

Use this record when preparing or executing a case; keep one result per execution profile. Procedure preparation and execution status are separate fields.

| Field | Required value |
| --- | --- |
| Identity | Group and unique case ID; catalog entries; originating implementation issue; procedure path and version |
| Preparation | Ready or missing setup; responsible implementation owner; exact commands, inputs, steps and expected observations/exit status |
| Execution profile | Source revision, binary SHA-256, pinned compiler/dependencies, Debug/Release, EXE/DLL role, cheats/assertion options, packed/text mode |
| Game profile | Map/configuration/resource identities, effective difficulty/settings, RNG and compatibility modes, audio packages/mapping, time and command schedule where required |
| Device profile | Windows/adapter, resolution/mode/scaling, input and observation method; precision/render policy where required by the catalog |
| Result | Pending, passed, failed or reused; actual observations, exit code, diagnostic/evidence reference and reviewing decision |
| Reuse or disposition | For reused evidence, original record and applicability analysis against changed code/profile; for a missing or failed variant, explicit unresolved item or maintainer disposition and revalidation record |

Keep source assets and private configuration copies untracked. Public evidence contains reproducible setup descriptions and results; privately retained resource identities can be referenced without uploading packages.

## Execution and acceptance

Apply the existing Debug/Packed, Debug/Text, Release/Packed and Release/Text requirements to the relevant native cases. Record audio availability profiles independently. Host procedures use their supported x86 host configuration rather than assuming standalone behavior. Unavailable host/resources/localization or hardware behavior remain named gaps until the required check is performed or the maintainer explicitly disposes of that requirement.

Continue to run automated regressions during implementation. A future native-control adapter can execute applicable cases if its observations establish the contract. The production AI API remains downstream of baseline acceptance, so this ledger must not depend on its delivery. No game window is launched by preparing this document.

The implementation completion comment must identify all transferred case IDs and delivered setup. The validation task closes after its records are reviewed; #69 additionally needs its reference/comparison, integrated build, measurement and explicit acceptance evidence. Earlier manual testing is eligible for reuse after an applicability check, but pending audit cannot be reported as a pass.
