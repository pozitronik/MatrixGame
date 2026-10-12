// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "MatrixMap.hpp"
#include "MatrixSide.hpp"
#include "Helper.hpp"

#include <memory>

namespace tests {
template<class T>
struct EngineDelete {
    void operator()(T *value) const { HDelete(T, value, g_MatrixHeap); }
};

template<class T>
using EnginePtr = std::unique_ptr<T, EngineDelete<T>>;

// Production constructors may depend on the zeroed storage supplied by HNew.
// Keep that allocation contract; make_unique does not provide it.
template<class T>
EnginePtr<T> make_engine_object() {
    return EnginePtr<T>(HNew(g_MatrixHeap) T);
}

// Production map/side ownership with no meshes, packages, window or device.
struct CommandWorld {
    CHeap heap;
    CBlockPar data;
    std::unique_ptr<CMatrixMapLogic> map;
    const DWORD previousFlags = g_Flags;

    CommandWorld() {
        g_Flags = 0;
        g_MatrixHeap = &heap;
        g_MatrixData = &data;
        try {
            CacheInit();
            map = std::make_unique<CMatrixMapLogic>();
            g_MatrixMap = map.get();
            map->m_SizeMove = CPoint(20, 20);
            map->m_PlayerSide = nullptr;
            map->m_Side = HNew(g_MatrixHeap) CMatrixSideUnit;
            map->m_SideCnt = 1;
            map->m_PlayerSide = map->m_Side;
            map->m_PlayerSide->m_Id = PLAYER_SIDE;
        }
        catch (...) { clear(); throw; }
    }

    ~CommandWorld() { clear(); }

    void clear() {
        map.reset();
#ifdef _DEBUG
        CHelper::ClearAll();
#endif
        CacheDeinit();
        g_MatrixMap = nullptr;
        g_MatrixData = nullptr;
        g_MatrixHeap = nullptr;
        g_Flags = previousFlags;
    }
};
}
