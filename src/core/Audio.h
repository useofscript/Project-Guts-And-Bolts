#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

// Sound for the whole engine. Built-in effects are generated in code (so no
// sound files are needed); games can also play .wav / .mp3 / .flac files from
// the games folder. If the computer has no sound device, everything quietly
// does nothing.
namespace Audio {

// Names of the built-in sounds: "jump", "coin", "oof", "explosion", ...
const std::vector<std::string>& builtinNames();

void init();
void shutdown();
void update();                                   // clean up finished sounds

// Play a built-in name or a file path. `position` null = not 3D (UI / music).
// Returns a handle (0 = couldn't play).
int  play(const std::string& soundId, float volume = 0.6f, float pitch = 1.0f,
          bool loop = false, const glm::vec3* position = nullptr);
void stop(int handle);
void stopAll();
bool isPlaying(int handle);
void setVolume(int handle, float volume);
void setPitch(int handle, float pitch);
void setPosition(int handle, const glm::vec3& position);
void setListener(const glm::vec3& position, const glm::vec3& forward);
void setMasterVolume(float v);                   // 0..1

// The sound engine itself (a ma_engine*), for voice chat. Null with no sound device.
void* engineHandle();

} // namespace Audio
