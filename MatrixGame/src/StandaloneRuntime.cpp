// MatrixGame - licensed under GPLv2 or any later version.
#include "StandaloneRuntime.hpp"

#include "MatrixConfig.hpp"
#include "MatrixGameDll.hpp"
#include "SoundBridge.hpp"
#include "WindowDisplay.hpp"
#include "GroupCounts.hpp"

namespace StandaloneSettings {
namespace {
Values saved;
Values started;
float camera_speed = 0.0f;  // Scroll speed configured by the game data.

void apply_immediate(const Values &values) {
    g_Config.m_ShowStencilShadows = values.stencil_shadows;
    g_Config.m_ShowProjShadows = values.projected_shadows;
    g_Config.m_CamMoveSpeed = camera_speed * static_cast<float>(values.camera_speed) / 100.0f;
    SoundBridge::master_volume(static_cast<float>(values.sound_volume) / 100.0f);
    GroupCounts::set_original(values.group_counts == GroupCountMode::Original);
}
}  // namespace

void apply_startup(const Values &values, SRobotsSettings &settings) {
    settings.m_ShowStencilShadows = values.stencil_shadows;
    settings.m_ShowProjShadows = values.projected_shadows;
    settings.m_SkyBox = static_cast<byte>(values.skybox);
    settings.m_RobotShadow = values.robot_shadows ? 1 : 0;
}

void initialize_runtime(const Values &values) {
    camera_speed = g_Config.m_CamMoveSpeed;  // ReadParams restores the data value for each session.
    saved = started = values;
    apply_immediate(values);
}

void apply_runtime(const Values &values) {
    apply_immediate(values);
    if (Display::enabled()) Display::request(values.display.mode, values.display.fullscreen);
    saved = values;
}

const Values &current() { return saved; }

bool restart_pending(const Values &values) {
    return values.display.width != started.display.width || values.display.height != started.display.height ||
           values.random != started.random || values.skybox != started.skybox ||
           values.robot_shadows != started.robot_shadows;
}

}  // namespace StandaloneSettings
