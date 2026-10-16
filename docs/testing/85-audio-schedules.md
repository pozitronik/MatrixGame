# Audio scheduling and repeated outcome profiles

These checks complete the selected O8 audio and O7 synthetic-session schedule extensions in the [outcome completion plan](85-outcome-completion.md). They link production map, form and audio implementations; the graphics device, audio device and decoded samples remain synthetic. They do not establish audible output, native device recovery or full game initialization.

All four contracts use seed 79, ParkMiller and LegacyCRT, and separate 10 ms and 16 ms schedules. Each profile prints its fixture and build identity. Checks remain active in Release.

| Contract | Observations |
| --- | --- |
| `game.outcome.music_fade` | With no music installed, requesting a fade leaves the changing flag clear. With synthetic music installed, production map ticks fade down and restore the level monotonically, with each change bounded by the supplied timestep. Both directions finish at 1010 ms for 10 ms ticks and 1024 ms for 16 ms ticks. |
| `game.outcome.music_form_refill` | Two form ticks with a lost graphics device start and refill the same music stream while map time and RNG draws remain unchanged. After the device reports success, another form tick refills the stream and advances the map by exactly the selected timestep. Buffer totals are 4, 8 and 12; the queue is explicitly drained between ticks to model playback. |
| `game.outcome.form_audio_device_error` | A healthy audio service survives the first lost-graphics-device tick. A reported audio failure destroys the directly installed service before the graphics early return. Another lost-device tick remains silent and preserves map time/RNG. Once graphics is available, the map advances despite unavailable audio. |
| `game.outcome.repeated_sessions` | Three full-auto outcome journeys (loss, victory, loss) each finish at their production result boundary, then destroy their synthetic session. The intervening fresh sessions have zero clock/frame, active player status, a fresh two-robot selection, unassigned groups and cleared pause/exit/input state. Every journey and fresh session uses the selected RNG/timestep profile. |

The fade checks preserve the existing arithmetic. `CMatrixMapLogic::Takt` first checks proximity to the target, otherwise adds or subtracts `step * 0.001`; the standalone music service clamps the level. A subsequent map tick clears the changing flag. The 16 ms schedule reaches its clamped target at 1008 ms and clears the flag at 1024 ms. This is measured production behavior, not a rounded one-second expectation.

The form checks begin without allocated default-pool graphics resources. Marking resources ready before the successful-device tick avoids recreation in the synthetic fixture. They establish audio work before the lost-device early return and resumed map advancement, not release/recreation of actual Direct3D resources. Audio-service recovery here tests a directly installed service without a replacement factory; it does not claim recovery of a real audio device.

Run the focused contracts in both configurations:

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game.outcome.(music_fade|music_form_refill|form_audio_device_error|repeated_sessions)$'
.\tools\test.ps1 -Configuration Release -Filter '^game.outcome.(music_fade|music_form_refill|form_audio_device_error|repeated_sessions)$'
```

These additions preserve the original 10 ms observations and add the required 16 ms profiles. `repeated_sessions` still uses the synthetic session constructor; successful `CGame::Init`, failed initialization, native input/device lifetimes and original-host reentry remain separate obligations. No native-validation record or complete-baseline acceptance follows from this suite alone.
