// MatrixGame - SR2 Planetary battles engine
// Copyright (C) 2012, Elemental Games, Katauri Interactive, CHK-Games
// Licensed under GPLv2 or any later version
// Refer to the LICENSE file included

#include "CHeap.hpp"
#include "CException.hpp"

#include <windows.h>
#include <stdlib.h>
#include <type_traits>

#include <utils.hpp>

#ifdef DEAD_PTR_SPY_ENABLE
#define DEAD_PTR_SPY_SEEK 10
namespace DeadPtr {
static_assert(std::is_trivially_copyable_v<SDeadMemBody>);
int get_id(void) {
    static int last_id;
    return last_id++;
}
uint8_t semetery_heap[SEMETERY_HEAP_SIZE];
SDeadMem semetery[SEMETERY_SIZE];

int semetery_cnt = 0;
int semetery_heap_size = 0;

int find_by_id(int id, uint8_t **ptr) {
    // Records need not be aligned: their payload lengths are arbitrary bytes.
    for (int offset = 0; offset <= semetery_heap_size - int(sizeof(SDeadMemBody));) {
        SDeadMemBody body;
        memcpy(&body, semetery_heap + offset, sizeof(body));
        if (body.size < int(sizeof(body)) || body.size > semetery_heap_size - offset)
            break;
        if (body.id == id) {
            *ptr = semetery_heap + offset;
            return body.size;
        }
        offset += body.size;
    }
    *ptr = nullptr;
    return 0;
}
void remove_by_id(int id) {
    uint8_t *ptr;
    int sz = find_by_id(id, &ptr);

    if (sz == 0)
        return;
    const auto remaining = semetery_heap_size - (ptr - semetery_heap) - sz;
    memmove(ptr, ptr + sz, remaining);
    semetery_heap_size -= sz;
}
void remove_by_ptr(void *ptr) {
    for (int i = 0; i < semetery_cnt; ++i) {
        if (semetery[i].ptr_was == ptr) {
            int id = semetery[i].id;
            remove_by_id(id);

            static_assert(std::is_trivially_copyable_v<SDeadMem>);
            memmove(semetery + i, semetery + i + 1, (semetery_cnt - i - 1) * sizeof(SDeadMem));
            --semetery_cnt;
            return;
        }
    }
}

void remove_first(void) {
    if (semetery_cnt == 0)
        return;
    remove_by_id(semetery[0].id);
    memmove(semetery, semetery + 1, (semetery_cnt - 1) * sizeof(SDeadMem));
    --semetery_cnt;
}

void *get_dead_mem(void *mem) {
    for (int i = 0; i < semetery_cnt; ++i) {
        if (semetery[i].ptr_was == mem) {
            uint8_t *ptr;
            int sz = find_by_id(semetery[i].id, &ptr);
            return sz == 0 ? nullptr : ptr + sizeof(SDeadMemBody);
        }
    }

    return NULL;
}

void free_mem(void *mem0, size_t allocation_size) {
    int *mem = (int *)mem0;

    int idx = 0;
    for (; idx < DEAD_PTR_SPY_SEEK && (idx + 4) * sizeof(int) <= allocation_size; ++idx) {
        if (my_signature(mem + idx))
            goto ok;
    }

    return;

ok:;

    {
        const int payload_size = my_signature_size(mem + idx);
        if (payload_size < int((idx + 4) * sizeof(int)) ||
            size_t(payload_size) > allocation_size ||
            payload_size > SEMETERY_HEAP_SIZE - int(sizeof(SDeadMemBody)))
            return;
        const int sz = int(sizeof(SDeadMemBody)) + payload_size;

        if (semetery_cnt >= SEMETERY_SIZE) {
            remove_first();
        }

        while ((semetery_heap_size + sz) > SEMETERY_HEAP_SIZE) {
            remove_first();
        }

        semetery[semetery_cnt].id = get_id();
        semetery[semetery_cnt].ptr_was = mem;
        semetery[semetery_cnt].size = sz;

        SDeadMemBody body;
        body.id = semetery[semetery_cnt].id;
        body.size = sz;

        memcpy(semetery_heap + semetery_heap_size, &body, sizeof(SDeadMemBody));
        memcpy(semetery_heap + semetery_heap_size + sizeof(SDeadMemBody), mem, sz - sizeof(SDeadMemBody));
        semetery_heap_size += sz;
        ++semetery_cnt;
    }
}

}  // namespace DeadPtr
#endif

