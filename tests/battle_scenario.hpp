// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "random.hpp"
#include "battle_geometry.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class CMatrixRobotAI;

namespace tests::battle {

enum class SetupFailure { None, Topology, Entities, Selection };
struct Profile {
    random::Mode mode = random::Mode::ParkMiller;
    unsigned seed = 79;
    int step_ms = 10;
    int max_frames = 3000;
    bool audio = true;
    bool initially_selected = true;
    NavigationLayout layout = NavigationLayout::CentralObstacle;
    bool unavailable_icon_targets = false;
};

// IDs belong to a session, never to a pointer, group slot or process-global UID.
using EntityId = unsigned;
struct RobotState {
    EntityId id;
    bool alive;
    float x, y;
    int group;
    int order;
    int destination_x, destination_y;
    // 0 is null; -1 is a non-null reference outside the registered live entities.
    int target = 0;
    bool target_is_null = true;
    bool in_place = false;
};

// Fixture writes are confined to setup. Commands use the human context-command
// route; advance() runs the complete production map boundary without drawing.
class Session {
public:
    explicit Session(Profile profile = {}, SetupFailure failure = SetupFailure::None);
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    void ground_command(float x, float y);
    bool target_command(EntityId id);
    bool remove(EntityId id);
    void advance();
    std::string snapshot() const;
    RobotState robot(EntityId id) const;
    bool matches(EntityId id, const CMatrixRobotAI *candidate) const;
    int frame() const;
    int time() const;
    std::uint64_t rejected_icon_targets() const;
    void check_running() const;
    std::string metadata(std::string_view fixture = "unarmed-chassis-obstacle-v1") const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tests::battle
