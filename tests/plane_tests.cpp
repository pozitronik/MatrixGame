// MatrixGame - licensed under GPLv2 or any later version.
#include "Math3D.hpp"
#include "test_support.hpp"
#include "stupid_logger.hpp"
#include <cmath>
#include <cstddef>
#include <type_traits>

logger_type lgr{std::cerr};

namespace {
using tests::Case;
void check_close(float actual, float expected) { MG_CHECK(std::abs(actual - expected) < .0001f); }
SPlane sentinel() {
    SPlane plane;
    plane.dxplane = D3DXPLANE(31, 37, 41, 43);
    plane.norm = D3DXVECTOR3(47, 53, 59);
    plane.dist = 61;
    plane.signbits = 7;
    return plane;
}
void coefficients(const SPlane &plane, float x, float y, float z, float d) {
    check_close(plane.dxplane.a, x);
    check_close(plane.dxplane.b, y);
    check_close(plane.dxplane.c, z);
    check_close(plane.dxplane.d, d);
    check_close(plane.norm.x, x);
    check_close(plane.norm.y, y);
    check_close(plane.norm.z, z);
    check_close(plane.dist, d);
    MG_CHECK(plane.signbits == ((x < 0 ? 1 : 0) | (y < 0 ? 2 : 0) | (z < 0 ? 4 : 0)));
}
void point_normal() {
    auto plane = sentinel();
    SPlane::BuildFromPointNormal(plane, {0, 0, 2}, {0, 0, 1});
    coefficients(plane, 0, 0, 1, -2);
    check_close(plane.Distance({0, 0, 10}), 8);
    float t = -1;
    MG_CHECK(plane.FindIntersect({0, 0, 10}, {0, 0, -1}, t));
    check_close(t, 8);
}
void points_orientation() {
    auto plane = sentinel();
    for (bool reverse : {false, true}) {
        const D3DXVECTOR3 a(0, 0, 2), b(1, 0, 2), c(0, 1, 2);
        SPlane::BuildFromPoints(plane, a, reverse ? c : b, reverse ? b : c);
        const float sign = reverse ? -1.f : 1.f;
        coefficients(plane, 0, 0, sign, -2 * sign);
        check_close(plane.Distance({0, 0, 10}), 8 * sign);
        float t = -1;
        MG_CHECK(plane.FindIntersect({0, 0, 10}, {0, 0, -1}, t));
        check_close(t, 8);
    }
}
void distance_and_sides() {
    auto plane = sentinel();
    SPlane::BuildFromPointNormal(plane, {3, 0, 0}, {-1, 0, 0});
    coefficients(plane, -1, 0, 0, 3);
    check_close(plane.CalcPointDist({2, 99, 99}), -2);
    check_close(plane.Distance({2, 99, 99}), 1);
    check_close(plane.Distance({3, 99, 99}), 0);
    check_close(plane.Distance({4, 99, 99}), -1);
    MG_CHECK(plane.IsOnSide({2, 99, 99}));
    MG_CHECK(plane.IsOnSide({3, 99, 99}));
    MG_CHECK(!plane.IsOnSide({4, 99, 99}));
}
void box_octants() {
    for (unsigned signs = 0; signs < 8; ++signs) {
        const D3DXVECTOR3 normal(signs & 1 ? -1.f : 1.f, signs & 2 ? -1.f : 1.f, signs & 4 ? -1.f : 1.f);
        auto plane = sentinel();
        SPlane::BuildFromPointNormal(plane, {2, 4, 8}, normal);
        MG_CHECK(plane.signbits == signs);
        const float d = -(2 * normal.x + 4 * normal.y + 8 * normal.z);
        coefficients(plane, normal.x, normal.y, normal.z, d);
        for (float offset : {-20.f, 0.f, 20.f}) {
            const D3DXVECTOR3 low(1 + offset * normal.x, 3 + offset * normal.y, 7 + offset * normal.z);
            const D3DXVECTOR3 high(low.x + 2, low.y + 2, low.z + 2);
            unsigned expected = 0;
            for (unsigned corner = 0; corner < 8; ++corner) {
                const float x = corner & 1 ? high.x : low.x;
                const float y = corner & 2 ? high.y : low.y;
                const float z = corner & 4 ? high.z : low.z;
                expected |= x * normal.x + y * normal.y + z * normal.z + d >= 0 ? 1 : 2;
            }
            MG_CHECK(plane.BoxSide(low, high) == expected);
        }
    }
    auto plane = sentinel();
    SPlane::BuildFromPointNormal(plane, {0, 0, 2}, {0, 0, 1});
    MG_CHECK(plane.BoxSide({0, 0, 2}, {1, 1, 3}) == 1);
    MG_CHECK(plane.BoxSide({0, 0, 1}, {1, 1, 2}) == 3);
}
void intersection_contract() {
    auto plane = sentinel();
    SPlane::BuildFromPointNormal(plane, {0, 0, 2}, {0, 0, 1});
    float t = 17;
    MG_CHECK(!plane.FindIntersect({0, 0, 10}, {1, 0, 0}, t));
    check_close(t, 17);
    MG_CHECK(!plane.FindIntersect({0, 0, 10}, {0, 0, .000001f}, t));
    check_close(t, 17);
    MG_CHECK(plane.FindIntersect({0, 0, 10}, {0, 0, -2}, t));
    check_close(t, 4);
    MG_CHECK(plane.FindIntersect({0, 0, 10}, {0, 0, 1}, t));
    check_close(t, -8);
    MG_CHECK(plane.FindIntersect({0, 0, 2}, {0, 0, -1}, t));
    check_close(t, 0);
}
void reuse_and_alias() {
    auto plane = sentinel();
    SPlane::BuildFromPointNormal(plane, {0, 0, 2}, {0, 0, 2});
    coefficients(plane, 0, 0, 2, -4); // Preserve non-unit normal scaling.
    SPlane::BuildFromPointNormal(plane, {3, 0, 0}, {-1, 0, 0});
    coefficients(plane, -1, 0, 0, 3);
    SPlane::BuildFromPoints(plane, {0, 0, 2}, {1, 0, 2}, {0, 1, 2});
    coefficients(plane, 0, 0, 1, -2);
    SPlane::BuildFromPointNormal(plane, {0, 0, 7}, plane.norm);
    coefficients(plane, 0, 0, 1, -7);
}
void direct_fields() {
    auto plane = sentinel();
    plane.norm = D3DXVECTOR3(0, -1, 0);
    plane.dist = 4;
    plane.UpdateSignBits();
    MG_CHECK(plane.signbits == 2);
    check_close(plane.Distance({0, 1, 0}), 3);
    // Ordinary camera planes populate norm/dist, without invoking a builder.
    check_close(plane.dxplane.a, 31);
    check_close(plane.dxplane.d, 43);
    const SPlane copy = plane;
    check_close(copy.Distance({0, 1, 0}), 3);
    MG_CHECK(copy.signbits == 2);
}
void layout() {
    static_assert(std::is_standard_layout_v<SPlane>);
    static_assert(sizeof(SPlane) == 36 && alignof(SPlane) == 4);
    static_assert(offsetof(SPlane, dxplane) == 0 && offsetof(SPlane, norm) == 16);
    static_assert(offsetof(SPlane, dist) == 28 && offsetof(SPlane, signbits) == 32);
    MG_CHECK(sizeof(void *) == 4);
}
const Case cases[]{
    {"game.geometry.plane.point_normal", point_normal}, {"game.geometry.plane.points_orientation", points_orientation},
    {"game.geometry.plane.distance_and_sides", distance_and_sides}, {"game.geometry.plane.box_octants", box_octants},
    {"game.geometry.plane.intersection_contract", intersection_contract}, {"game.geometry.plane.reuse_and_alias", reuse_and_alias},
    {"game.geometry.plane.direct_fields", direct_fields}, {"game.geometry.plane.layout", layout},
};
}
int main(int argc, char **argv) { return tests::run(argc, argv, cases); }
