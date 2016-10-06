#pragma once
#include "pch.h"

class Decompressor
{
public:
    Decompressor();

    void decompress(LPCWSTR inputCompressedFilePath, LPCWSTR outputDecompressedFilePath);

private:
    uint32_t zlibDecompress(void* source, void* dest, size_t sourceBytesCount);

    std::vector<std::unique_ptr<Chunk>> decompressChunks(uint8_t* compressedFileContent, std::vector<size_t>& fatChunkSizes, size_t fatStartIndex);

    size_t getViewSize(size_t fatIndex, std::vector<size_t>& fatChunksSizes);

    void writeDecompressedChunksToFile(std::vector<std::unique_ptr<Chunk>>&& decompressedChunks, LPCWSTR filePath);
};

