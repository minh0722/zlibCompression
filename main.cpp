#include <iostream>
#include <cstdio>
#include <cstdint>
#include <cassert>
#include <memory>
#include <vector>
#include <ppl.h>
#include <Windows.h>
#include <chrono>
#include <numeric>
#include "zlib.h"
#include "file_util.h"
#include "fat.h"
#include "CompressedFileMap.h"
using namespace std;
using namespace std::chrono;

#define CHRONO_BEGIN \
    auto t1 = high_resolution_clock::now();

#define CHRONO_END \
    auto t2 = high_resolution_clock::now(); \
    auto duration = duration_cast<milliseconds>(t2 - t1).count(); \
    cout << "Time: " << duration << " milliseconds" << endl;

struct Coord
{
    static uint32_t count;

    Coord()
    {
        x = count++;
        y = count++;
        z = count++;
    }

    uint32_t x, y, z;
};

uint32_t Coord::count = 0;

static const LPCTSTR FAT_FILE_PATH = L"compressedFat.fat";
static const LPCTSTR BIG_FILE_PATH = L"bigFile.bin";
static const LPCWSTR COMPRESSED_BIG_FILE = L"compressedBigFile.forge";
static const LPCWSTR DECOMPRESSED_BIG_FILE = L"decompressedBigFile.forge";
static const size_t PAGE_SIZE = 64 * 1024;
static const int COMPRESSION_LEVEL = 9;
static const size_t CHUNKS_PER_MAP_COUNT1 = 1024;
static const size_t MAP_SIZE1 = PAGE_SIZE * CHUNKS_PER_MAP_COUNT1;

static const char* TEST_FILE = "test.bin";
static const uint32_t TEST_MAP_CHUNK_SIZE = 10 * sizeof(Coord);
static const char* COMPRESSED_FILE = "testCompressed.bin";
static const LPCWSTR DECOMPRESSED_FILE = L"testDecompressed.bin";
static const uint32_t CHUNK_SIZE = sizeof(Coord) * 2;

void throwIfFailed(int ret)
{
    if (ret != Z_OK)
        throw std::exception();
}

void throwIfFalse(bool flag)
{
    if (!flag)
        throw std::exception();
}

void createFile()
{
    FILE* file;
    fopen_s(&file, reinterpret_cast<const char*>(BIG_FILE_PATH), "a+b");

    Coord* c = new Coord[9000];

    for (int i = 0; i < 10000 * 4; ++i)
        fwrite(c, sizeof(Coord), 9000, file);

    delete[] c;
}

void createSmallFile()
{
    FILE* file;
    fopen_s(&file, TEST_FILE, "a+b");

    Coord* c = new Coord[10];

    for (int i = 0; i < 10; ++i)
        fwrite(c, sizeof(Coord), 10, file);

    delete[] c;
}


struct Chunk
{
    struct Deleter
    {
        void operator()(void* ptr)
        {
            BOOL ret = VirtualFree(ptr, 0, MEM_RELEASE);
            throwIfFalse(ret);
        }
    };

    Chunk(size_t pageSize) : m_memory(VirtualAlloc(nullptr, pageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE), Deleter()) ,
        chunkSize(pageSize)
    {
    }

    Chunk(void* m, size_t pageSize) : m_memory(m, Deleter()), chunkSize(pageSize) 
    {
    }

    std::unique_ptr<void, Deleter> m_memory;
    size_t chunkSize;
};

LARGE_INTEGER getCountToAlignment(const LARGE_INTEGER& num, const size_t alignment)
{
    LARGE_INTEGER count = { 0 };
    count.QuadPart = align(num.QuadPart, alignment) - num.QuadPart;

    return count;
}

uint32_t compress(void* source, void* dest, size_t sourceBytesCount)
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
    stream.avail_in = sourceBytesCount;
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
    uint32_t compressedSize = reinterpret_cast<uint8_t*>(currentDest) - reinterpret_cast<uint8_t*>(dest);
    return compressedSize;
}

uint32_t decompress(void* source, void* dest, size_t sourceBytesCount)
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

