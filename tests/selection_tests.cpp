// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "command_world.hpp"
#include "MatrixFormGame.hpp"
#include "MatrixMultiSelection.hpp"
#include "MatrixObjectBuilding.hpp"
#include "Interface/CInterface.h"

#include <vector>

struct CountingBuilding : CMatrixBuilding {
    mutable int calls = 0;
    mutable CRect rectangle{};
    bool hit = false;
    bool InRect(const CRect &rect) const override {
        ++calls;
        rectangle = rect;
        return hit && rect.IsInRect(CPoint(50, 50));
    }
};

// Synthetic visibility replaces only mesh/frustum setup; selection and input are production code.
struct SelectionFixture {
    tests::CommandWorld world;
    CFormMatrixGame form;
    CountingBuilding *building;
    std::vector<CountingBuilding *> extra;
    CIFaceList *previousInterface = g_IFaceList;
    explicit SelectionFixture(const CPoint &origin = CPoint(0, 0)) {
        g_IFaceList = HNew(g_MatrixHeap) CIFaceList;
        world.map->m_PlayerSide->InitPlayerSide();
        building = HNew(g_MatrixHeap) CountingBuilding;
        building->m_Side = PLAYER_SIDE;
        CMatrixMapStatic::objects_left = 0;
        CMatrixMapStatic::objects_rite = 1;
        CMatrixMapStatic::objects[0] = building;
        CMultiSelection::StaticInit();
        CMultiSelection::m_GameSelection = CMultiSelection::Begin(origin);
        MG_CHECK(CMultiSelection::m_GameSelection != nullptr);
    }
    ~SelectionFixture() {
        while (CMultiSelection::m_First) {
            auto *selection = CMultiSelection::m_First;
            HDelete(CMultiSelection, selection, g_MatrixHeap);
        }
        for (int i = 0; i < CMatrixMapStatic::objects_rite; ++i) CMatrixMapStatic::objects[i] = nullptr;
        CMatrixMapStatic::objects_left = CMatrixMapStatic::objects_rite = 0;
        for (auto *object : extra) { HDelete(CountingBuilding, object, g_MatrixHeap); }
        if (building) { HDelete(CountingBuilding, building, g_MatrixHeap); }
        HDelete(CIFaceList, g_IFaceList, g_MatrixHeap);
        g_IFaceList = previousInterface;
    }
    CountingBuilding *add_building() {
        auto *object = HNew(g_MatrixHeap) CountingBuilding;
        object->m_Side = PLAYER_SIDE;
        object->hit = true;
        extra.push_back(object);
        CMatrixMapStatic::objects[CMatrixMapStatic::objects_rite++] = object;
        return object;
    }
};

