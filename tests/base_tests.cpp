// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"

#include "BaseDef.hpp"
#include "CRC32.hpp"

#include <array>

namespace {

static_assert(sizeof(dword) == 4, "The engine checksum contract requires a 32-bit dword.");

void crc_empty() {
    MG_CHECK(Base::CalcCRC32(nullptr, 0) == 1);
    const auto initial = Base::CalcCRC32_Begin(nullptr, 0);
    MG_CHECK(Base::CalcCRC32_Buf(initial, nullptr, 0) == initial);
    MG_CHECK(Base::CalcCRC32_End(initial) == 0);
}

void crc_compatibility() {
    constexpr std::array<byte, 9> text = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    constexpr std::array<byte, 6> binary = {0x80, 0xff, 0x00, 0x31, 0x00, 0x7f};
    MG_CHECK(Base::CalcCRC32(text.data(), static_cast<int>(text.size())) == 0x1ED504FAUL);
    MG_CHECK(Base::CalcCRC32(binary.data(), static_cast<int>(binary.size())) == 0x9F668287UL);
}

void crc_incremental() {
    constexpr std::array<byte, 16> data = {0x00, 0x80, 0xff, 0x42, 0x00, 0x01, 0x7f, 0xfe,
                                          0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x00};
    constexpr int length = static_cast<int>(data.size());
    const auto expected = Base::CalcCRC32(data.data(), length);
    for (int split = 0; split <= length; ++split) {
        auto crc = Base::CalcCRC32_Begin(nullptr, 0);
        crc = Base::CalcCRC32_Buf(crc, data.data(), split);
        crc = Base::CalcCRC32_Buf(crc, data.data() + split, length - split);
        MG_CHECK(Base::CalcCRC32_End(crc) == expected);
    }

    auto crc = Base::CalcCRC32_Begin(nullptr, 0);
    for (const auto &value : data) {
        crc = Base::CalcCRC32_Buf(crc, &value, 1);
    }
    MG_CHECK(Base::CalcCRC32_End(crc) == expected);
}

void crc_byte_range() {
    constexpr std::array<byte, 6> payload = {0x80, 0xff, 0x00, 0x31, 0x00, 0x7f};
    std::array<byte, 8> padded = {0x12, 0x80, 0xff, 0x00, 0x31, 0x00, 0x7f, 0x34};
    const auto expected = Base::CalcCRC32(payload.data(), static_cast<int>(payload.size()));
    MG_CHECK(Base::CalcCRC32(padded.data() + 1, static_cast<int>(payload.size())) == expected);
    padded.front() = 0xfe;
    padded.back() = 0xdc;
    MG_CHECK(Base::CalcCRC32(padded.data() + 1, static_cast<int>(payload.size())) == expected);
}

void point_arithmetic() {
    Base::CPoint point(3, -4);
    point += Base::CPoint(-5, 6);
    MG_CHECK(point == Base::CPoint(-2, 2));
    point -= Base::CPoint(-5, 6);
    MG_CHECK(point == Base::CPoint(3, -4));
}

void point_distance() {
    const Base::CPoint first(-2, -3);
    const Base::CPoint second(1, 1);
    MG_CHECK(first.Dist2(second) == 25);
    MG_CHECK(second.Dist2(first) == 25);
    MG_CHECK(first.Dist2(first) == 0);
}

void rect_empty() {
    MG_CHECK(!Base::CRect(1, 2, 3, 4).IsEmpty());
    MG_CHECK(Base::CRect(1, 2, 1, 4).IsEmpty());
    MG_CHECK(Base::CRect(1, 2, 3, 2).IsEmpty());
    MG_CHECK(Base::CRect(3, 4, 1, 2).IsEmpty());
}

void rect_contains() {
    const Base::CRect rect(-2, -3, 2, 3);
    MG_CHECK(rect.IsInRect(Base::CPoint(0, 0)));
    MG_CHECK(!rect.IsInRect(Base::CPoint(-3, 0)));
    MG_CHECK(!rect.IsInRect(Base::CPoint(0, 4)));
    // The existing engine contract excludes all four edges.
    MG_CHECK(!rect.IsInRect(Base::CPoint(-2, 0)));
    MG_CHECK(!rect.IsInRect(Base::CPoint(2, 0)));
    MG_CHECK(!rect.IsInRect(Base::CPoint(0, -3)));
    MG_CHECK(!rect.IsInRect(Base::CPoint(0, 3)));
}

void rect_normalize() {
    Base::CRect rect(5, 3, -2, -4);
    rect.Normalize();
    MG_CHECK(rect.left == -2 && rect.top == -4 && rect.right == 5 && rect.bottom == 3);
    MG_CHECK(!rect.IsEmpty());
    rect.Normalize();
    MG_CHECK(rect.left == -2 && rect.top == -4 && rect.right == 5 && rect.bottom == 3);

    Base::CRect horizontal(5, -4, -2, 3);
    horizontal.Normalize();
    MG_CHECK(horizontal.left == -2 && horizontal.top == -4 && horizontal.right == 5 && horizontal.bottom == 3);
    Base::CRect vertical(-2, 3, 5, -4);
    vertical.Normalize();
    MG_CHECK(vertical.left == -2 && vertical.top == -4 && vertical.right == 5 && vertical.bottom == 3);

    Base::CRect empty(1, 2, 1, 2);
    empty.Normalize();
    MG_CHECK(empty.IsEmpty());
}

constexpr tests::Case cases[] = {
        {"base.crc.empty", crc_empty},
        {"base.crc.compatibility", crc_compatibility},
        {"base.crc.incremental", crc_incremental},
        {"base.crc.byte_range", crc_byte_range},
        {"base.point.arithmetic", point_arithmetic},
        {"base.point.distance", point_distance},
        {"base.rect.empty", rect_empty},
        {"base.rect.contains", rect_contains},
        {"base.rect.normalize", rect_normalize},
};

}  // namespace

int main(int argc, char **argv) {
    return tests::run(argc, argv, cases);
}
