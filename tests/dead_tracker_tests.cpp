// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "CMain.hpp"
#include "CHeap.hpp"
#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <memory>
#include <vector>
#include <stupid_logger.hpp>

logger_type lgr{std::cerr};

#if !defined(DEAD_PTR_SPY_ENABLE) || !defined(DEAD_CLASS_SPY_ENABLE) || !defined(MEM_SPY_ENABLE)
#error These tests require the complete optional tracker configuration.
#endif

// Existing implementation storage, inspected only by this separately linked
// instrumentation executable. No production visibility or layout is changed.
namespace DeadPtr {
extern uint8_t semetery_heap[SEMETERY_HEAP_SIZE];
extern int semetery_heap_size, semetery_cnt;
}
namespace Base {
extern BYTE DCS_semetery_heap[SEMETERY_HEAP_SIZE];
extern int DCS_semetery_heap_size, DCS_semetery_cnt;
}

namespace {
struct PointerRecord {
    std::vector<int> data;
    explicit PointerRecord(int words, int value) : data(words, value) {
        data[2] = DPTR_SIGNATURE0; data[3] = DPTR_SIGNATURE1;
        data[4] = DPTR_SIGNATURE2; data[5] = words * int(sizeof(int));
    }
    void save() { DeadPtr::free_mem(data.data(), data.size() * sizeof(int)); }
    void remove() { DeadPtr::remove_by_ptr(data.data()); }
    void *saved() { return DeadPtr::get_dead_mem(data.data()); }
    void check() { MG_CHECK(saved()); MG_CHECK(std::memcmp(saved(), data.data(), data.size() * sizeof(int)) == 0); }
};
template<size_t N> struct ClassRecord : Base::CMain {
    std::array<BYTE, N> data;
    explicit ClassRecord(BYTE value) { data.fill(value); }
    void save(int bytes = sizeof(ClassRecord)) { DCS_ClassDead(bytes); }
    void remove() { DCS_Reincarnation(); }
    void check(int bytes = sizeof(ClassRecord)) {
        MG_CHECK(DCS_GetDeadBody());
        MG_CHECK(std::memcmp(DCS_GetDeadBody(), this, bytes) == 0);
    }
};

void pointer_compaction() {
    std::array<PointerRecord, 4> records{PointerRecord(13, 1), PointerRecord(17, 2), PointerRecord(19, 3), PointerRecord(23, 4)};
    for (auto &record : records) record.save();
    MG_CHECK(DeadPtr::semetery_cnt == 4);
    const int old_size = DeadPtr::semetery_heap_size;
    const int new_size = old_size - int(sizeof(DeadPtr::SDeadMemBody)) - int(records[1].data.size() * sizeof(int));
    std::vector<BYTE> tail(DeadPtr::semetery_heap + new_size, DeadPtr::semetery_heap + old_size);
    std::fill_n(DeadPtr::semetery_heap + old_size, 256, BYTE{0xa5});
    records[1].remove(); // Middle removal must not copy bytes beyond the occupied suffix.
    MG_CHECK(DeadPtr::semetery_cnt == 3 && DeadPtr::semetery_heap_size == new_size);
    MG_CHECK(std::memcmp(tail.data(), DeadPtr::semetery_heap + new_size, tail.size()) == 0);
    MG_CHECK(!records[1].saved());
    for (int i : {0, 2, 3}) records[i].check();
    records[1].save(); records[1].check(); // Reuse an original address after compaction.
    for (int i : {0, 1, 3, 2}) records[i].remove(); // Head, tail, single, empty.
    records[2].remove();
    MG_CHECK(DeadPtr::semetery_cnt == 0 && DeadPtr::semetery_heap_size == 0);
}
void class_compaction() {
    std::array<ClassRecord<128>, 4> records{ClassRecord<128>(1), ClassRecord<128>(2), ClassRecord<128>(3), ClassRecord<128>(4)};
    const std::array lengths{47, 63, 91, 127}; // Deliberately unaligned record headers.
    for (int i = 0; i < 4; ++i) records[i].save(lengths[i]);
    const int old_size = Base::DCS_semetery_heap_size;
    const int new_size = old_size - 2 * int(sizeof(int)) - lengths[1];
    std::vector<BYTE> tail(Base::DCS_semetery_heap + new_size, Base::DCS_semetery_heap + old_size);
    std::fill_n(Base::DCS_semetery_heap + old_size, 256, BYTE{0xa5});
    records[1].remove();
    MG_CHECK(Base::DCS_semetery_cnt == 3 && Base::DCS_semetery_heap_size == new_size);
    MG_CHECK(std::memcmp(tail.data(), Base::DCS_semetery_heap + new_size, tail.size()) == 0);
    MG_CHECK(!records[1].DCS_GetDeadBody());
    for (int i : {0, 2, 3}) records[i].check(lengths[i]);
    records[1].save(lengths[1]); records[1].check(lengths[1]);
    for (int i : {0, 1, 3, 2}) records[i].remove();
    records[2].remove();
    MG_CHECK(Base::DCS_semetery_cnt == 0 && Base::DCS_semetery_heap_size == 0);
}
void pointer_eviction() {
    std::array<PointerRecord, 3> records{PointerRecord(87500, 1), PointerRecord(87500, 2), PointerRecord(87500, 3)};
    for (auto &record : records) record.save();
    MG_CHECK(!records[0].saved());
    MG_CHECK(DeadPtr::semetery_cnt == 2 && DeadPtr::semetery_heap_size == 700016);
    records[1].check(); records[2].check();
    records[1].remove(); records[2].remove();
}
void class_eviction() {
    std::array<std::unique_ptr<ClassRecord<350000>>, 3> records;
    for (int i = 0; i < 3; ++i) {
        records[i] = std::make_unique<ClassRecord<350000>>(BYTE(i + 1));
        records[i]->save();
    }
    MG_CHECK(!records[0]->DCS_GetDeadBody());
    MG_CHECK(Base::DCS_semetery_cnt == 2);
    records[1]->check(); records[2]->check();
    records[1]->remove(); records[2]->remove();
}
void pointer_bounds() {
    for (size_t length = 0; length <= 15; ++length) {
        auto bytes = std::make_unique<BYTE[]>(length);
        DeadPtr::free_mem(bytes.get(), length); // Incomplete signatures must not read beyond allocation.
    }
    PointerRecord record(16, 5);
    for (int invalid : {-1, 0, 20, 65, INT_MAX}) {
        record.data[5] = invalid; record.save();
        MG_CHECK(!record.saved() && DeadPtr::semetery_cnt == 0);
    }
    record.data[5] = int(record.data.size() * sizeof(int)); record.save(); record.check();
    DeadPtr::SDeadMemBody header;
    std::memcpy(&header, DeadPtr::semetery_heap, sizeof(header));
    auto damaged = header;
    damaged.size = INT_MAX;
    std::memcpy(DeadPtr::semetery_heap, &damaged, sizeof(damaged));
    MG_CHECK(!record.saved());
    std::memcpy(DeadPtr::semetery_heap, &header, sizeof(header));
    record.check(); record.remove();
    ClassRecord<64> object(7);
    for (int invalid : {-1, SEMETERY_HEAP_SIZE, INT_MAX}) {
        object.save(invalid); MG_CHECK(!object.DCS_GetDeadBody());
    }
    object.save(); object.check();
    std::array<int, 2> class_header;
    std::memcpy(class_header.data(), Base::DCS_semetery_heap, sizeof(class_header));
    auto damaged_class = class_header;
    damaged_class[1] = 0;
    std::memcpy(Base::DCS_semetery_heap, damaged_class.data(), sizeof(damaged_class));
    MG_CHECK(!object.DCS_GetDeadBody());
    std::memcpy(Base::DCS_semetery_heap, class_header.data(), sizeof(class_header));
    object.check(); object.remove();
    // Exercise the actual instrumented allocator's size handoff for tiny blocks.
    Base::CMain::BaseInit();
    for (unsigned size = 1; size < 16; ++size) {
        void *block = Base::CHeap::AllocClear(size, __FILE__, __LINE__);
        Base::CHeap::Free(block, __FILE__, __LINE__);
    }
    Base::CMain::BaseDeInit();
}
constexpr tests::Case cases[] = {
    {"base.memory.pointer_compaction", pointer_compaction}, {"base.memory.class_compaction", class_compaction},
    {"base.memory.pointer_eviction", pointer_eviction}, {"base.memory.class_eviction", class_eviction},
    {"base.memory.pointer_bounds", pointer_bounds},
};
}
int main(int argc, char **argv) { return tests::run(argc, argv, cases); }