namespace Base {

#ifdef MEM_SPY_ENABLE

#include <stdio.h>

SMemHeader *SMemHeader::first_mem_block;
SMemHeader *SMemHeader::last_mem_block;
uint SMemHeader::fullsize;
uint SMemHeader::maxblocksize;

void CHeap::StaticInit(void) {
    SMemHeader::first_mem_block = NULL;
    SMemHeader::last_mem_block = NULL;
    SMemHeader::fullsize = 0;
    SMemHeader::maxblocksize = 0;
}

void SMemHeader::Release(void) {
    LIST_DEL(this, first_mem_block, last_mem_block, prev, next);
    fullsize -= blocksize;
#ifdef MEM_CHECK
    uint8_t *d1 = (uint8_t *)(this + 1);
    uint8_t *d2 = ((uint8_t *)this) + blocksize - MEM_CHECK_BOUND_SIZE;
    for (int i = 0; i < MEM_CHECK_BOUND_SIZE; ++i) {
        bool begin = *(d1 + i) != MEM_CHECK_FILLER;
        bool end = *(d2 + i) != MEM_CHECK_FILLER;
        if (begin || end) {
#ifdef _DEBUG
            ERROR_S(utils::format(L"Memory corruption detected at (%ls)\n%ls - %d",
                begin ? L"begin" : L"end",
                utils::to_wstring(file).c_str(),
                line));
#else
            auto str = utils::format("Memory corruption detected at:\n%s - %d", file, line);
            MessageBox(NULL, str.c_str(), "Memory corruption!", MB_OK | MB_ICONERROR);
            debugbreak();
#endif
        }
    }

    memset(d2, 0, MEM_CHECK_BOUND_SIZE);
#endif
}

void CHeap::StaticDeInit(void) {
    // check!!!!!!!!!
    if (SMemHeader::first_mem_block) {
        std::string buf{"There are some memory leaks have detected:\n"};
        while (SMemHeader::last_mem_block) {
            if (buf.length() < 65000)
            {
                auto rec =
                    utils::format("%i, %u : %s - %i\n",
                        SMemHeader::last_mem_block->cnt,
                        SMemHeader::last_mem_block->blocksize,
                        SMemHeader::last_mem_block->file,
                        SMemHeader::last_mem_block->line);

                // if the same record is already in the buffer - don't add it again
                if (buf.find(rec) == std::string::npos)
                {
                    buf += rec;
                }

                // sprintf(buf + len, "%u : %s - %i\n", SMemHeader::last_mem_block->blocksize,
                // SMemHeader::last_mem_block->file, SMemHeader::last_mem_block->line);
            }
            else
            {
                buf += "............ and more.......";
                break;
            }
            SMemHeader::last_mem_block = SMemHeader::last_mem_block->prev;
        }
        MessageBoxA(0, buf.c_str(), "Memory leaks!!!!!!!!!", MB_ICONEXCLAMATION);
    }
}

void CHeap::Free(void *buf, const char *file, int line) {
    if (!buf)
    {
        ERROR_S(
            utils::format(
                L"NULL pointer is passed to Free function at: %ls - %d",
                utils::to_wstring(file).c_str(),
                line));
    }

#ifdef DEAD_PTR_SPY_ENABLE
    const auto allocation_size = SMemHeader::CalcBegin(buf)->blocksize - SMemHeader::CalcSize(0);
    DeadPtr::free_mem(buf, allocation_size);
#endif

    SMemHeader *h = SMemHeader::CalcBegin(buf);
    h->Release();
    Free(h);
}

#endif

void CHeap::AllocationError(int zn)
{
    wchar buf[256];
    wcscpy(buf, L"NULL memory allocated on ");

    wchar temp[64];

    wchar *tstr = temp;
    int tsme = 63;

    int fm = 0;
    if (zn < 0) {
        fm = 1;
        zn = -zn;
    }

    while (zn > 0) {
        tsme--;
        tstr[tsme] = wchar(zn - int(zn / 10) * 10) + wchar('0');
        zn = zn / 10;
    }
    if (fm) {
        tsme--;
        tstr[tsme] = L'-';
    }
    int cnt = 63 - tsme;
    for (int i = 0; i < cnt; i++)
        tstr[i] = tstr[tsme + i];
    tstr[cnt] = 0;

    wcscat(buf, temp);
    wcscat(buf, L" bytes request.");

    ERROR_S(buf);
}

}  // namespace Base
