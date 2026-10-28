// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "battle_scenario.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>

class CMatrixFlyer;
class CMatrixRobotAI;
class CMatrixMapStatic;
struct SObjectCore;

namespace tests::carrier {
struct Progress {
    int release, landed, retired, falling_samples;
};
struct Pair {
    CMatrixFlyer *carrier;
    CMatrixRobotAI *cargo;
    SObjectCore *carrier_core, *cargo_core;
    int requested_place;
    std::array<int, 2> destination;
    std::optional<std::array<float, 3>> carrier_pose;
    std::optional<float> cargo_height;
    std::optional<Progress> progress;
};
struct Approach {
    std::array<float, 3> minimum, maximum;
    float roof, maximum_z, maximum_surface;
    int inside, first_inside, last_inside;
    std::optional<float> sampled_surface, hit_roof;
};
struct Frame {
    std::optional<std::array<int, 4>> resources;
    std::optional<Approach> approach;
    std::string original;
    CMatrixMapStatic *obstacle = nullptr;
};

// Pose values come from the driver's existing samples, never another refresh.
std::string observe(const battle::Session &session, std::span<const Pair> pairs, const Frame &frame = {});
void invalidate(const char *family, std::optional<random::Mode> mode = {}, int step = 0);
void complete_after_cleanup();

class Recording {
public:
    Recording(const battle::Session &session, random::Mode mode, int step,
              const char *family, int variant, int replay = 0);
    void command(const char *name, std::uint64_t before, const std::string &fields);
    void request(std::uint64_t before, float angle, std::array<float, 2> landing, int place,
                 std::array<int, 2> destination);
    void maintenance(std::uint64_t before, bool retry, std::array<int, 2> places,
                     std::optional<std::array<int, 4>> resources_before = {},
                     std::optional<std::array<int, 4>> resources_after = {}, std::optional<int> cooldown = {});
    void damage(std::uint64_t before, bool repair, std::array<float, 3> position, float health_before,
                std::optional<float> health_after, bool cargo_marked);
    void snapshot(const std::string &observation);
    void finish(std::span<const std::array<float, 3>> standing_poses = {}, std::optional<float> separation = {});

private:
    const battle::Session &session_;
    std::filesystem::path path_;
    std::ofstream output_;
    std::uint64_t hash_ = 14695981039346656037ull;
    unsigned observations_{};
};
} // namespace tests::carrier
