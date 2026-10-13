// MatrixGame - licensed under GPLv2 or any later version.
#include "benchmark_support.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <float.h>
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace bench {

void Counters::add(std::string_view name, std::int64_t amount) {
    for (auto &entry : values) {
        if (entry.first == name) {
            entry.second += amount;
            return;
        }
    }
    values.emplace_back(std::string(name), amount);
}

std::int64_t Counters::get(std::string_view name) const {
    for (const auto &entry : values) {
        if (entry.first == name) return entry.second;
    }
    return 0;
}

namespace {

// Allocation counting replaces the executable's CRT heap imports. It needs no
// engine change and covers the engine heap, which calls calloc/realloc/free.
using MallocFunction = void *(__cdecl *)(size_t);
using CallocFunction = void *(__cdecl *)(size_t, size_t);
using ReallocFunction = void *(__cdecl *)(void *, size_t);
using FreeFunction = void(__cdecl *)(void *);

MallocFunction originalMalloc;
CallocFunction originalCalloc;
ReallocFunction originalRealloc;
FreeFunction originalFree;

std::atomic<std::int64_t> allocationCalls;
std::atomic<std::int64_t> reallocationCalls;
std::atomic<std::int64_t> releaseCalls;
std::atomic<std::int64_t> requestedBytes;

void *__cdecl countingMalloc(size_t size) {
    allocationCalls.fetch_add(1, std::memory_order_relaxed);
    requestedBytes.fetch_add(static_cast<std::int64_t>(size), std::memory_order_relaxed);
    return originalMalloc(size);
}

void *__cdecl countingCalloc(size_t count, size_t size) {
    allocationCalls.fetch_add(1, std::memory_order_relaxed);
    requestedBytes.fetch_add(static_cast<std::int64_t>(count * size), std::memory_order_relaxed);
    return originalCalloc(count, size);
}

void *__cdecl countingRealloc(void *block, size_t size) {
    // realloc(nullptr, n) allocates and realloc(p, 0) releases, as in the CRT.
    if (!block) allocationCalls.fetch_add(1, std::memory_order_relaxed);
    else if (!size) releaseCalls.fetch_add(1, std::memory_order_relaxed);
    else reallocationCalls.fetch_add(1, std::memory_order_relaxed);
    requestedBytes.fetch_add(static_cast<std::int64_t>(size), std::memory_order_relaxed);
    return originalRealloc(block, size);
}

void __cdecl countingFree(void *block) {
    if (block) releaseCalls.fetch_add(1, std::memory_order_relaxed);
    originalFree(block);
}

struct AllocationSnapshot {
    std::int64_t allocations = 0;
    std::int64_t reallocations = 0;
    std::int64_t frees = 0;
    std::int64_t bytes = 0;

    static AllocationSnapshot now() {
        return {allocationCalls.load(), reallocationCalls.load(), releaseCalls.load(), requestedBytes.load()};
    }
    AllocationSnapshot operator+(const AllocationSnapshot &other) const {
        return {allocations + other.allocations, reallocations + other.reallocations, frees + other.frees,
                bytes + other.bytes};
    }
    AllocationSnapshot operator-(const AllocationSnapshot &start) const {
        return {allocations - start.allocations, reallocations - start.reallocations, frees - start.frees,
                bytes - start.bytes};
    }
    bool operator==(const AllocationSnapshot &) const = default;
};

bool replaceImport(void **slot, void *replacement) {
    DWORD previous = 0;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &previous)) return false;
    *slot = replacement;
    VirtualProtect(slot, sizeof(void *), previous, &previous);
    return true;
}

template<class Function>
bool remember(Function &original, void *current) {
    auto candidate = reinterpret_cast<Function>(current);
    if (original && original != candidate) return false;
    original = candidate;
    return true;
}

