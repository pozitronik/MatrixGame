// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <cstdint>

namespace NavigationWork {

// Caller-owned observations; gameplay never reads these values. Use on the
// simulation thread, just like the observed navigation and robot operations.
struct Counters {
    std::uint64_t local_searches{};
    std::uint64_t local_cells_initialized{};
    std::uint64_t local_queue_entries{};
    std::uint64_t local_failures{};
    std::uint64_t zone_searches{};
    std::uint64_t robot_collision_queries{};
    std::uint64_t robot_collision_candidates{};
    std::uint64_t terrain_collision_queries{};
    std::uint64_t terrain_mask_cells{};
    std::uint64_t terrain_geometry_tests{};

    bool operator==(const Counters &) const = default;
};

// Starts with the supplied values; nesting temporarily redirects collection and
// restores the previous destination. The caller keeps the destination alive.
class Capture {
public:
    explicit Capture(Counters &destination) noexcept;
    ~Capture() noexcept;
    Capture(const Capture &) = delete;
    Capture &operator=(const Capture &) = delete;
    Capture(Capture &&) = delete;
    Capture &operator=(Capture &&) = delete;

private:
    Counters *previous_;
};

// Null outside a capture. No allocation, lazy game-state access, timer or RNG.
Counters *active() noexcept;

}  // namespace NavigationWork
