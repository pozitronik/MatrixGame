// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Explicit little-endian byte construction for wire-format fixtures. Builders write each field with its
// specified width so fixtures do not depend on native structure sizes, packing or pointer width.
namespace wire {

class Bytes {
public:
    std::vector<uint8_t> data;

    Bytes &u8(uint8_t value) {
        data.push_back(value);
        return *this;
    }
    Bytes &u16(uint16_t value) { return u8(static_cast<uint8_t>(value)).u8(static_cast<uint8_t>(value >> 8)); }
    Bytes &u32(uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) u8(static_cast<uint8_t>(value >> shift));
        return *this;
    }
    Bytes &u64(uint64_t value) { return u32(static_cast<uint32_t>(value)).u32(static_cast<uint32_t>(value >> 32)); }
    Bytes &raw(std::span<const uint8_t> bytes) {
        data.insert(data.end(), bytes.begin(), bytes.end());
        return *this;
    }
    Bytes &raw(const Bytes &bytes) { return raw(std::span<const uint8_t>(bytes.data)); }
    Bytes &text(std::string_view value) {
        data.insert(data.end(), value.begin(), value.end());
        return *this;
    }
    // UTF-16LE code units without a terminator.
    Bytes &utf16(std::u16string_view value) {
        for (char16_t unit : value) u16(static_cast<uint16_t>(unit));
        return *this;
    }
    // UTF-16LE code units followed by one zero code unit.
    Bytes &utf16z(std::u16string_view value) { return utf16(value).u16(0); }
    // Bytes padded with zeros to a fixed field width.
    Bytes &fixed(std::string_view value, size_t width) {
        if (value.size() > width) throw std::length_error("Fixed-width field overflow");
        data.insert(data.end(), value.begin(), value.end());
        data.insert(data.end(), width - value.size(), 0);
        return *this;
    }
    void put_u32(size_t offset, uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) data.at(offset++) = static_cast<uint8_t>(value >> shift);
    }
    uint32_t size() const { return static_cast<uint32_t>(data.size()); }
};

inline uint32_t read_u32(std::span<const uint8_t> bytes, size_t offset) {
    uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) value |= uint32_t(bytes[offset++]) << shift;
    return value;
}

// RFC 1950 stream using RFC 1951 stored blocks. Fixtures built this way are independent of the
// compressor and its version; decoders must accept any valid zlib stream.
inline Bytes zlib_stored(std::span<const uint8_t> input) {
    Bytes out;
    out.u8(0x78).u8(0x01);
    size_t position = 0;
    do {
        const size_t length = std::min<size_t>(input.size() - position, 65535);
        const bool final = position + length == input.size();
        out.u8(final ? 1 : 0).u16(static_cast<uint16_t>(length)).u16(static_cast<uint16_t>(~length));
        out.raw(input.subspan(position, length));
        position += length;
    }
    while (position < input.size());
    const uLong checksum = adler32(adler32(0, nullptr, 0), input.data(), static_cast<uInt>(input.size()));
    out.u8(uint8_t(checksum >> 24)).u8(uint8_t(checksum >> 16)).u8(uint8_t(checksum >> 8)).u8(uint8_t(checksum));
    return out;
}

}  // namespace wire