// Returns a description of the counted scope, or an empty string when unavailable.
std::string installAllocationCounters() {
    auto *base = reinterpret_cast<BYTE *>(GetModuleHandleW(nullptr));
    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    auto *headers = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    const auto &directory = headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress) return {};

    struct Slot {
        void **address;
        void *replacement;
        void *original;
    };
    std::vector<Slot> slots;
    bool consistent = true;
    for (auto *module = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + directory.VirtualAddress); module->Name;
         ++module) {
        if (!module->OriginalFirstThunk) continue;
        auto *names = reinterpret_cast<IMAGE_THUNK_DATA *>(base + module->OriginalFirstThunk);
        auto *addresses = reinterpret_cast<IMAGE_THUNK_DATA *>(base + module->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addresses) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            const auto *import = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData);
            const std::string_view name(reinterpret_cast<const char *>(import->Name));
            auto *address = reinterpret_cast<void **>(&addresses->u1.Function);
            if (name == "malloc") {
                consistent = consistent && remember(originalMalloc, *address);
                slots.push_back({address, reinterpret_cast<void *>(&countingMalloc), *address});
            } else if (name == "calloc") {
                consistent = consistent && remember(originalCalloc, *address);
                slots.push_back({address, reinterpret_cast<void *>(&countingCalloc), *address});
            } else if (name == "realloc") {
                consistent = consistent && remember(originalRealloc, *address);
                slots.push_back({address, reinterpret_cast<void *>(&countingRealloc), *address});
            } else if (name == "free") {
                consistent = consistent && remember(originalFree, *address);
                slots.push_back({address, reinterpret_cast<void *>(&countingFree), *address});
            }
        }
    }
    if (!consistent || !originalMalloc || !originalCalloc || !originalRealloc || !originalFree) return {};
    for (size_t i = 0; i < slots.size(); ++i) {
        if (!replaceImport(slots[i].address, slots[i].replacement)) {
            // Leave the process with its original imports rather than partial counting.
            while (i-- > 0) replaceImport(slots[i].address, slots[i].original);
            return {};
        }
    }
    return "executable CRT imports: malloc, calloc, realloc, free";
}

struct Options {
    std::vector<std::string> selected;
    int warmup = 3;
    int repetitions = 15;
    int iterations = 0;
    int validationDelayMicroseconds = 0;
    bool smoke = false;
    bool list = false;
    std::string output;
};

Options parse(int argc, char **argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        auto text = [&]() -> const char * {
            if (i + 1 >= argc) throw std::invalid_argument("Missing value for " + std::string(argument));
            return argv[++i];
        };
        auto count = [&](bool allowZero) {
            const char *value = text();
            char *end = nullptr;
            const long parsed = std::strtol(value, &end, 10);
            if (!*value || *end || parsed < (allowZero ? 0 : 1) || parsed > 1000000) {
                throw std::invalid_argument("Invalid value for " + std::string(argument) + ": " + value);
            }
            return static_cast<int>(parsed);
        };
        if (argument == "--list") options.list = true;
        else if (argument == "--smoke") options.smoke = true;
        else if (argument == "--workload") options.selected.emplace_back(text());
        else if (argument == "--warmup") options.warmup = count(true);
        else if (argument == "--repetitions") options.repetitions = count(false);
        else if (argument == "--iterations") options.iterations = count(false);
        else if (argument == "--validation-delay-us") options.validationDelayMicroseconds = count(true);
        else if (argument == "--output") options.output = text();
        else throw std::invalid_argument("Unknown argument: " + std::string(argument));
    }
    if (options.smoke) {
        options.warmup = 1;
        options.repetitions = 3;
        if (!options.iterations) options.iterations = 1;
    }
    return options;
}

std::string escape(std::string_view text) {
    std::string result;
    for (const char c : text) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(c));
                    result += buffer;
                } else {
                    result += c;
                }
        }
    }
    return result;
}

std::string jsonString(std::string_view text) { return "\"" + escape(text) + "\""; }

// Linear interpolation between closest ranks of a sorted sample.
double quantile(const std::vector<double> &sorted, double q) {
    if (sorted.empty()) return 0.0;
    const double position = q * static_cast<double>(sorted.size() - 1);
    const auto lower = static_cast<size_t>(std::floor(position));
    const auto upper = static_cast<size_t>(std::ceil(position));
    return sorted[lower] + (sorted[upper] - sorted[lower]) * (position - static_cast<double>(lower));
}

struct Statistics {
    double min, p10, median, p90, max, mean, mad;
};

