# Standalone sound

Standalone sound effects and battle music use the system XAudio2.9 runtime on Windows 10/11. The DirectX 9 graphics runtime remains a separate prerequisite. DLL builds continue to use the host's sound callbacks and do not initialize the standalone audio device.

## Prepare local resources

Copy `DATA/Sound.pkg` from an owned game installation to `DATA/sound.pkg` beside `MatrixGame.exe`. No mapping file, Python installation or extraction step is needed for the standard sound effects. The executable includes a default catalog for the battle engine's sound names; audio payloads are supplied separately.

For speech, also copy `DATA/voicesRus.pkg` or `DATA/voicesEng.pkg` from the original installation to `DATA/voices.pkg` beside the standalone executable. Choose one language; both archives use the same virtual resource paths. Speech is optional, and its absence does not prevent sound effects.

For battle music, copy `DATA/music.pkg` from the original installation to `DATA/music.pkg` beside the standalone executable. Music is optional; see [Battle music](#battle-music).

For source builds, put local `sound.pkg` and optional `voices.pkg` and `music.pkg` at the repository root, then build Debug and Release with `tools/build.ps1`. The helper stages these packages under `DATA` beside the executable. Keep supplied packages unchanged and untracked. No resource files are required to compile or run automated tests.

All three audio packages are optional for startup and are checked as files on disk, independently of registered archives. With none present, the game leaves standalone audio disabled and does not read overrides or create an audio device. With only some packages, available mapped clips can play and absent clips or music stay silent. A missing `DATA/robots.pkg` instead produces the resource-placement dialog described in the [README](../README.md).

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

Volume is clamped to `0..1`; pan/balance is clamped to `-1..1`. The standalone master volume (`SoundVolume` in `CFG/standalone.txt`, also set by the [Settings window](SETTINGS.md)) multiplies each voice's volume, including voices already playing; per-voice volumes reported to the game stay unscaled. Host audio in the DLL build ignores it. Non-finite values select zero volume or centred balance. Mono sounds reach both output channels at centre; positive balance attenuates the left channel and negative balance attenuates the right channel. Stereo clips retain their channel separation. The existing game code supplies positional attenuation, sound layers, loop flags and fading.

Layered sounds now record their active slot, making interrupt, skip and layer shutdown effective in both standalone and host DLL builds. Previously the slot index was never assigned, so those layer controls had no effect. Layer shutdown checks the sound identity before stopping a slot; an expired layer cannot clear another sound that reused that slot. Host callback signatures and interface layout are unchanged, but audible host-game behavior still requires a host playtest.

Destroyed handles are not reused during a service lifetime. Missing mappings, missing/invalid clips and device failures are reported without aborting the battle. Repeated failures for a sound name are suppressed until resource/service reset. The legacy `Sound.BReady` alias is absent from the inspected HD catalog and is handled as missing data rather than substituted with a guessed clip.

## Battle music

The battle music tracks are the files directly inside `music\BATTLE` in the registered packages, normally the two tracks of the original `music.pkg`. Loose files in a `music/BATTLE` folder beside the executable are listed as well, and a loose file replaces a packaged one with the same name. Tracks play in case-insensitive name order and the list loops. Each new battle session continues with the track after the one that was playing when the previous session ended, so repeated battles do not always restart the first track. The order uses no game random numbers and does not affect battle simulation.

A game built with `MATRIXGAME_MUSIC=OFF` (`tools/build.ps1 -WithoutMusic`, and every DLL build) has no decoder and does not need libogg or libvorbis. It ignores `music.pkg`, writes `Battle music disabled: this build has no music decoder` to `test.log` once when audio starts, and otherwise behaves as described here without music.

Each track must be an Ogg Vorbis stream (starting with `OggS`) with one or two channels. The original tracks use the `.dat` extension; the name is not checked. The compressed track, at most 64 MiB, is read into memory when it starts. It is decoded with libvorbisfile into 250 ms buffers, and at most four buffers (one second) are queued ahead of playback. The game form refills the queue once per frame, so decoding never covers a whole track and a frame decodes at most four buffers. When tracks with different channel counts or sample rates follow each other, the next track starts after the previous one has played out.

Music plays continuously during a battle, including while the battle is paused or a dialog is open. When the game is inactive, the main loop stops and music falls silent after its queued second, then resumes where it stopped when the game is active again.

`MusicVolume` in `CFG/standalone.txt` (0-100, also the Music row of the [Settings window](SETTINGS.md)) sets the music level immediately. It is independent of `SoundVolume`: neither changes the other. The engine's existing music fade also controls standalone music. When the Terron is destroyed, or the `MUSIC 0` console command runs, music fades out over one second; `MUSIC 1` fades it back in. A new battle session starts at full fade level. The DLL build keeps host-owned music: these calls still change the host's music volume, and `MusicVolume` has no effect there.

When standalone audio starts without any battle track, for example with `sound.pkg` but no `music.pkg`, the game writes `Battle music disabled` to `test.log` once and plays effects and speech normally. A track that cannot be read or decoded, has an unsupported layout or contains no audio is reported once by name and skipped for the rest of the session. When every track fails, music stays silent. Corrupt data inside a track ends that track at the failure; holes in the Ogg stream are skipped. If the device rejects the music voice, music stops for the session with one diagnostic.

## Check sound

Run these checks in Debug and Release with cheats disabled, using the same resource set and the default Windows output device. Record the build revision, configuration, mapping/speech choice and exit status.

1. Dismiss the introductory dialog. Select a friendly robot, click its Move command button, then press `X` to cancel targeting. Check the configured UI click sound; hover sounds may be configured as silence.
2. Select the robot and issue `M`, then click open ground. Listen for the matching movement sound while it moves and for stopping when it settles. Pneumatic footsteps are configured as repeated one-shots; other chassis may loop.
3. Select a gun-carrying robot, press `Enter`, aim into empty ground and hold the left mouse button for two seconds. Release it, then press `Enter` to leave manual control. Check firing sounds and their stop behavior.
4. Use the bomber setup and detonation cases in [Standalone playtesting](PLAYTESTING.md). Check an explosion sound and subsequent selection/input.
5. Pan the camera away from an active robot and back. Check positional volume/balance without asserting an exact loudness or distance from a single listening observation.
6. Close the window while a movement/weapon sound is active. Confirm sound stops, the process exits with status zero and a subsequent launch plays normally.
7. With `DATA/music.pkg`, start a battle and listen for music. Change Music in the Settings window and confirm only music changes; change Volume and confirm music does not. Leave the battle running across a track end and through a repeated session, and note any audible gap or frame-time stall. Without `DATA/music.pkg`, confirm the battle starts with effects and `test.log` reports `Battle music disabled`.

For a silent run, inspect `test.log` for `Standalone sound initialized`, `Battle music`, missing mapping/resource names or device errors. Confirm `DATA/sound.pkg` and any selected speech package exist in the output directory; select the intended Windows audio output. `CFG/sounds.txt` is optional. To check release defaults, test a freshly extracted archive without adding that override, then supply only the desired packages. A missing speech package does not prevent non-speech effects. A successful build or muted native-device smoke test does not establish audible game behavior.
