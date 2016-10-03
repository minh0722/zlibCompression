#pragma once
#include "file_util.h"
#include <vector>

namespace File
{
    void throwIfFalse(bool flag)
    {
        if (!flag)
            throw std::exception();
    }

    struct Fat
    {
        void writeToFile(LPCWSTR fatFilePath)
        {
            File::ManagedHandle fatHandle = File::createWriteFile(fatFilePath);

            size_t chunksCount = m_chunksSizes.size();

            throwIfFalse(WriteFile(fatHandle.get(), &m_fileSize, sizeof(m_fileSize), nullptr, nullptr));
            throwIfFalse(WriteFile(fatHandle.get(), &chunksCount, sizeof(chunksCount), nullptr, nullptr));
            throwIfFalse(WriteFile(fatHandle.get(), m_chunksSizes.data(), chunksCount * sizeof(size_t), nullptr, nullptr));
        }

        void readFromFile(LPCWSTR fatFilePath)
        {
            File::ManagedHandle fatHandle = File::createReadFile(fatFilePath);

            //LARGE_INTEGER fileSize = File::fileSize(fatHandle.get());

            size_t chunksCount = 0;

            DWORD readCount;
            throwIfFalse(ReadFile(fatHandle.get(), &m_fileSize, sizeof(m_fileSize), &readCount, nullptr));
            throwIfFalse(ReadFile(fatHandle.get(), &chunksCount, sizeof(chunksCount), &readCount, nullptr));

            m_chunksSizes.resize(chunksCount);

            throwIfFalse(ReadFile(fatHandle.get(), m_chunksSizes.data(), chunksCount * sizeof(size_t), &readCount, nullptr));
        }

        std::vector<size_t> m_chunksSizes;
        size_t m_fileSize;
    };
}