Statistics describe(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    Statistics result{};
    result.min = values.front();
    result.max = values.back();
    result.p10 = quantile(values, 0.10);
    result.median = quantile(values, 0.50);
    result.p90 = quantile(values, 0.90);
    double sum = 0.0;
    for (double value : values) sum += value;
    result.mean = sum / static_cast<double>(values.size());
    std::vector<double> deviations;
    for (double value : values) deviations.push_back(std::fabs(value - result.median));
    std::sort(deviations.begin(), deviations.end());
    result.mad = quantile(deviations, 0.50);
    return result;
}

std::string number(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    return buffer;
}

std::string compilerIdentity() {
#if defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_FULL_VER);
#elif defined(__clang__)
    return std::string("Clang ") + __clang_version__;
#elif defined(__GNUC__)
    return std::string("GCC ") + __VERSION__;
#else
    return "unknown";
#endif
}

std::string configurationIdentity() {
#if defined(_DEBUG)
    return "Debug";
#elif defined(ASSERT_OFF)
    return "Release";
#else
    return "unknown";
#endif
}

std::string processorIdentity() {
    unsigned registers[12]{};
#if defined(_MSC_VER)
    int info[4]{};
    __cpuid(info, 0x80000000);
    if (static_cast<unsigned>(info[0]) < 0x80000004) return "unknown";
    for (unsigned leaf = 0; leaf < 3; ++leaf) {
        __cpuid(info, static_cast<int>(0x80000002 + leaf));
        std::memcpy(registers + leaf * 4, info, sizeof(info));
    }
#else
    unsigned a, b, c, d;
    if (!__get_cpuid(0x80000000, &a, &b, &c, &d) || a < 0x80000004) return "unknown";
    for (unsigned leaf = 0; leaf < 3; ++leaf) {
        __get_cpuid(0x80000002 + leaf, &a, &b, &c, &d);
        registers[leaf * 4] = a;
        registers[leaf * 4 + 1] = b;
        registers[leaf * 4 + 2] = c;
        registers[leaf * 4 + 3] = d;
    }
#endif
    std::string brand(reinterpret_cast<const char *>(registers), sizeof(registers));
    brand = brand.c_str();
    const auto first = brand.find_first_not_of(' ');
    return first == std::string::npos ? "unknown" : brand.substr(first);
}

struct Result {
    const Workload *workload;
    int iterations;
    std::vector<double> samples;
    Counters work;
    Counters inspection;
    std::vector<AllocationSnapshot> allocations;
};

void spin(std::chrono::microseconds duration) {
    const auto end = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < end) {
    }
}

Result measure(const Workload &workload, const Options &options) {
    Result result{&workload, options.iterations ? options.iterations : workload.defaultIterations, {}, {}, {}, {}};

    // Untimed reference pass: every iteration must repeat the work of the first one,
    // and every later sample must repeat the total.
    {
        auto session = workload.create();
        Counters first;
        for (int i = 0; i < result.iterations; ++i) {
            Counters iteration;
            session->prepare();
            session->run(iteration);
            if (i == 0) first = iteration;
            else if (!(iteration == first)) {
                throw std::runtime_error("Workload " + workload.name + " performed different work in iteration " +
                                         std::to_string(i));
            }
            for (const auto &[name, value] : iteration.values) result.work.add(name, value);
        }
        session->inspect(result.inspection);
    }

    for (int sample = 0; sample < options.warmup + options.repetitions; ++sample) {
        auto session = workload.create();
        // Pre-create every counter so the timed region does not allocate names.
        Counters work = result.work;
        for (auto &entry : work.values) entry.second = 0;
        std::chrono::steady_clock::duration elapsed{};
        AllocationSnapshot allocations;
        for (int i = 0; i < result.iterations; ++i) {
            session->prepare();
            const auto allocationStart = AllocationSnapshot::now();
            const auto start = std::chrono::steady_clock::now();
            session->run(work);
            if (i == 0 && options.validationDelayMicroseconds) {
                spin(std::chrono::microseconds(options.validationDelayMicroseconds));
            }
            elapsed += std::chrono::steady_clock::now() - start;
            allocations = allocations + (AllocationSnapshot::now() - allocationStart);
        }
        session.reset();
        if (!(work == result.work)) {
            throw std::runtime_error("Workload " + workload.name + " performed different work in sample " +
                                     std::to_string(sample));
        }
        if (sample < options.warmup) continue;
        result.samples.push_back(std::chrono::duration<double, std::nano>(elapsed).count());
        result.allocations.push_back(allocations);
    }
    return result;
}

