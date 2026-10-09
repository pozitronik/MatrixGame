// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include <Audio.hpp>

namespace SoundBridge {
bool available();
void initialize_standalone();
void install(std::unique_ptr<Audio::Service> service);
void shutdown();
uint32_t create(wchar_t *name, int group, int loop);
void destroy(uint32_t id);
void play(uint32_t id);
bool playing(uint32_t id);
void volume(uint32_t id, float value);
void pan(uint32_t id, float value);
float volume(uint32_t id);
} // namespace SoundBridge
