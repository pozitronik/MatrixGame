// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bench {

// Ordered name/value pairs; equal work must produce equal values in every sample.
struct Counters {
    std::vector<std::pair<std::string, std::int64_t>> values;

    // Looks up names without allocating once a counter exists.
    void add(std::string_view name, std::int64_t amount);
    std::int64_t get(std::string_view name) const;
    bool operator==(const Counters &other) const = default;
};

// One prepared fixture. Construction and destruction are untimed.
class Session {
public:
    virtual ~Session() = default;
    // Perform the timed work once. Counters must depend only on the work done.
    virtual void run(Counters &work) = 0;
    // Untimed diagnostic counters gathered once after a verification run.
    virtual void inspect(Counters &) {}
};

struct Parameter {
    std::string name;
    std::string value;
};

struct Workload {
    std::string name;
    std::string description;
    std::vector<Parameter> parameters;
    int defaultIterations = 1;
    // Seed, build and return a fresh session; called for every sample.
    std::function<std::unique_ptr<Session>()> create;
};

int run(int argc, char **argv, std::span<const Workload> workloads);

}  // namespace bench