void writeCounters(std::ostream &out, const Counters &counters) {
    out << "{";
    for (size_t i = 0; i < counters.values.size(); ++i) {
        out << (i ? ", " : "") << jsonString(counters.values[i].first) << ": " << counters.values[i].second;
    }
    out << "}";
}

void writeReport(std::ostream &out, const Options &options, const std::vector<Result> &results,
                 const std::string &allocationScope) {
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    SYSTEM_INFO system{};
    GetNativeSystemInfo(&system);
    out << "{\n  \"schema\": \"matrixgame-benchmark/1\",\n";
    out << "  \"build\": {\"compiler\": " << jsonString(compilerIdentity())
        << ", \"configuration\": " << jsonString(configurationIdentity()) << ", \"architecture\": "
        << jsonString(sizeof(void *) == 4 ? "x86" : "x64") << ", \"engine_heap_tracking\": "
#if defined(_DEBUG) || defined(FORCE_ENABLE_MEM_SPY)
        << "true"
#else
        << "false"
#endif
        << "},\n";
    out << "  \"host\": {\"processor\": " << jsonString(processorIdentity())
        << ", \"logical_processors\": " << system.dwNumberOfProcessors
        << ", \"timer_frequency_hz\": " << frequency.QuadPart << "},\n";
    out << "  \"runner\": {\"warmup\": " << options.warmup << ", \"repetitions\": " << options.repetitions
        << ", \"smoke\": " << (options.smoke ? "true" : "false")
        << ", \"validation_delay_us\": " << options.validationDelayMicroseconds
        << ", \"x87_control_word\": " << jsonString([] {
               char buffer[16];
               std::snprintf(buffer, sizeof(buffer), "0x%04x", _control87(0, 0) & 0xffff);
               return std::string(buffer);
           }())
        << ", \"timer\": \"std::chrono::steady_clock\", \"sample_unit\": \"ns\", \"allocation_scope\": "
        << (allocationScope.empty() ? "null" : jsonString(allocationScope)) << "},\n";
    out << "  \"workloads\": [\n";
    for (size_t w = 0; w < results.size(); ++w) {
        const auto &result = results[w];
        const auto stats = describe(result.samples);
        out << "    {\n      \"name\": " << jsonString(result.workload->name)
            << ",\n      \"description\": " << jsonString(result.workload->description)
            << ",\n      \"parameters\": {";
        for (size_t i = 0; i < result.workload->parameters.size(); ++i) {
            const auto &parameter = result.workload->parameters[i];
            out << (i ? ", " : "") << jsonString(parameter.name) << ": " << jsonString(parameter.value);
        }
        out << "},\n      \"iterations_per_sample\": " << result.iterations << ",\n      \"work_per_sample\": ";
        writeCounters(out, result.work);
        out << ",\n      \"inspection\": ";
        writeCounters(out, result.inspection);
        out << ",\n      \"samples_ns\": [";
        for (size_t i = 0; i < result.samples.size(); ++i) out << (i ? ", " : "") << number(result.samples[i]);
        out << "],\n      \"statistics_ns\": {\"min\": " << number(stats.min) << ", \"p10\": " << number(stats.p10)
            << ", \"median\": " << number(stats.median) << ", \"p90\": " << number(stats.p90)
            << ", \"max\": " << number(stats.max) << ", \"mean\": " << number(stats.mean)
            << ", \"mad\": " << number(stats.mad) << "},\n      \"allocations_per_sample\": ";
        if (allocationScope.empty()) {
            out << "null";
        } else {
            const auto &first = result.allocations.front();
            const bool stable = std::all_of(result.allocations.begin(), result.allocations.end(),
                                            [&](const AllocationSnapshot &value) { return value == first; });
            out << "{\"stable\": " << (stable ? "true" : "false") << ", \"allocations\": [";
            for (size_t i = 0; i < result.allocations.size(); ++i) {
                out << (i ? ", " : "") << result.allocations[i].allocations;
            }
            out << "], \"reallocations\": [";
            for (size_t i = 0; i < result.allocations.size(); ++i) {
                out << (i ? ", " : "") << result.allocations[i].reallocations;
            }
            out << "], \"frees\": [";
            for (size_t i = 0; i < result.allocations.size(); ++i) {
                out << (i ? ", " : "") << result.allocations[i].frees;
            }
            out << "], \"requested_bytes\": [";
            for (size_t i = 0; i < result.allocations.size(); ++i) {
                out << (i ? ", " : "") << result.allocations[i].bytes;
            }
            out << "]}";
        }
        out << "\n    }" << (w + 1 < results.size() ? "," : "") << "\n";
    }
    out << "  ]\n}\n";
}

