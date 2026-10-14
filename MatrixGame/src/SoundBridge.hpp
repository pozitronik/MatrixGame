// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include <Audio.hpp>
#include <Music.hpp>
#include <string>
#include <vector>

namespace SoundBridge {
bool available();
// Creates the standalone device when any audio package is present, with battle music when tracks exist.
void initialize_standalone(bool resources_available);
void install(std::unique_ptr<Audio::Service> service);
// Plays the tracks through the installed service's device, at the retained MusicVolume level.
void install_music(std::vector<std::wstring> tracks, Audio::Loader loader, Audio::DecoderFactory open);
void shutdown();
uint32_t create(wchar_t *name, int group, int loop);
void destroy(uint32_t id);
void play(uint32_t id);
bool playing(uint32_t id);
void volume(uint32_t id, float value);
void pan(uint32_t id, float value);
float volume(uint32_t id);
// Standalone master level for effects and speech; retained across services. Host audio ignores it.
void master_volume(float value);

// Battle music tracks under music\BATTLE, packaged and loose, in case-insensitive name order.
std::vector<std::wstring> battle_tracks();
// Starts standalone battle music on the first call and keeps its queue filled; call once per frame.
void update_music();
// Standalone MusicVolume level; retained across services. Host audio ignores it.
void music_volume(float value);
float music_volume();
// Music level the engine fades: the host's music volume, or the standalone fade level.
bool music_available();
float music_level();
void music_level(float value);
} // namespace SoundBridge
