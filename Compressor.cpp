#include "pch.h"
#include <ppl.h>
#include "Compressor.h"
#include "Fat.h"

Compressor::Compressor()
{
}

void Compressor::compress(LPCWSTR inputFilePath, LPCWSTR outputFilePath)
{
    /// open input file
    ManagedHandle bigFile1 = createReadFile(inputFilePath);

    /// align down file size to map size
    LARGE_INTEGER bigFileSize1 = fileSize(bigFile1.get());
    LARGE_INTEGER bigFileAlignedSize = alignDown(bigFileSize1, MAP_SIZE1);

    /// how much map viewing we have to do
    const size_t MAP_COUNT1 = bigFileAlignedSize.QuadPart / MAP_SIZE1;

    /// mapping of the file
    ManagedHandle fileMapping = createReadFileMapping(bigFile1.get(), 0);

    /// contains offsets of the compressed chunks
    Fat fat;

    for (size_t i = 0; i < MAP_COUNT1; ++i)
    {
        /// the offset to the current map view of the file
        LARGE_INTEGER offset;
        offset.QuadPart = i * MAP_SIZE1;

        ManagedViewHandle mapFile1 = createReadMapViewOfFile(fileMapping.get(), offset, MAP_SIZE1);

        auto chunks = splitFile(reinterpret_cast<uint8_t*>(mapFile1.get()), CHUNKS_PER_MAP_COUNT1, PAGE_SIZE);
        auto compressedChunks = compressChunks(std::move(chunks));
        writeCompressedChunksToFile(std::move(compressedChunks), outputFilePath);
        getFat(fat, std::move(compressedChunks));
    }

    /// now we need to compress the remaining unaligned datas
    size_t remainingDataInByte = bigFileSize1.QuadPart - bigFileAlignedSize.QuadPart;

    if (remainingDataInByte)
    {
        ManagedViewHandle mapFile = createReadMapViewOfFile(fileMapping.get(), bigFileAlignedSize, remainingDataInByte);

        auto chunks = splitLastUnalignedBytes(mapFile.get(), remainingDataInByte);
        auto compressedChunks = compressChunks(std::move(chunks));
        writeCompressedChunksToFile(std::move(compressedChunks), outputFilePath);
        getFat(fat, std::move(compressedChunks));
    }

    fat.m_fileSize = bigFileSize1.QuadPart;
    fat.writeToFile(FAT_FILE_PATH);
}

size_t Compressor::zlibCompress(void* source, void* dest, size_t sourceBytesCount)
{
    int ret, flush;
    unsigned have;
    z_stream stream;
    void* currentDest = dest;

    uint8_t output[PAGE_SIZE];

    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    ret = deflateInit(&stream, COMPRESSION_LEVEL);
    assert(ret == Z_OK);

    flush = Z_FINISH;
    stream.avail_in = static_cast<uInt>(sourceBytesCount);
    stream.next_in = reinterpret_cast<Bytef*>(source);

    do
    {
        stream.avail_out = PAGE_SIZE;
        stream.next_out = output;

        ret = deflate(&stream, flush);
        assert(ret != Z_STREAM_ERROR);

        /// how many bytes have been written
        have = PAGE_SIZE - stream.avail_out;

        /// copy the output to dest
        if (have != 0)
        {
            memcpy(currentDest, output, have);
        }
        currentDest = reinterpret_cast<uint8_t*>(currentDest) + have;

    } while (stream.avail_out == 0);

    assert(ret == Z_STREAM_END);
    assert(stream.avail_in == 0);

    (void)deflateEnd(&stream);

    /// return the size of the compressed data
    uintptr_t compressedSize = reinterpret_cast<uintptr_t>(currentDest) - reinterpret_cast<uintptr_t>(dest);
    return compressedSize;
}

