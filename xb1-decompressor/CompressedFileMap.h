#pragma once
#include "pch.h"

class CompressedFileMap
{
    struct Page
    {
        Page() : m_start({ 0 }), m_end({ 0 }), m_buffer(nullptr) {}

        LARGE_INTEGER m_start;
        LARGE_INTEGER m_end;
        ManagedMem<void> m_buffer;
    };

public:
    CompressedFileMap(LPCWSTR compressedFileName);

    void* readMem(size_t start, size_t size);

private:
    size_t getCorrectViewSize(HANDLE file, size_t start, size_t size);

    std::vector<Page> m_pages;
    LPCWSTR m_fileName;
    size_t m_nextNewPageIndex;
};

