// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "battle_scenario.hpp"
#include "MatrixRobot.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace tests::target_recording {
struct Identity {
    random::Mode mode;
    int step;
    bool button;
    int target; // -1: repair patient; 0/1/2: friendly/enemy/ground bomb.
    int ending; // 0/1/2: completion/removal/Stop.
};
struct Observation {
    CMatrixRobotAI *actor = nullptr;
    CMatrixRobotAI *peer = nullptr;
    SObjectCore *actor_core = nullptr;
    int peer_id = 0;
    int heals = 0, damage_events = 0, interrupted_at = 0;
    int requested_at = 0, dying_at = 0, retired_at = 0;
    CPoint destination{-1, -1}, stop_destination{-1, -1};
};

void prepare(int target, int ending = -1);
void complete_after_cleanup();

class Recording {
    Identity identity_;
    std::filesystem::path path_;
    std::ofstream stream_;
public:
    explicit Recording(Identity identity);
    void header(const battle::Session &session);
    void command(const battle::Session &session, const char *name, const char *input, int key, int target,
                 std::array<int, 2> point, std::uint64_t before, bool accepted);
    void snapshot(const battle::Session &session, const char *phase, const Observation &observation,
                  std::string_view state = {});
    void finish(const battle::Session &session, const Observation &observation);
};
} // namespace tests::target_recording
