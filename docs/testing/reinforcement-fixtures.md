# Synthetic reinforcement fixtures

These fixtures exercise successful reinforcement requests and carrier delivery through production `CMatrixBuilding::Maintenance`, `CMatrixSideUnit::OrderFlyer`, `CMatrixFlyer::ApplyOrder`, robot assembly and ordinary map ticks. They contribute to ECO-03 and NAV-03 without completing those catalog entries. The public fixture uses no game packages, native window or graphics device, and changes no production implementation.

## Explicit icon-rendering failure profile

Reinforcement assembly always calls the robot icon renderer. The default no-render device continues to abort on `CreateRenderTarget`. A session must explicitly set `Profile::unavailable_icon_targets` to enable a different profile: the exact lockable 256-by-256 ARGB icon target request returns `D3DERR_NOTAVAILABLE` with a null output. Invalid arguments return `D3DERR_INVALIDCALL`; successful drawing and all other rendering operations remain unsupported. Accepted failure responses increment an observable counter. The session's metadata uses `storage-device-unavailable-icons-no-draw-cursor-surrogate`, so its evidence cannot be confused with the normal fixture profile.

`game.harness.device.unavailable_icon_targets` checks the HRESULT, cleared output, invalid requests, absence of allocations and repeated device teardown. `game.reinforcement.unavailable_robot_icons` calls production `CreateTextures` twice and requires null large/medium icons, one failed target allocation per call and unchanged RNG state. A later ordinary session has the original strict profile. This is the game's existing icon-allocation failure path, not a substitute icon renderer or evidence of correct native icons.

## Resources and ownership

`tests/battle_flyer.hpp` installs real storage-format material meshes and managed CPU textures for wheel chassis 2, Birex armour 1 and one carrier body. The body has no rotors, engine parts, weapons or fire streams. Its models are loaded by the production flyer loader. Configuration supplies approach distance 300, height 100, landing height 40, carrier hit points 1000 and cargo hit points 1500 (150 displayed robot hit points). Robot material loading, attachment matrices, bounds, carrying, falling and command execution use production code. The caller owns a `BattleMaterialProfile`; the existing session owns map, cache and skin teardown.

The ordinary opposing robot is retained so the scenario remains a battle rather than entering the result dialog. The requesting base is an unregistered fixture object and is not ticked, so it neither grants income nor participates in side-level production strategy. Request eligibility and landing places still come from the production map and navigation structures. Only the request method is called directly; subsequent flight and cargo movement use complete map ticks.

## Contracts

| Case | Assertions |
| --- | --- |
| `game.reinforcement.delivery` | Seed 83, both RNG modes, 10 ms steps: a score-100 proposal creates one carrier and one assembled player robot; request consumes three RNG draws, starts the 1000 ms cooldown and changes no resources. The cargo reserves a valid place and receives an attack order to that place. An immediate repeat request changes nothing. The carrier releases the robot, the robot lands, and the carrier leaves the always-drawn list and is deleted. Object-core leases make unexpected deletion observable before dereferencing a retired unit. |
| `game.reinforcement.reservations` | One live robot reserves place zero. Score 60 creates two carriers; scores 100 and 130 create one each. Each cargo reserves a different free place, skips zero and reaches the ground. A difficulty multiplier of 1.5 yields a 1500 ms cooldown. Request draws are one approach-angle draw plus a proposal and approach-distance draw per carrier; resource balances remain unchanged. |
| `game.reinforcement.carrier_limit` | Sixteen real, setup-only idle carriers fill `MAX_ALWAYS_DRAW_OBJ`. A valid request consumes its cooldown and two proposal/angle draws but allocates no cargo or icon target and leaves every existing carrier intact. Explicit deletion clears all always-drawn entries; these idle carriers are never ticked without trajectories. |

The bounded single-carrier reference times are 4470 ms release, 4790 ms landing and 8750 ms carrier deletion in Park-Miller mode; LegacyCRT gives 4960, 5280 and 9230 ms respectively. Different approach-distance draws explain the different schedules. These expectations describe the declared synthetic configuration and floating-point profile, not all maps or packaged carrier models.

## Running and limits

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.(reinforcement|harness.device)\.'
.\tools\test.ps1 -Configuration Release -Filter '^game\.(reinforcement|harness.device)\.'
```

Use `-Compiler MSVC` for the other supported compiler profile. Debug also checks tracked allocation cleanup. Native successful icon generation, packaged multi-part carrier models, armed/headed cargo variants, carrier destruction, arbitrary terrain and obstacles, rendered delivery effects, successful UI-button routing and broader proposal-budget mixtures remain separate baseline work. This fixture does not permit structural refactoring before whole-baseline acceptance.
