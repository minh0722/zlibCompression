#include "pch.h"
#include "Fat.h"

void Fat::writeToFile(LPCWSTR fatFilePath)
{
    ManagedHandle fatHandle = createWriteFile(fatFilePath);

    size_t chunksCount = m_chunksSizes.size();

    throwIfFalse(WriteFile(fatHandle.get(), &m_fileSize, sizeof(m_fileSize), nullptr, nullptr));
    throwIfFalse(WriteFile(fatHandle.get(), &chunksCount, sizeof(chunksCount), nullptr, nullptr));
    throwIfFalse(WriteFile(fatHandle.get(), m_chunksSizes.data(), chunksCount * sizeof(size_t), nullptr, nullptr));
}

void Fat::readFromFile(LPCWSTR fatFilePath)
{
    ManagedHandle fatHandle = createReadFile(fatFilePath);
    
    size_t chunksCount = 0;

    DWORD readCount;
    throwIfFalse(ReadFile(fatHandle.get(), &m_fileSize, sizeof(m_fileSize), &readCount, nullptr));
    throwIfFalse(ReadFile(fatHandle.get(), &chunksCount, sizeof(chunksCount), &readCount, nullptr));

    m_chunksSizes.resize(chunksCount);

    throwIfFalse(ReadFile(fatHandle.get(), m_chunksSizes.data(), chunksCount * sizeof(size_t), &readCount, nullptr));
}