namespace {
void mouse_moves_are_queued() {
    SelectionFixture fixture;
    for (int i = 1; i <= 100; ++i) fixture.form.MouseMove(i, i);
    MG_CHECK(fixture.building->calls == 0);
    MG_CHECK(fixture.world.map->m_Cursor.GetPos().x == 100);
    MG_CHECK(fixture.world.map->m_Cursor.GetPos().y == 100);
}

void latest_rectangle_once() {
    SelectionFixture fixture;
    fixture.building->hit = true;
    auto *selection = CMultiSelection::m_GameSelection;
    for (int i = 1; i <= 100; ++i) fixture.form.MouseMove(i, i);
    MG_CHECK(selection->UpdatePending(TRACE_BUILDING, nullptr, 0));
    MG_CHECK(fixture.building->calls == 1);
    MG_CHECK(selection->FindItem(fixture.building));
    MG_CHECK(!selection->UpdatePending(TRACE_BUILDING, nullptr, 0));
    MG_CHECK(fixture.building->calls == 1);
    fixture.form.MouseMove(20, 20);
    MG_CHECK(selection->UpdatePending(TRACE_BUILDING, nullptr, 0));
    MG_CHECK(fixture.building->calls == 2);
    MG_CHECK(!selection->FindItem(fixture.building));
}

void release_before_frame() {
    SelectionFixture fixture;
    fixture.form.MouseMove(90, 90);
    MG_CHECK(fixture.building->calls == 0);
    fixture.form.MouseKey(B_UP, VK_LBUTTON, 42, 84);
    MG_CHECK(fixture.building->calls == 1);
    MG_CHECK(fixture.building->rectangle.right == 42 && fixture.building->rectangle.bottom == 84);
    MG_CHECK(CMultiSelection::m_GameSelection == nullptr);
    MG_CHECK(fixture.world.map->m_PlayerSide->GetCurSelGroup()->GetObjectsCnt() == 0);
}

void reversed_rectangle_and_mask() {
    SelectionFixture fixture(CPoint(100, 100));
    fixture.building->hit = true;
    auto *selection = CMultiSelection::m_GameSelection;
    fixture.form.MouseMove(0, 0);
    MG_CHECK(selection->UpdatePending(TRACE_BUILDING, nullptr, 0));
    MG_CHECK(selection->FindItem(fixture.building));
    MG_CHECK(fixture.building->rectangle.left == 0 && fixture.building->rectangle.top == 0);
    fixture.form.MouseMove(0, 0);
    MG_CHECK(selection->UpdatePending(TRACE_ROBOT, nullptr, 0));
    MG_CHECK(!selection->FindItem(fixture.building));
    MG_CHECK(fixture.building->calls == 1);
}

void cancellation_discards_pending() {
    SelectionFixture fixture;
    auto *selection = CMultiSelection::m_GameSelection;
    fixture.form.MouseMove(100, 100);
    selection->End(false);
    MG_CHECK(!selection->UpdatePending(TRACE_BUILDING, nullptr, 0));
    MG_CHECK(fixture.building->calls == 0);
    MG_CHECK(!selection->FindItem(fixture.building));
}

void selection_limit_and_order() {
    SelectionFixture fixture;
    fixture.building->hit = true;
    for (int i = 0; i < 31; ++i) fixture.add_building();
    fixture.form.MouseMove(100, 100);
    auto *selection = CMultiSelection::m_GameSelection;
    MG_CHECK(selection->UpdatePending(TRACE_BUILDING, nullptr, 0));
    MG_CHECK(selection->FindItem(fixture.building));
    for (int i = 0; i < 31; ++i) MG_CHECK(selection->FindItem(fixture.extra[i]) == (i < 29));
}

void small_rectangle_is_preserved() {
    SelectionFixture fixture;
    fixture.form.MouseMove(1, 2);
    MG_CHECK(CMultiSelection::m_GameSelection->UpdatePending(TRACE_BUILDING, nullptr, 0));
    MG_CHECK(fixture.building->rectangle.right == 1 && fixture.building->rectangle.bottom == 2);
    MG_CHECK(fixture.building->calls == 1);
}

void object_teardown_before_refresh() {
    SelectionFixture fixture;
    fixture.building->hit = true;
    auto *selection = CMultiSelection::m_GameSelection;
    fixture.form.MouseMove(100, 100);
    MG_CHECK(selection->UpdatePending(TRACE_BUILDING, nullptr, 0));
    MG_CHECK(selection->FindItem(fixture.building));
    fixture.form.MouseMove(80, 80);
    CMatrixMapStatic::RemoveFromSorted(fixture.building);
    auto *removed = fixture.building;
    HDelete(CountingBuilding, fixture.building, g_MatrixHeap);
    fixture.building = nullptr;
    MG_CHECK(selection->UpdatePending(TRACE_BUILDING, nullptr, 0));
    MG_CHECK(!selection->FindItem(removed));
}
constexpr tests::Case cases[] = {
    {"game.selection.queued_moves", mouse_moves_are_queued},
    {"game.selection.latest_rectangle", latest_rectangle_once},
    {"game.selection.release_before_frame", release_before_frame},
    {"game.selection.reversed_mask", reversed_rectangle_and_mask},
    {"game.selection.cancel_pending", cancellation_discards_pending},
    {"game.selection.limit_order", selection_limit_and_order},
    {"game.selection.small_rectangle", small_rectangle_is_preserved},
    {"game.selection.object_teardown", object_teardown_before_refresh},
};
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) {
        std::cerr << "Selection test leaked tracked heap allocations.\n";
        result = EXIT_FAILURE;
    }
#endif
    if (result == EXIT_SUCCESS) Base::CMain::BaseDeInit();
    return result;
}
