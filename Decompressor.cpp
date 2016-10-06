#include "pch.h"
#include "Decompressor.h"
#include "Fat.h"
#include "CompressedFileMap.h"
#include <ppl.h>

Decompressor::Decompressor()
{
}

void Decompressor::decompress(LPCWSTR inputCompressedFilePath, LPCWSTR outputDecompressedFilePath)
{
    Fat fat;
    fat.readFromFile(FAT_FILE_PATH);

    CompressedFileMap compressedFileMap(inputCompressedFilePath);

    size_t fatIndex = 0;
    LARGE_INTEGER offset = { 0 };

    while (fatIndex + CHUNKS_PER_MAP_COUNT < fat.m_chunksSizes.size())
    {
        size_t viewSize = getViewSize(fatIndex, fat.m_chunksSizes);
        uint8_t* compressedFileContent = reinterpret_cast<uint8_t*>(compressedFileMap.readMem(fat.m_chunksSizes[fatIndex], viewSize));

        auto decompressedChunks = decompressChunks(compressedFileContent, fat.m_chunksSizes, fatIndex);
        writeDecompressedChunksToFile(std::move(decompressedChunks), outputDecompressedFilePath);

        fatIndex += CHUNKS_PER_MAP_COUNT;
    }

    /// decompress the rest unaligned chunks
    if (fatIndex < fat.m_chunksSizes.size() - 1)
    {
        size_t viewSize = fat.m_chunksSizes.back() - fat.m_chunksSizes[fatIndex];
        uint8_t* compressedFileContent = reinterpret_cast<uint8_t*>(compressedFileMap.readMem(fat.m_chunksSizes[fatIndex], viewSize));

        auto decompressedChunks = decompressChunks(compressedFileContent, fat.m_chunksSizes, fatIndex);
        writeDecompressedChunksToFile(std::move(decompressedChunks), outputDecompressedFilePath);
    }
}

uint32_t Decompressor::zlibDecompress(void* source, void* dest, size_t sourceBytesCount)
{
    int ret;
    unsigned have;
    z_stream stream;
    void* currentDest = dest;

    uint8_t output[PAGE_SIZE];

    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = 0;
    stream.next_in = Z_NULL;

    ret = inflateInit(&stream);
    assert(ret == Z_OK);

    stream.avail_in = sourceBytesCount;
    stream.next_in = reinterpret_cast<Bytef*>(source);

    do
    {
        stream.avail_out = PAGE_SIZE;
        stream.next_out = output;

        ret = inflate(&stream, Z_FINISH);
        assert(ret != Z_STREAM_ERROR);

        switch (ret)
        {
        case Z_NEED_DICT:
            ret = Z_DATA_ERROR;
        case Z_DATA_ERROR:
        case Z_MEM_ERROR:
            (void)inflateEnd(&stream);
            assert(false);
        default:
            break;
        }

        have = PAGE_SIZE - stream.avail_out;
        if (have != 0)
        {
            memcpy(currentDest, output, have);
        }
        currentDest = reinterpret_cast<uint8_t*>(currentDest) + have;

    } while (stream.avail_out == 0);

    (void)inflateEnd(&stream);

    assert(ret == Z_STREAM_END);

    uint32_t decompressedSize = reinterpret_cast<uint8_t*>(currentDest) - reinterpret_cast<uint8_t*>(dest);
    return decompressedSize;
}

std::vector<std::unique_ptr<Chunk>> Decompressor::decompressChunks(uint8_t* compressedFileContent, std::vector<size_t>& fatChunkSizes, size_t fatStartIndex)
{
    std::vector<std::unique_ptr<Chunk>> decompressedChunks;

    size_t fatEndIndex = (fatStartIndex + CHUNKS_PER_MAP_COUNT) >= fatChunkSizes.size() ? fatChunkSizes.size() - 1 : fatStartIndex + CHUNKS_PER_MAP_COUNT;

    decompressedChunks.resize(fatEndIndex - fatStartIndex);

    concurrency::parallel_for(fatStartIndex, fatEndIndex, [this, &fatChunkSizes, &decompressedChunks, &compressedFileContent, &fatStartIndex](size_t i)
    {
        std::unique_ptr<uint8_t[]> mem(reinterpret_cast<uint8_t*>(VirtualAlloc(nullptr, PAGE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)));

        size_t compressedChunkSize = fatChunkSizes[i + 1] - fatChunkSizes[i];
        size_t offset = fatChunkSizes[i] - fatChunkSizes[fatStartIndex];

        size_t decompressedSize = zlibDecompress(compressedFileContent + offset, mem.get(), compressedChunkSize);

        decompressedChunks[i % CHUNKS_PER_MAP_COUNT] = std::make_unique<Chunk>(mem.release(), decompressedSize);
    });

    return decompressedChunks;
}

size_t Decompressor::getViewSize(size_t fatIndex, std::vector<size_t>& fatChunksSizes)
{
    return fatChunksSizes[fatIndex + CHUNKS_PER_MAP_COUNT] - fatChunksSizes[fatIndex];
}

void Decompressor::writeDecompressedChunksToFile(std::vector<std::unique_ptr<Chunk>>&& decompressedChunks, LPCWSTR filePath)
{
    ManagedHandle outputFile = createWriteFile(filePath);

    for (size_t i = 0; i < decompressedChunks.size(); ++i)
    {
        uint8_t* chunkMem = reinterpret_cast<uint8_t*>(decompressedChunks[i]->m_memory.get());
        size_t size = decompressedChunks[i]->chunkSize;

        DWORD written;
        BOOL ret = WriteFile(outputFile.get(), chunkMem, size, &written, nullptr);
        assert(ret == TRUE);
    }
}