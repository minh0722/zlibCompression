#pragma once
#include "pch.h"

struct Fat
{
public:
    void readFromFile(LPCWSTR fatFilePath);
    
    ManagedMemArray<size_t> m_chunksOffsets;
    DWORD m_chunksOffsetsCount;
    DWORD m_lastChunkSizeBeforeCompression;
    size_t m_originalFileSize;
};

