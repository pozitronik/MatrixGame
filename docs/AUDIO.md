# Standalone sound

Standalone sound effects use the system XAudio2.9 runtime on Windows 10/11. The DirectX 9 graphics runtime remains a separate prerequisite. DLL builds continue to use the host's sound callbacks and do not initialize the standalone audio device.

## Prepare local resources

Copy `DATA/Sound.pkg` from an owned game installation to `DATA/sound.pkg` beside `MatrixGame.exe`. No mapping file, Python installation or extraction step is needed for the standard sound effects. The executable includes a default catalog for the battle engine's sound names; audio payloads are supplied separately.

For speech, also copy `DATA/voicesRus.pkg` or `DATA/voicesEng.pkg` from the original installation to `DATA/voices.pkg` beside the standalone executable. Choose one language; both archives use the same virtual resource paths. Speech is optional, and its absence does not prevent sound effects. Background music and Ogg decoding remain outside this implementation; `music.pkg` is not used.

For source builds, put local `sound.pkg` and optional `voices.pkg` at the repository root, then build Debug and Release with `tools/build.ps1`. The helper stages these packages under `DATA` beside the executable. Keep supplied packages unchanged and untracked. No resource files are required to compile or run automated tests.

Both audio packages are optional for startup and are checked as files on disk, independently of registered archives. With neither present, the game leaves standalone audio disabled and does not read overrides or create an audio device. With only one package, available mapped clips can play and absent clips stay silent. A missing `DATA/robots.pkg` instead produces the resource-placement dialog described in the [README](../README.md).

## Optional sound overrides

The maintained catalog in [SoundMapping.cpp](../MatrixGame/src/SoundMapping.cpp) contains the resource identifiers used by the battle engine. To replace individual mappings or add aliases for another resource set, create `CFG/sounds.txt` beside the executable, in UTF-8 or UTF-16 text:

```text
Sound {
    ButtonClick=sound\custom-click.wav
}
```

Keys are case-sensitive event suffixes: `ButtonClick` overrides `Sound.ButtonClick`. Unspecified names keep their built-in defaults. Each key must be unique and contain only ASCII letters, digits or underscores; paths must be nonempty. The file must contain exactly one flat `Sound` block. An unreadable or invalid override file produces a diagnostic and leaves the entire default catalog in use. A valid override pointing to a missing or invalid clip silences that event and logs the resource error; it does not substitute another clip.

For an alternative original-game configuration, the optional helper can prepare an override from that installation's `CFG/CacheData.dat`:

```powershell
python -B tools/prepare-sound-resources.py --game-directory '<original game directory>'
```

This helper requires Python 3.9 or newer. It validates the configuration checksum and tree format, writes the mapping to root-level `sounds.txt`, and copies `sound.pkg` and a selected speech archive to the project root. It does not modify the installation or extract audio. Existing files with different content are rejected rather than overwritten. Russian speech is selected by default; use `--voices Eng` for English or `--voices None` to skip copying speech. That option leaves existing speech files untouched. The build helper stages an existing root-level `sounds.txt` under `CFG` as an override. Generated overrides remain private and are excluded from releases; the built-in catalog supplies release defaults.

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

For a silent run, inspect `test.log` for `Standalone sound initialized`, missing mapping/resource names or device errors. Confirm `DATA/sound.pkg` and any selected speech package exist in the output directory; select the intended Windows audio output. `CFG/sounds.txt` is optional. To check release defaults, test a freshly extracted archive without adding that override, then supply only the desired packages. A missing speech package does not prevent non-speech effects. A successful build or muted native-device smoke test does not establish audible game behavior.