std::vector<std::unique_ptr<Chunk>> Compressor::splitFile(uint8_t* fileContent, size_t pageCount, size_t pageSize)
{
    std::vector<std::unique_ptr<Chunk>> result;

    for (size_t i = 0; i < pageCount; ++i)
    {
        result.emplace_back(std::make_unique<Chunk>(pageSize));
    }

    concurrency::parallel_for(size_t(0), size_t(pageCount), [&result, fileContent, &pageSize](size_t i)
    {
        void* currentChunkPtr = result[i]->m_memory.get();
        memcpy(currentChunkPtr, fileContent + i * pageSize, pageSize);
    });

    return result;
}

std::vector<std::unique_ptr<Chunk>> Compressor::compressChunks(std::vector<std::unique_ptr<Chunk>>&& chunks)
{
    std::vector<std::unique_ptr<Chunk>> result;
    result.resize(chunks.size());

    concurrency::parallel_for(size_t(0), chunks.size(), [this, &chunks, &result](size_t i)
    {
        std::unique_ptr<uint8_t[]> mem(reinterpret_cast<uint8_t*>(VirtualAlloc(nullptr, PAGE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)));

        uint64_t compressedSize = zlibCompress(chunks[i]->m_memory.get(), mem.get(), chunks[i]->chunkSize);

        result[i] = std::make_unique<Chunk>(mem.release(), compressedSize);
    });

    return result;
}

void Compressor::writeCompressedChunksToFile(std::vector<std::unique_ptr<Chunk>>&& compressedChunks, LPCWSTR filePath)
{
    ManagedHandle outputFile = createWriteFile(filePath);

    for (size_t i = 0; i < compressedChunks.size(); ++i)
    {
        uint8_t* chunkMem = reinterpret_cast<uint8_t*>(compressedChunks[i]->m_memory.get());
        size_t size = compressedChunks[i]->chunkSize;

        DWORD written;
        BOOL ret = WriteFile(outputFile.get(), chunkMem, static_cast<DWORD>(size), &written, nullptr);
        assert(ret == TRUE);
    }
}

void Compressor::getFat(Fat& fat, std::vector<std::unique_ptr<Chunk>>&& compressedChunks)
{
    if (fat.m_chunksSizes.size() == 0)
    {
        fat.m_chunksSizes.push_back(0);
    }

    for (size_t i = 0; i < compressedChunks.size(); ++i)
    {
        size_t prevOffset = fat.m_chunksSizes.back();
        fat.m_chunksSizes.push_back(compressedChunks[i]->chunkSize + prevOffset);
    }
}


std::vector<std::unique_ptr<Chunk>> Compressor::splitLastUnalignedBytes(void* mapViewOfLastChunk, size_t chunkSizeInBytes)
{
    size_t chunksCount = chunkSizeInBytes / PAGE_SIZE;
    size_t alignedBytesCount = alignDown(chunkSizeInBytes, PAGE_SIZE);
    size_t trailBytesCount = chunkSizeInBytes - alignedBytesCount;

    std::vector<std::unique_ptr<Chunk>> chunks;

    /// split the chunks
    if (chunksCount)
    {
        /// we have at least one chunk, so create the chunks and create the last chunk with remaining unaligned bytes
        chunks = splitFile(reinterpret_cast<uint8_t*>(mapViewOfLastChunk), chunksCount, PAGE_SIZE);
        mapViewOfLastChunk = reinterpret_cast<uint8_t*>(mapViewOfLastChunk) + alignedBytesCount;

        /// if we have trailing bytes that are less than PAGE_SIZE
        if (trailBytesCount)
        {
            auto lastUnalignedChunk = splitFile(reinterpret_cast<uint8_t*>(mapViewOfLastChunk), 1, trailBytesCount);
            chunks.emplace_back(std::move(lastUnalignedChunk[0]));
        }
    }
    else
    {
        /// the content are less than one chunk so just create 1 chunk of that size
        chunks.emplace_back(std::make_unique<Chunk>(chunkSizeInBytes));
        memcpy(chunks[0]->m_memory.get(), mapViewOfLastChunk, chunkSizeInBytes);
    }

    return chunks;
}