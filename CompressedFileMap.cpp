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

    /// map the page that contains start
    ManagedHandle fileHandle = createReadFile(m_fileName);
    ManagedHandle fileMapping = createReadFileMapping(fileHandle.get(), 0);

    size_t alignedStart = alignDown(start, 65536);
    size_t viewSize = getCorrectViewSize(fileHandle.get(), alignedStart, PAGE_CACHE_SIZE);

    ManagedViewHandle fileView = createReadMapViewOfFile(fileMapping.get(), { (DWORD)alignedStart }, viewSize);

    std::unique_ptr<uint8_t[]> newPage(reinterpret_cast<uint8_t*>(VirtualAlloc(nullptr, viewSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)));
    memcpy(newPage.get(), fileView.get(), viewSize);

    void* result = newPage.get() + (start - alignedStart);

    m_pages[m_nextNewPageIndex].m_buffer = ManagedMem(newPage.release(), MemoryHandle());
    m_pages[m_nextNewPageIndex].m_start.QuadPart = alignedStart;
    m_pages[m_nextNewPageIndex].m_end.QuadPart = alignedStart + viewSize;

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
