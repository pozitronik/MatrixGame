// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "test_support.hpp"
#include <charconv>
#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>

namespace tests {

// Append already-escaped JSON text and decimal integers without locale-aware streams.
// The caller owns the buffer and decides when to clear it; nested snapshots append in place.
class SnapshotLine {
public:
    explicit SnapshotLine(std::string &buffer) : buffer_(buffer) { buffer_.reserve(4096); }
    SnapshotLine &operator<<(std::string_view value) {
        buffer_.append(value);
        return *this;
    }
    SnapshotLine &operator<<(const char *value) { return *this << std::string_view(value); }
    SnapshotLine &operator<<(char value) {
        buffer_.push_back(value);
        return *this;
    }
    SnapshotLine &operator<<(bool) = delete;
    SnapshotLine &operator<<(signed char) = delete;
    SnapshotLine &operator<<(unsigned char) = delete;
    template<std::integral T> requires (!std::same_as<T, char> && !std::same_as<T, bool> &&
                                       !std::same_as<T, signed char> && !std::same_as<T, unsigned char>)
    SnapshotLine &operator<<(T value) {
        static_assert(sizeof(T) <= sizeof(std::uint64_t));
        char digits[32];
        const auto result = std::to_chars(digits, digits + sizeof(digits), value);
        MG_CHECK(result.ec == std::errc{});
        buffer_.append(digits, result.ptr);
        return *this;
    }
    std::string_view view() const { return buffer_; }
private:
    std::string &buffer_;
};

} // namespace tests
