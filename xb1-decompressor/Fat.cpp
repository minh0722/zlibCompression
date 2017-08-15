#include "pch.h"
#include "Fat.h"

void Fat::readFromFile(LPCWSTR fatFilePath)
{
    ManagedHandle fatHandle = createReadFile(L"\DataPCFat.fat", 0);

    DWORD readCount;

    throwIfFalse(ReadFile(fatHandle.get(), &m_originalFileSize, sizeof(m_originalFileSize), &readCount, nullptr));
    throwIfFalse(ReadFile(fatHandle.get(), &m_chunksOffsetsCount, sizeof(m_chunksOffsetsCount), &readCount, nullptr));

    std::unique_ptr<size_t[]> mem(
        reinterpret_cast<size_t*>(
            VirtualAlloc(
                nullptr,
                m_chunksOffsetsCount * sizeof(size_t),
                MEM_RESERVE | MEM_COMMIT | MEM_GRAPHICS | MEM_LARGE_PAGES,
                PAGE_READWRITE | PAGE_GPU_COHERENT)));

    m_chunksOffsets = ManagedMemArray<size_t>(
        mem.release(),
        MemCloser());

    assert(m_chunksOffsetsCount > 0);
    throwIfFalse(ReadFile(fatHandle.get(), m_chunksOffsets.get(), m_chunksOffsetsCount * sizeof(size_t), &readCount, nullptr));
    throwIfFalse(ReadFile(fatHandle.get(), &m_lastChunkSizeBeforeCompression, sizeof(m_lastChunkSizeBeforeCompression), &readCount, nullptr));
}
