// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "battle_scenario.hpp"
#include <filesystem>
#include <fstream>
#include <string_view>

struct SObjectCore;
class CMatrixMapStatic;
namespace Base {
class CPoint;
}

namespace tests::war_recording {

enum class Change { None, Retirement, Move };
enum class Role { First, Replay, Intact };
struct Identity {
    random::Mode mode;
    int step;
    Change caller;
    Role role;
};

void invalidate(random::Mode mode, int step, Change caller);
int object_id(const battle::Session &session, const CMatrixMapStatic *object);
void complete();

class Recording {
public:
    explicit Recording(Identity identity);
    void begin(const battle::Session &session, const SObjectCore &core);
    void command(const battle::Session &session, const SObjectCore &core, std::string_view name,
                 std::uint64_t before, int pulses, float health, Base::CPoint firing_place);
    void tick(const battle::Session &session, const SObjectCore &core, std::string_view original,
              int pulses, float health, Base::CPoint firing_place, int firing_target);
    void finish(const battle::Session &session, std::string_view milestones);

private:
    void snapshot(const battle::Session &session, const SObjectCore &core, std::string_view phase,
                  int pulses, float health, Base::CPoint firing_place, int firing_target,
                  std::string_view original = {});
    Identity identity_;
    std::filesystem::path path_;
    std::ofstream file_;
};

} // namespace tests::war_recording