std::vector<std::unique_ptr<Chunk>> splitFile(uint8_t* fileContent, uint32_t pageCount, size_t pageSize)
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

std::vector<std::unique_ptr<Chunk>> compressChunks(std::vector<std::unique_ptr<Chunk>>&& chunks)
{
    vector<std::unique_ptr<Chunk>> result;
    result.resize(chunks.size());

    concurrency::parallel_for(size_t(0), chunks.size(), [&chunks, &result](size_t i)
    {
        unique_ptr<uint8_t[]> mem(reinterpret_cast<uint8_t*>(VirtualAlloc(nullptr, PAGE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)));

        uint64_t compressedSize = compress(chunks[i]->m_memory.get(), mem.get(), chunks[i]->chunkSize);

        result[i] = std::make_unique<Chunk>(mem.release(), compressedSize);
    });
    
    return result;
}

void writeCompressedChunksToFile(std::vector<std::unique_ptr<Chunk>>&& compressedChunks)
{
    ManagedHandle outputFile = createWriteFile(COMPRESSED_BIG_FILE);

    for (size_t i = 0; i < compressedChunks.size(); ++i)
    {
        uint8_t* chunkMem = reinterpret_cast<uint8_t*>(compressedChunks[i]->m_memory.get());
        size_t size = compressedChunks[i]->chunkSize;

        DWORD written;
        BOOL ret = WriteFile(outputFile.get(), chunkMem, size, &written, nullptr);
        assert(ret == TRUE);
    }
}

size_t getTotalCompressionSize(std::vector<std::unique_ptr<Chunk>>&& compressedChunks)
{
    return std::accumulate(
        compressedChunks.begin(), 
        compressedChunks.end(), 
        0,
        [](size_t sum, std::unique_ptr<Chunk>& ptr) 
        {
            return sum + ptr->chunkSize;
        }
    );
}

void getFat(File::Fat& fat, std::vector<std::unique_ptr<Chunk>>&& compressedChunks)
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

