#pragma once
#include "pch.h"

struct Fat
{
    void writeToFile(LPCWSTR fatFilePath);
    void readFromFile(LPCWSTR fatFilePath);

    std::vector<size_t> m_chunksSizes;
    size_t m_fileSize;
};
