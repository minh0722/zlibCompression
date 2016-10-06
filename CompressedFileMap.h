#pragma once
#include "pch.h"

class CompressedFileMap
{
    struct MemoryHandle
    {
        void operator()(void* ptr)
        {
            VirtualFree(ptr, 0, MEM_RELEASE);
        }
    };

    using ManagedMem = std::unique_ptr<void, MemoryHandle>;

    struct Page
    {
        Page() : m_start({0}), m_end({0}), m_buffer(nullptr) {}

        LARGE_INTEGER m_start;
        LARGE_INTEGER m_end;
        ManagedMem m_buffer;
    };

public:
    CompressedFileMap(LPCWSTR compressedFileName);

    void* readMem(size_t start, size_t size);

private:
    /* assure that we don't read past the end of file */
    size_t getCorrectViewSize(HANDLE file, size_t start, size_t size);

    std::vector<Page> m_pages;
    LPCWSTR m_fileName;
    size_t m_nextNewPageIndex;
};

