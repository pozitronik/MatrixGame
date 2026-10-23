# Automatic positional updates and repeated weapon audio

Two synthetic contracts extend the [presentation audio tests](87-presentation.md) without changing production code. They cover the automatic `AddSound` update route and a weapon's sound reuse across multiple emissions. Both use seed 87, both supported RNG modes and independent 10/16 ms profiles. The fake audio backend retains playing loops until production stops them; generated PCM and meshes require no original resources, native window or audio device.

```powershell
$cases = '^game\.presentation\.(automatic_positional_camera|repeated_weapon_loop)$'
.\tools\test.ps1 -Configuration Debug -Filter $cases
.\tools\test.ps1 -Configuration Release -Filter $cases
.\tools\test.ps1 -Configuration Debug -DLL -Filter $cases
.\tools\test.ps1 -Configuration Release -DLL -Filter $cases
```

## Camera-driven positional sound

`automatic_positional_camera` submits one looping laser sound through `CSound::AddSound` at `(400,400,400)`. These coordinates are exact multiples of the production 40-unit sound grid, including its positive height field. This isolates update wiring from coordinate quantization. The synthetic sound declares pan `[-1,1]`, volume `[0,1]` and attenuation 0.5, scaled by production to 0.001.

The fixture translates the strategy camera and recomputes its matrices. This is a supplied listener movement, not OS input. Immediately after a camera move, the backend's previous pan and volume must remain unchanged. Ordinary `Session::advance` calls then run the whole production map, including its `CSound::Takt` call; the test makes no direct `CSound::Takt`, `ChangePos` or repeated `Play` call to update the sound. After a complete positional-update interval, the same voice must have the new camera-relative pan and gain. The already-covered `CalcPanVol` component supplies the expected envelope at the fixed source; sign and relative-volume assertions separately require the intended spatial change.

Each observation advances at least 120 ms, rounded up to a whole profile step. This crosses the sound manager's strict, greater-than-100-ms update gate without resetting its process-retained private clock or treating its current phase as fresh-session state. The test does not claim a fixed first update timestamp.

Moving the camera beyond the audible distance must destroy the voice exactly once through the positional update. Returning the camera cannot recreate that retired sound. Only a new `AddSound` request may create a replacement voice, using the cached PCM. Global cleanup followed by more ordinary map ticks must leave both voices destroyed exactly once, with no extra allocation or audio diagnostic.

## Weapon loop reuse across shots

`repeated_weapon_loop` creates a real laser weapon effect with a one-unit upward ray, a 100 ms cooldown and looping sound attenuation 10, scaled to 0.02. It starts firing and advances 64 weapon ticks, with no map ticks. This is a component-level firing schedule rather than an autonomous robot combat scenario. The short ray avoids a target and damage/effect RNG consumption.

For zero-based tick `n`, the expected cumulative shot count is `1 + floor(n * step_ms / 100)`. The 10 ms profile emits seven shots in the initial 64 ticks; the 16 ms profile emits eleven. Across all of them, one backend voice must remain playing and retain its identity. Emitter positions change from 25 units right, to an inaudible 260 units right, to 225 units left. The same voice transitions from full volume to zero and then to half volume with opposite pan, including shots while inaudible. No additional voice or PCM load is allowed.

Explicit `FireEnd` retires the first voice once. A new `FireBegin` and one 100 ms weapon tick must produce one more shot and a distinct replacement voice from the cached clip. Destroying the still-firing effect retires that replacement; subsequent global cleanup cannot destroy either voice again. The entire weapon sequence consumes no shared RNG draws.

This differs from the automatic positional array: an existing weapon-owned loop remains alive at zero volume and can become audible again. `AddSound` positional updates remove inaudible entries. The tests preserve both existing contracts rather than prescribing one lifecycle for every positional sound.

## Validation and boundaries

The actual standalone game, x86 DLL and affected presentation target build with pinned MinGW in Debug and Release, with cheats disabled. The two new cases and five existing positional/weapon-loop controls pass in all four configurations: 28 registered-case executions. Removing the automatic `SetSoundPos` update fails the camera/pan assertion; disabling laser sound-handle reuse fails the single-voice assertion on a later shot. Both temporary production mutations are byte-restored, the game and test are rebuilt, and both positive cases pass again after each control.

The emitted profiles identify synthetic continuous-loop audio rather than the base battle fixture's unavailable backend. These tests do not establish native audible output, display reset with active sound, emitter motion or deletion through a robot's full lifecycle, every weapon type, TTL/fade boundaries, real carrier icons or original-host audio callbacks. Those retain their separate coverage and native-evidence obligations. Supported MSVC configurations remain for CI; the complete baseline gate and issue #87 remain open.
