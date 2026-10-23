# Plane builder regression contracts

The correction for [plane query coefficients](https://github.com/pozitronik/MatrixGame/issues/305) resolves legacy finding D7 without changing the plane representation. Each `SPlane` builder obtains coefficients from the existing DirectX math function, copies them to `norm` and `dist`, and then derives sign bits. Queries therefore use the same equation that the builder calculated.

## Covered contracts

The eight `game.geometry.plane.*` cases link the production header and MatrixLib. Assertions stay active in Release. The fixture initializes every plane field to distinct sentinels before construction, so an omitted assignment fails deterministically rather than relying on uninitialized memory.

| Case | Contract |
| --- | --- |
| `point_normal` | A plane through z = 2 with normal +z gives signed distance 8 from z = 10 and downward intersection parameter 8. Both stored coefficient sets agree. |
| `points_orientation` | Reversing point winding reverses signed distance and coefficients while retaining the same ray intersection. |
| `distance_and_sides` | Projection, signed distance and inclusive plane-side classification retain their distinct meanings for a negative normal. |
| `box_octants` | All eight normal-sign combinations agree with an independent eight-corner classification for front, crossing and back boxes. Contact on the plane retains the existing inclusive-front convention. |
| `intersection_contract` | Parallel and near-parallel directions reject without changing the output parameter; scaled directions, negative parameters and origins on the plane retain the existing line-intersection semantics. |
| `reuse_and_alias` | Rebuilding replaces old coefficients/sign bits; non-unit normals keep their scaling, and an input referring to the output's normal remains valid. |
| `direct_fields` | Camera-style direct writes to `norm`/`dist`, sign-bit refresh and copying retain their existing behavior without requiring a builder. |
| `layout` | The supported x86 structure remains 36 bytes, aligned to four bytes, with unchanged offsets for `dxplane`, `norm`, `dist` and `signbits`. |

The original plane checks now run as registered `game.presentation.plane_normal` and `game.presentation.plane_points` engine regressions. Their `probe.presentation.*` executable commands remain unregistered aliases. The integrated clear-rectangle checks have their own `game.presentation.clear_rectangle*` names and retain the same alias convention.

## Reproduce

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^(game\.geometry\.plane\.|game\.presentation\.plane_)'
.\tools\test.ps1 -Configuration Release -Filter '^(game\.geometry\.plane\.|game\.presentation\.plane_)'
.\tools\test.ps1 -Configuration Debug -DLL -Filter '^(game\.geometry\.plane\.|game\.presentation\.plane_)'
.\tools\test.ps1 -Configuration Release -DLL -Filter '^(game\.geometry\.plane\.|game\.presentation\.plane_)'
```

## Assertion strength and compatibility

A pinned MinGW Debug/Release comparison compiled the same eight-case fixture against the unchanged header and the four-assignment candidate. The original fails six mathematical cases and passes the direct-field/layout controls; the candidate passes all eight. Independent omission controls remove both normal copies, both distance copies, or both builder sign-bit updates. The first two controls fail six cases each; the sign-bit control fails five while intersection, direct-field and layout cases pass. These are intended assertion failures, not crashes. The comparison selects temporary header copies without modifying production source. Failed approximate-value checks report the calling line and actual/expected values, using `std::source_location`.

No union aliasing, normalization, allocation, RNG consumption, resource-format or host-interface change is introduced. The existing direct-field construction of ordinary camera frustum planes does not call either builder. Its source path is unchanged; the focused test does not substitute for native camera rendering evidence.

## Observable consumers and native evidence

The live builder consumers are `CalcVis` in `MatrixVisiCalc.cpp` and Debug-only flyer targeting in `MatrixMap.cpp`. `CalcVis` computes silhouette edges and shadow flags in temporary buffers. The flags feed Debug sphere markers; edge construction can also create Debug cones. Its `m_GroupVis` array remains zero-cleared, and the local `CStorage` record is never saved. Correcting the planes therefore changes the edge/shadow calculation, its run time and Debug diagnostics, but generates no saved map or Release visibility output. Map-argument startup appends to `calcvis.log` and exits before the game loop; its Debug markers are not drawn. The in-battle Debug F7 and `CALCVIS` routes can display diagnostics. The Debug flyer command instead changes its target to the mouse ray intersection with the horizontal plane at the flyer's height.

The mathematical regressions do not establish native `CalcVis` run time/Debug markers, Debug flyer input delivery or original-host behavior. Those observations remain under [native validation](native-validation.md) and complete baseline acceptance. `CalcVis` does not rewrite its input map: it loads a local storage object and changes only that unsaved object. Independently, it skips plane construction for non-land cells while the edge pass still reads those cells and their neighbors; this pre-existing caller limitation is recorded in the [legacy findings](../research/89-legacy-evidence/defects.md#d7-plane-representation-mismatch) for separate investigation.
