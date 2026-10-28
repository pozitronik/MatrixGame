// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "random.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class CMatrixRobotAI;
namespace tests::battle { class Session; }

namespace tests::manual {

// A borrowed scene view. Resolve the original UID before reading any live-only unit fields.
class View {
public:
    View(battle::Session &session, CMatrixRobotAI &actor);
    ~View();
    void aim_assigned();
    std::string snapshot(const char *phase) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

void invalidate_original(int chassis);
void invalidate_enemy(int chassis, random::Mode mode, int step, bool moving);

class Recording {
public:
    Recording(battle::Session &session, View &view, int chassis, random::Mode mode, int step,
              bool moving, int continuation, int replay);
    ~Recording();
    void state(const char *phase);
    void command(const char *name, const char *kind, std::uint64_t draws_before,
                 int code = -1, int value = -1, int x = -1, int y = -1);
    void finish(const std::vector<std::string> &existing_frames, std::size_t prefix_frames,
                int exit_time, int retry_time);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Called after the executable's required case, lifetime and base-cleanup checks.
void complete_after_cleanup();

} // namespace tests::manual
