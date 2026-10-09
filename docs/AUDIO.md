# Standalone sound

Standalone sound effects use the system XAudio2.9 runtime on Windows 10/11. The DirectX 9 graphics runtime remains a separate prerequisite. DLL builds continue to use the host's sound callbacks and do not initialize the standalone audio device.

## Prepare local resources

The sound package contains PCM WAV files. The original game's `CFG/CacheData.dat` maps logical names such as `Sound.ButtonClick` to archive entries. Keep the original packages and generated mapping private and untracked.

Use Python 3.9 or newer to prepare resources from a locally installed copy of the game:

```powershell
python -B tools/prepare-sound-resources.py --game-directory '<original game directory>'
```

The helper reads the original configuration, validates its content checksum and tree format, writes only the sound-name mapping to root-level `sounds.txt`, and copies `sound.pkg` and a selected speech archive to the project root. It does not modify the installation or extract audio. Existing files with different content are rejected rather than overwritten. Russian speech is selected by default; use `--voices Eng` for English, or `--voices None` to prepare effects without copying speech. The speech archive is staged as `voices.pkg`; both language packages use the same virtual resource paths. `--voices None` leaves existing copies untouched: to disable previously prepared speech, remove the local root-level `voices.pkg` and the `DATA/voices.pkg` copy in each output directory you use.

Build Debug and Release with `tools/build.ps1`. The helper stages local `sound.pkg` and optional `voices.pkg` under `DATA`, and `sounds.txt` under `CFG`, beside the executable. No resource files are required to compile or run the automated tests. Background music and Ogg decoding are outside this sound-effect implementation; `music.pkg` is not staged for playback.

The mapping contract is a UTF-16 text configuration with a `Sound` block whose parameters map an event suffix to its virtual archive path. For example, `ButtonClick=sound\ButtonClick.wav` resolves `Sound.ButtonClick`. The generated file is private configuration, not project source.

## Playback and lifetime

The WAV reader accepts mono/stereo PCM with 8-bit unsigned or 16-bit signed samples and validates RIFF boundaries, chunk padding, frame width and byte rate. It also accepts two bounded legacy layouts present in original clips: an outer size that includes the eight-byte RIFF header, and an omitted final sample-chunk pad. Chunk contents must still fit the actual file, and incomplete PCM frames are rejected. The resource loader reads through the existing archive system; decoded clips are cached and shared between voices. Each voice retains its clip until the audio thread has stopped reading it. Shutdown releases voices before cached samples and the audio device.

Volume is clamped to `0..1`; pan/balance is clamped to `-1..1`. Non-finite values select zero volume or centred balance. Mono sounds reach both output channels at centre; positive balance attenuates the left channel and negative balance attenuates the right channel. Stereo clips retain their channel separation. The existing game code supplies positional attenuation, sound layers, loop flags and fading.

Layered sounds now record their active slot, making interrupt, skip and layer shutdown effective in both standalone and host DLL builds. Previously the slot index was never assigned, so those layer controls had no effect. Layer shutdown checks the sound identity before stopping a slot; an expired layer cannot clear another sound that reused that slot. Host callback signatures and interface layout are unchanged, but audible host-game behavior still requires a host playtest.

Destroyed handles are not reused during a service lifetime. Missing mappings, missing/invalid clips and device failures are reported without aborting the battle. Repeated failures for a sound name are suppressed until resource/service reset. The legacy `Sound.BReady` alias is absent from the inspected HD catalog and is handled as missing data rather than substituted with a guessed clip.

## Check sound

Run these checks in Debug and Release with cheats disabled, using the same resource set and the default Windows output device. Record the build revision, configuration, mapping/speech choice and exit status.

1. Dismiss the introductory dialog. Select a friendly robot, click its Move command button, then press `X` to cancel targeting. Check the configured UI click sound; hover sounds may be configured as silence.
2. Select the robot and issue `M`, then click open ground. Listen for the matching movement sound while it moves and for stopping when it settles. Pneumatic footsteps are configured as repeated one-shots; other chassis may loop.
3. Select a gun-carrying robot, press `Enter`, aim into empty ground and hold the left mouse button for two seconds. Release it, then press `Enter` to leave manual control. Check firing sounds and their stop behavior.
4. Use the bomber setup and detonation cases in [Standalone playtesting](PLAYTESTING.md). Check an explosion sound and subsequent selection/input.
5. Pan the camera away from an active robot and back. Check positional volume/balance without asserting an exact loudness or distance from a single listening observation.
6. Close the window while a movement/weapon sound is active. Confirm sound stops, the process exits with status zero and a subsequent launch plays normally.

For a silent run, inspect `test.log` for `Standalone sound initialized`, missing mapping/resource names or device errors. Confirm `CFG/sounds.txt`, `DATA/sound.pkg` and any selected speech package exist in the output directory; select the intended Windows audio output. A missing speech package does not prevent non-speech effects. A successful build or muted native-device smoke test does not establish audible game behavior.
