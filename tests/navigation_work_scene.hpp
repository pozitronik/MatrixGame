// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "NavigationWork.hpp"
#include "random.hpp"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace tests::navigation_work {

inline constexpr std::array fields{
    &NavigationWork::Counters::local_searches, &NavigationWork::Counters::local_cells_initialized,
    &NavigationWork::Counters::local_queue_entries, &NavigationWork::Counters::local_failures,
    &NavigationWork::Counters::zone_searches, &NavigationWork::Counters::robot_collision_queries,
    &NavigationWork::Counters::robot_collision_candidates, &NavigationWork::Counters::terrain_collision_queries,
    &NavigationWork::Counters::terrain_mask_cells, &NavigationWork::Counters::terrain_geometry_tests};
inline constexpr std::array names{
    "local_searches", "local_cells_initialized", "local_queue_entries", "local_failures", "zone_searches",
    "robot_collision_queries", "robot_collision_candidates", "terrain_collision_queries", "terrain_mask_cells",
    "terrain_geometry_tests"};

enum class Kind { CrowdedDestination, OpposingFlow };
const char *name(Kind kind);

struct Options {
    Kind kind;
    random::Mode mode;
    int step_ms;
};

struct Events {
    std::array<int, 8> first_movement{};
    std::array<int, 8> first_arrival{};
    std::array<int, 16> assigned_cells{};
    std::array<std::uint32_t, 16> final_position_bits{};
    std::uint32_t minimum_separation_bits{};
    std::uint64_t initial_rng_draws{}, final_rng_draws{};
    int frames{}, time_ms{};
    bool operator==(const Events &) const = default;
};

struct Report {
    Events events;
    NavigationWork::Counters total;
    // Intervals (0, 4000], (4000, 12000], (12000, 32000] ms.
    std::array<NavigationWork::Counters, 3> intervals{};
    std::vector<NavigationWork::Counters> frames;
    std::vector<std::string> states;
};

// Owns only test fixture state; movement, commands and scheduling remain production code.
class Scene {
public:
    explicit Scene(Options options);
    ~Scene();
    Scene(const Scene &) = delete;
    Scene &operator=(const Scene &) = delete;
    std::string metadata() const;
    Report run(bool collect, bool record);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

Report run(Options options, bool collect, bool record);
void print(const Report &report);

} // namespace tests::navigation_work
