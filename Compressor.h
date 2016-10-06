#pragma once
#include "pch.h"

struct Fat;

class Compressor
{
public:
    Compressor();

    void compress(LPCWSTR inputFilePath, LPCWSTR outputFilePath);

private:
    size_t zlibCompress(void* source, void* dest, size_t sourceBytesCount);

    std::vector<std::unique_ptr<Chunk>> splitFile(uint8_t* fileContent, size_t pageCount, size_t pageSize);

    std::vector<std::unique_ptr<Chunk>> compressChunks(std::vector<std::unique_ptr<Chunk>>&& chunks);

    void writeCompressedChunksToFile(std::vector<std::unique_ptr<Chunk>>&& compressedChunks, LPCWSTR filePath);

    void getFat(Fat& fat, std::vector<std::unique_ptr<Chunk>>&& compressedChunks);

    std::vector<std::unique_ptr<Chunk>> splitLastUnalignedBytes(void* mapViewOfLastChunk, size_t chunkSizeInBytes);
};

