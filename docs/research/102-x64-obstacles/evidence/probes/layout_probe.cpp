// Issue 102 exploratory layout probe. Prints sizes/offsets of persisted and host-facing
// structures from the real headers so x86 and x64 builds can be compared.
#include <cstddef>
#include <cstdio>

#include <windows.h>

#include "Pack.hpp"
#include "CStorage.hpp"
#include "MatrixGameDll.hpp"
#include "Common.hpp"
#include "VectorObject.hpp"

#define S(T) std::printf("sizeof %-28s %4u\n", #T, unsigned(sizeof(T)))
#define O(T, M) std::printf("offset %-28s %4u\n", #T "::" #M, unsigned(offsetof(T, M)))

int main() {
    std::printf("arch %s\n", sizeof(void *) == 8 ? "x64" : "x86");
    S(void *); S(long); S(dword); S(DWORD); S(wchar_t); S(size_t); S(bool); S(long double); S(Base::EFileType);
    // Package archive (on-disk records read directly into memory).
    S(Base::SFileRec); O(Base::SFileRec, m_Type); O(Base::SFileRec, m_Offset); O(Base::SFileRec, m_Extra);
    S(Base::SFolderRec); S(Base::SFileHandleRec);
    // Map payload records consumed as raw arrays.
    S(SCompilePoint); S(SCompileMove); S(SCompileMoveCell); S(SCompileBottomVert); S(Base::CRect);
    // Vector object file records.
    S(SVOHeader); S(SVOVertexNorTex); S(SVOMaterial); S(SVOGroup); S(SVOFrame); S(SVOAnimHeader);
    S(SVOMatrixHeader); S(SVOFrameEdge); S(SVOEdge);
    // Original host interface.
    S(SRobotsSettings); O(SRobotsSettings, FD3DDevice); O(SRobotsSettings, m_ShowStencilShadows);
    O(SRobotsSettings, m_BPP); O(SRobotsSettings, m_Brightness); O(SRobotsSettings, m_FSAASamples);
    O(SRobotsSettings, m_MaxDistance); O(SRobotsSettings, m_VSync);
    S(SRobotGameState);
    S(SMGDRobotInterface); O(SMGDRobotInterface, m_Run);
    S(SMGDRangersInterface); O(SMGDRangersInterface, m_RangersText); O(SMGDRangersInterface, m_MusicVolumeSet);
    S(SMGDRangersInterfaceText); O(SMGDRangersInterfaceText, m_Buf); O(SMGDRangersInterfaceText, m_Pitch);
    return 0;
}