void printSummary(const Result &result) {
    const auto stats = describe(result.samples);
    const double scale = 1.0e6;
    std::printf("%s: %d iteration(s)/sample, %zu samples: median %.3f ms, p10 %.3f, p90 %.3f, MAD %.3f ms (%.1f%%)\n",
                result.workload->name.c_str(), result.iterations, result.samples.size(), stats.median / scale,
                stats.p10 / scale, stats.p90 / scale, stats.mad / scale,
                stats.median > 0 ? 100.0 * stats.mad / stats.median : 0.0);
    std::printf("  work:");
    for (const auto &[name, value] : result.work.values) {
        std::printf(" %s=%lld", name.c_str(), static_cast<long long>(value));
    }
    std::printf("\n");
    if (!result.inspection.values.empty()) {
        std::printf("  inspection:");
        for (const auto &[name, value] : result.inspection.values) {
            std::printf(" %s=%lld", name.c_str(), static_cast<long long>(value));
        }
        std::printf("\n");
    }
    if (!result.allocations.empty()) {
        const auto &first = result.allocations.front();
        std::printf("  allocations/sample: %lld, reallocations %lld, frees %lld, requested bytes %lld\n",
                    static_cast<long long>(first.allocations), static_cast<long long>(first.reallocations),
                    static_cast<long long>(first.frees), static_cast<long long>(first.bytes));
    }
}

}  // namespace

int run(int argc, char **argv, std::span<const Workload> workloads) {
    Options options;
    try {
        options = parse(argc, argv);
    }
    catch (const std::exception &error) {
        std::cerr << error.what() << "\nUsage: " << argv[0]
                  << " [--list] [--workload NAME]... [--warmup N] [--repetitions N] [--iterations N]"
                     " [--validation-delay-us N] [--output FILE] [--smoke]\n";
        return 2;
    }
    if (options.list) {
        for (const auto &workload : workloads) std::cout << workload.name << ": " << workload.description << '\n';
        return EXIT_SUCCESS;
    }

    std::vector<const Workload *> chosen;
    for (const auto &name : options.selected) {
        auto found = std::find_if(workloads.begin(), workloads.end(),
                                  [&](const Workload &workload) { return workload.name == name; });
        if (found == workloads.end()) {
            std::cerr << "Unknown workload: " << name << "\nUse --list to see available workloads.\n";
            return 2;
        }
        chosen.push_back(&*found);
    }
    if (chosen.empty()) {
        for (const auto &workload : workloads) chosen.push_back(&workload);
    }

    const auto allocationScope = installAllocationCounters();
    std::vector<Result> results;
    try {
        for (const auto *workload : chosen) {
            results.push_back(measure(*workload, options));
            printSummary(results.back());
        }
    }
    catch (const std::exception &error) {
        std::cerr << "Benchmark failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    catch (...) {
        // Engine diagnostics throw types outside std::exception; never let them reach terminate.
        std::cerr << "Benchmark failed: unexpected engine exception\n";
        return EXIT_FAILURE;
    }
    if (allocationScope.empty()) std::cout << "Allocation counters are unavailable in this build.\n";

    if (!options.output.empty()) {
        std::ofstream file(options.output, std::ios::binary | std::ios::trunc);
        writeReport(file, options, results, allocationScope);
        file.close();
        if (!file) {
            std::cerr << "Cannot write benchmark report: " << options.output << '\n';
            return EXIT_FAILURE;
        }
        std::cout << "Benchmark report: " << options.output << '\n';
    }
    return EXIT_SUCCESS;
}

}  // namespace bench
