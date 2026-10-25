// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "battle_scenario.hpp"
#include <array>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

struct SObjectCore;
class CMatrixMapStatic;

namespace tests::battle::bomb_escape {
struct Milestones {
    int safe = 0, refuge = 0, first_return = 0, second_escape = 0, cleared = 0;
    int damage = 0, arrived = 0, retired = 0;
    float maximum_distance = 0;
    std::optional<float> bomb_distance, refuge_distance;
};

void invalidate(std::string_view refuge);
void complete_after_cleanup();

class Recording {
public:
    Recording(Session &session, std::string_view refuge, bool detonate, bool original, random::Mode mode,
              int step, int replay, SObjectCore *bomber_core);
    void state(const char *phase, std::string_view raw, const Milestones &milestones);
    void command(const char *name, std::uint64_t before);
    void finish();

private:
    Session &session_;
    SObjectCore *bomber_core_;
    CMatrixMapStatic *bomber_identity_;
    std::string path_;
    std::array<char, 65536> buffer_{};
    std::ofstream stream_;
    bool finished_ = false;
};
} // namespace tests::battle::bomb_escape