std::vector<std::unique_ptr<Chunk>> splitLastUnalignedBytes(void* mapViewOfLastChunk, size_t chunkSizeInBytes)
{
    size_t chunksCount = chunkSizeInBytes / PAGE_SIZE;
    size_t alignedBytesCount = alignDown(chunkSizeInBytes, PAGE_SIZE);
    size_t trailBytesCount = chunkSizeInBytes - alignedBytesCount;

    std::vector<unique_ptr<Chunk>> chunks;

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

void compress()
{
    /*
    Create a mapping of the file.
    First we create a map view of size map-size-aligned and compress them.
    Then create map view starting from the beginning of the trail datas
    that are after the aligned chunks
    */

    /// open input file
    ManagedHandle bigFile1 = createReadFile(BIG_FILE_PATH);

    /// align down file size to map size
    LARGE_INTEGER bigFileSize1 = fileSize(bigFile1.get());
    LARGE_INTEGER bigFileAlignedSize = alignDown(bigFileSize1, MAP_SIZE1);

    /// how much map viewing we have to do and total number of chunks
    const size_t MAP_COUNT1 = bigFileAlignedSize.QuadPart / MAP_SIZE1;
    const size_t TOTAL_CHUNKS_COUNT = bigFileAlignedSize.QuadPart / PAGE_SIZE;

    /// mapping of the file
    ManagedHandle fileMapping = createReadFileMapping(bigFile1.get(), 0);

    /// contains offsets of the compressed chunks
    File::Fat fat;

    for (size_t i = 0; i < MAP_COUNT1; ++i)
    {
        /// the offset to the current map view of the file
        LARGE_INTEGER offset;
        offset.QuadPart = i * MAP_SIZE1;

        ManagedViewHandle mapFile1 = createReadMapViewOfFile(fileMapping.get(), offset, MAP_SIZE1);

        auto chunks = splitFile(reinterpret_cast<uint8_t*>(mapFile1.get()), CHUNKS_PER_MAP_COUNT1, PAGE_SIZE);
        auto compressedChunks = compressChunks(std::move(chunks));
        writeCompressedChunksToFile(std::move(compressedChunks));
        getFat(fat, std::move(compressedChunks));
    }

    /// now we need to compress the remaining unaligned datas
    size_t remainingDataInByte = bigFileSize1.QuadPart - bigFileAlignedSize.QuadPart;

    if (remainingDataInByte)
    {
        ManagedViewHandle mapFile = createReadMapViewOfFile(fileMapping.get(), bigFileAlignedSize, remainingDataInByte);

        auto chunks = splitLastUnalignedBytes(mapFile.get(), remainingDataInByte);
        auto compressedChunks = compressChunks(std::move(chunks));
        writeCompressedChunksToFile(std::move(compressedChunks));
        getFat(fat, std::move(compressedChunks));
    }

    fat.m_fileSize = bigFileSize1.QuadPart;
    fat.writeToFile(FAT_FILE_PATH);
}

std::vector<std::unique_ptr<Chunk>> decompressChunks(uint8_t* compressedFileContent, std::vector<size_t>& fatChunkSizes, size_t fatStartIndex)
{
    std::vector<std::unique_ptr<Chunk>> decompressedChunks(CHUNKS_PER_MAP_COUNT1);

    size_t fatEndIndex = (fatStartIndex + CHUNKS_PER_MAP_COUNT1) >= fatChunkSizes.size() ? fatChunkSizes.size() - 1 : fatStartIndex + CHUNKS_PER_MAP_COUNT1;

    //concurrency::parallel_for(fatStartIndex, fatEndIndex, [&fatChunkSizes, &decompressedChunks, &compressedFileContent, &fatStartIndex](size_t i)
    //{
    for (size_t i = fatStartIndex; i < fatEndIndex; ++i)
    {
        unique_ptr<uint8_t[]> mem(reinterpret_cast<uint8_t*>(VirtualAlloc(nullptr, PAGE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)));

        size_t compressedChunkSize = fatChunkSizes[i + 1] - fatChunkSizes[i];
        size_t offset = fatChunkSizes[i] - fatChunkSizes[fatStartIndex];

        size_t decompressedSize = decompress(compressedFileContent + offset, mem.get(), compressedChunkSize);

        decompressedChunks[i % CHUNKS_PER_MAP_COUNT1] = std::make_unique<Chunk>(mem.release(), decompressedSize);
    }
    //});

    return decompressedChunks;
}

size_t getViewSize(size_t fatIndex, std::vector<size_t>& fatChunksSizes)
{
    return fatChunksSizes[fatIndex + CHUNKS_PER_MAP_COUNT1] - fatChunksSizes[fatIndex];
}

void writeDecompressedChunksToFile(std::vector<std::unique_ptr<Chunk>>&& decompressedChunks)
{
    ManagedHandle outputFile = createWriteFile(DECOMPRESSED_BIG_FILE);

    for (size_t i = 0; i < decompressedChunks.size(); ++i)
    {
        uint8_t* chunkMem = reinterpret_cast<uint8_t*>(decompressedChunks[i]->m_memory.get());
        size_t size = decompressedChunks[i]->chunkSize;

        DWORD written;
        BOOL ret = WriteFile(outputFile.get(), chunkMem, size, &written, nullptr);
        assert(ret == TRUE);
    }
}

void decompress()
{
    File::Fat fat;
    fat.readFromFile(FAT_FILE_PATH);

    CompressedFileMap compressedFileMap(COMPRESSED_BIG_FILE);

    size_t fatIndex = 0;
    LARGE_INTEGER offset = {0};

    while(fatIndex + CHUNKS_PER_MAP_COUNT1 < fat.m_chunksSizes.size())
    {
        size_t viewSize = getViewSize(fatIndex, fat.m_chunksSizes);
        uint8_t* compressedFileContent = reinterpret_cast<uint8_t*>(compressedFileMap.readMem(fat.m_chunksSizes[fatIndex], viewSize));

        auto decompressedChunks = decompressChunks(compressedFileContent, fat.m_chunksSizes, fatIndex);
        writeDecompressedChunksToFile(std::move(decompressedChunks));

        fatIndex += CHUNKS_PER_MAP_COUNT1;
    }

    /// decompress the rest unaligned chunks
    if (fatIndex < fat.m_chunksSizes.size() - 1)
    {
        size_t viewSize = fat.m_chunksSizes.back() - fat.m_chunksSizes[fatIndex];
        uint8_t* compressedFileContent = reinterpret_cast<uint8_t*>(compressedFileMap.readMem(fat.m_chunksSizes[fatIndex], viewSize));

        auto decompressedChunks = decompressChunks(compressedFileContent, fat.m_chunksSizes, fatIndex);
        writeDecompressedChunksToFile(std::move(decompressedChunks));
    }

}

int main()
{
    //CHRONO_BEGIN;
    //compress();
    //CHRONO_END;

    decompress();

    return 0;

    
    
    
    
    
    //////////////////////////////////////////////////////////////////////////

    HANDLE bigFile = CreateFile(
        BIG_FILE_PATH,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,            /// open file if exist, else create new
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    assert(bigFile != INVALID_HANDLE_VALUE);
    
    LARGE_INTEGER bigFileSize;
    GetFileSizeEx(bigFile, &bigFileSize);


    const size_t CHUNKS_PER_MAP_COUNT = 1024;
    const size_t MAP_SIZE = PAGE_SIZE * CHUNKS_PER_MAP_COUNT;
    const size_t MAP_COUNT = bigFileSize.QuadPart / MAP_SIZE;   /// 64
        
    //CHRONO_BEGIN;

    for (size_t i = 0; i < MAP_COUNT; ++i)
    {
        /// the offset to the current mapping of the file
        LARGE_INTEGER offset;
        offset.QuadPart = i * MAP_SIZE;

        HANDLE fileMap = CreateFileMapping(bigFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
        LPVOID mapFile = MapViewOfFile(fileMap, FILE_MAP_READ, offset.HighPart, offset.LowPart, MAP_SIZE);
        assert(mapFile != nullptr);

        auto chunks = splitFile(reinterpret_cast<uint8_t*>(mapFile), CHUNKS_PER_MAP_COUNT, PAGE_SIZE);
        auto compressedChunks = compressChunks(std::move(chunks));
        writeCompressedChunksToFile(std::move(compressedChunks));


        CloseHandle(fileMap);
        UnmapViewOfFile(mapFile);
    }
    
    //CHRONO_END;

    CloseHandle(bigFile);
    

    ///// decompress
    //HANDLE decompressedBigFile = CreateFile(
    //    COMPRESSED_BIG_FILE,
    //    GENERIC_WRITE,
    //    0,
    //    nullptr,
    //    OPEN_ALWAYS,
    //    FILE_ATTRIBUTE_NORMAL,
    //    nullptr);
    //assert(decompressedBigFile != INVALID_HANDLE_VALUE);

    //LARGE_INTEGER decompressedFileSize;
    //GetFileSizeEx(decompressedBigFile, &decompressedFileSize);



    //std::vector<std::unique_ptr<Chunk>> chunks = splitFiles(reinterpret_cast<uint8_t*>(mapFile), 10);
    //std::vector<std::unique_ptr<Chunk>> compressedChunks = compressChunks(std::move(chunks));
    //std::vector<std::unique_ptr<Chunk>> decompressedChunks = decompressChunks(std::move(compressedChunks));
    //
    //for (size_t i = 0; i < chunks.size(); ++i)
    //{
    //    char* beforeCompress = reinterpret_cast<char*>(chunks[i]->m_memory.get());
    //    char* afterCompress = reinterpret_cast<char*>(decompressedChunks[i]->m_memory.get());
    //    size_t len = PAGE_SIZE;
    //
    //    assert(strncmp(beforeCompress, afterCompress, len) == 0);
    //}
    //
    //UnmapViewOfFile(mapFile);
    //CloseHandle(fileMap);
    //CloseHandle(inputFile);



    return 0;
}