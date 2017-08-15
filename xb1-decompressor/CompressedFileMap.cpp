#include "pch.h"
#include "CompressedFileMap.h"

static const size_t PAGE_COUNT = 5;
static const size_t PAGE_CACHE_SIZE = 64 * 1024 * 100;

CompressedFileMap::CompressedFileMap(LPCWSTR compressedFileName)
    : m_pages(PAGE_COUNT), m_fileName(compressedFileName), m_nextNewPageIndex(0)
{
}

void* CompressedFileMap::readMem(size_t start, size_t size)
{
    /// find in current pages if found
    for (size_t i = 0; i < PAGE_COUNT; ++i)
    {
        if (m_pages[i].m_start.QuadPart <= start && start + size <= m_pages[i].m_end.QuadPart)
        {
            uint8_t* buffer = reinterpret_cast<uint8_t*>(m_pages[i].m_buffer.get());
            size_t offset = start - m_pages[i].m_start.QuadPart;

            return buffer + offset;
        }
    }

    DWORD written;

    /// map the page that contains start
    size_t alignedStartOffset = alignDown(start, 65536);
    ManagedHandle fileHandle = createReadFile(m_fileName, alignedStartOffset);

    size_t viewSize = getCorrectViewSize(fileHandle.get(), alignedStartOffset, PAGE_CACHE_SIZE);

    /// dram garlic
    std::unique_ptr<uint8_t[]> newPage(
        reinterpret_cast<uint8_t*>(
            VirtualAlloc(
                nullptr, 
                viewSize, 
                MEM_RESERVE | MEM_COMMIT | MEM_GRAPHICS | MEM_LARGE_PAGES, 
                PAGE_READWRITE | PAGE_GPU_COHERENT)));
    
    ZeroMemory(newPage.get(), viewSize);
    throwIfFalse(ReadFile(fileHandle.get(), newPage.get(), viewSize, &written, nullptr));
    
    void* result = newPage.get() + (start - alignedStartOffset);

    m_pages[m_nextNewPageIndex].m_buffer = ManagedMem<void>(newPage.release(), MemCloser());
    m_pages[m_nextNewPageIndex].m_start.QuadPart = alignedStartOffset;
    m_pages[m_nextNewPageIndex].m_end.QuadPart = alignedStartOffset + viewSize;

    m_nextNewPageIndex = (m_nextNewPageIndex + 1) % PAGE_COUNT;

    return result;
}

size_t CompressedFileMap::getCorrectViewSize(HANDLE file, size_t start, size_t viewSize)
{
    size_t sizeOfFile = fileSize(file).QuadPart;

    if (start + viewSize >= sizeOfFile)
    {
        viewSize = sizeOfFile - start;
    }

    return viewSize;
}
