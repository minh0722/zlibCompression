#include <iostream>
#include <cstdio>
#include <cstdint>
#include <cassert>
#include <memory>
#include <vector>
#include <ppl.h>
#include <Windows.h>
#include <chrono>
#include "zlib.h"
#include "file_util.h"
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
static const LPCTSTR BIG_FILE_PATH = L"DataPC.forge";
static const LPCWSTR COMPRESSED_BIG_FILE = L"compressedBigFile.forge";
static const size_t PAGE_SIZE = 64 * 1024;
static const int COMPRESSION_LEVEL = 9;

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

void compress1()
{
    OFSTRUCT ofstruct;
    HANDLE inputFile = reinterpret_cast<HANDLE>(OpenFile(TEST_FILE, &ofstruct, OF_READ));
    HANDLE fileMap = CreateFileMapping(inputFile, nullptr, PAGE_READONLY, 0, 0, nullptr);       /// file has 100 coords
    LPVOID mapFile = MapViewOfFile(fileMap, FILE_MAP_READ, 0, 0, TEST_MAP_CHUNK_SIZE);          /// map only 10 coords

    if (!mapFile)
        throw std::exception();

    const int deflateLevel = 6;
    int ret, flush;
    z_stream stream;


    uint8_t outputBuffer[TEST_MAP_CHUNK_SIZE];

    uint8_t* input = reinterpret_cast<uint8_t*>(mapFile);
    uint8_t* output = outputBuffer;

    stream.zalloc = nullptr;
    stream.zfree = nullptr;
    stream.opaque = nullptr;

    ret = deflateInit(&stream, deflateLevel);
    throwIfFailed(ret);

    int chunksCount = TEST_MAP_CHUNK_SIZE / CHUNK_SIZE;         /// compress 2 coords at a time

    stream.next_out = output;
    for (int i = 0; i < chunksCount; ++i)
    {
        stream.avail_in = CHUNK_SIZE;
        stream.next_in = input + i * CHUNK_SIZE;

        flush = Z_NO_FLUSH;

        if (i == chunksCount - 1)
        {
            flush = Z_FINISH;
        }

        do
        {
            stream.avail_out = CHUNK_SIZE;
            stream.next_out = output + stream.total_out;

            ret = deflate(&stream, flush);
            assert(ret != Z_STREAM_ERROR);

        } while (stream.avail_out == 0);
    }

    FILE* compressedFile;
    fopen_s(&compressedFile, COMPRESSED_FILE, "w");

    fwrite(output, sizeof(uint8_t), stream.total_out, compressedFile);
    fclose(compressedFile);

    deflateEnd(&stream);

    UnmapViewOfFile(mapFile);
    CloseHandle(fileMap);
    CloseHandle(inputFile);
}

void decompress1()
{
    OFSTRUCT ofstruct;
    HANDLE inputFile = reinterpret_cast<HANDLE>(OpenFile(COMPRESSED_FILE, &ofstruct, OF_READ));
    HANDLE fileMap = CreateFileMapping(inputFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
    LPVOID mapFile = MapViewOfFile(fileMap, FILE_MAP_READ, 0, 0, 0);

    HANDLE outputFile = CreateFile(
        DECOMPRESSED_FILE,              /// name of the file
        GENERIC_WRITE | GENERIC_READ,   /// open for read write
        0,                              /// do not share
        nullptr,                        /// default security
        CREATE_ALWAYS,                  /// create file and overwrite if already exist
        FILE_ATTRIBUTE_NORMAL,          /// normal file
        nullptr);                      /// no attr. template

    if (!mapFile || outputFile != INVALID_HANDLE_VALUE)
        throw std::exception();

    int ret;
    z_stream stream;
    uint8_t outputBuffer[TEST_MAP_CHUNK_SIZE];

    uint8_t* input = reinterpret_cast<uint8_t*>(mapFile);
    uint8_t* output = outputBuffer;

    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = 0;
    stream.next_in = Z_NULL;

    ret = inflateInit(&stream);
    throwIfFailed(ret);

    /// whole file is 55kb so inflate all
    stream.avail_out = TEST_MAP_CHUNK_SIZE;
    stream.next_out = output;

    stream.avail_in = TEST_MAP_CHUNK_SIZE;
    stream.next_in = input;

    ret = inflate(&stream, Z_NO_FLUSH);
    assert(ret != Z_STREAM_ERROR);

    switch (ret)
    {
    case Z_NEED_DICT:
        ret = Z_DATA_ERROR;
        break;
    case Z_DATA_ERROR:
    case Z_MEM_ERROR:
        (void)inflateEnd(&stream);
        return;
    }

    DWORD bytesWritten;
    BOOL writeFlag = WriteFile(outputFile, output, stream.total_out, &bytesWritten, nullptr);
    assert(writeFlag != false);

    (void)inflateEnd(&stream);

    UnmapViewOfFile(mapFile);
    CloseHandle(inputFile);
    CloseHandle(fileMap);
    CloseHandle(outputFile);
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

size_t align(const size_t num, const size_t alignment)
{
    return ((num + alignment - 1) / alignment) * alignment;
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
        memcpy(currentDest, output, have);
        currentDest = reinterpret_cast<uint8_t*>(currentDest) + have;

    } while (stream.avail_out == 0);

    assert(ret == Z_STREAM_END);

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

        have = sourceBytesCount - stream.avail_out;
        assert(have != 0);

        memcpy(currentDest, output, have);
        currentDest = reinterpret_cast<uint8_t*>(currentDest) + have;

    } while (stream.avail_in != 0);

    (void)inflateEnd(&stream);

    assert(ret == Z_STREAM_END);

    uint32_t decompressedSize = reinterpret_cast<uint8_t*>(currentDest) - reinterpret_cast<uint8_t*>(dest);
    return decompressedSize;
}

std::vector<std::unique_ptr<Chunk>> splitFile(uint8_t* fileContent, uint32_t pageCount)
{
    std::vector<std::unique_ptr<Chunk>> result;

    for (size_t i = 0; i < pageCount; ++i)
    {
        result.emplace_back(std::make_unique<Chunk>(PAGE_SIZE));
    }

    concurrency::parallel_for(size_t(0), size_t(pageCount), [&result, fileContent](size_t i)
    {
        void* currentChunkPtr = result[i]->m_memory.get();
        memcpy(currentChunkPtr, fileContent + i * PAGE_SIZE, PAGE_SIZE);
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

std::vector<std::unique_ptr<Chunk>> decompressChunks(std::vector<std::unique_ptr<Chunk>>&& compressedChunks)
{
    vector<std::unique_ptr<Chunk>> result;
    result.resize(compressedChunks.size());

    concurrency::parallel_for(size_t(0), compressedChunks.size(), [&compressedChunks, &result](size_t i)
    {
        uint32_t compressedChunkSize = compressedChunks[i]->chunkSize;

        unique_ptr<uint8_t[]> mem(reinterpret_cast<uint8_t*>(VirtualAlloc(nullptr, PAGE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)));

        uint32_t decompressedSize = decompress(compressedChunks[i]->m_memory.get(), mem.get(), compressedChunkSize);

        result[i] = std::make_unique<Chunk>(mem.release(), decompressedSize);
    });

    return result;
}

void writeCompressedChunksToFile(std::vector<std::unique_ptr<Chunk>>&& compressedChunks)
{
    HANDLE outputFile = CreateFile(
        COMPRESSED_BIG_FILE,
        FILE_APPEND_DATA,
        0,
        nullptr,
        OPEN_ALWAYS,                    /// open file if exist, else create new
        FILE_ATTRIBUTE_NORMAL,
        nullptr);


    for (size_t i = 0; i < compressedChunks.size(); ++i)
    {
        uint8_t* chunkMem = reinterpret_cast<uint8_t*>(compressedChunks[i]->m_memory.get());
        size_t size = compressedChunks[i]->chunkSize;

        DWORD written;
        BOOL ret = WriteFile(outputFile, chunkMem, size, &written, nullptr);
        assert(ret == TRUE);
    }

    CloseHandle(outputFile);
}

void writeCompressedChunksToFile(HANDLE compressedFile, std::vector<std::unique_ptr<Chunk>>&& compressedChunks)
{
    for (size_t i = 0; i < compressedChunks.size(); ++i)
    {
        uint8_t* chunkMem = reinterpret_cast<uint8_t*>(compressedChunks[i]->m_memory.get());
        size_t size = compressedChunks[i]->chunkSize;

        DWORD written;
        BOOL ret = WriteFile(compressedFile, chunkMem, size, &written, nullptr);
        assert(ret == TRUE);
    }

    CloseHandle(compressedFile);

}

size_t getTotalCompressionSize(std::vector<std::unique_ptr<Chunk>>&& compressedChunks)
{
    size_t sum = 0;
    for (size_t i = 0; i < compressedChunks.size(); ++i)
    {
        sum += compressedChunks[i]->chunkSize;
    }

    return sum;
}

void getFat(std::vector<size_t>& fat, std::vector<std::unique_ptr<Chunk>>&& compressedChunks, size_t fatStartingIndex, size_t fatEndIndex)
{
    for (size_t i = fatStartingIndex, compressedIndex = 0; i < fatEndIndex; ++i, ++compressedIndex)
    {
        fat[i] = compressedChunks[compressedIndex]->chunkSize;
    }
}

void writeFatToFile(std::vector<size_t>&& fat)
{
    size_t size = fat.size();

    File::ManagedHandle fatHandler = File::createWriteFile(FAT_FILE_PATH);

    BOOL ret = WriteFile(fatHandler.get(), fat.data(), size * sizeof(size_t), nullptr, nullptr);
    assert(ret == TRUE);
}

int main()
{
    File::ManagedHandle bigFile1 = File::createReadFile(BIG_FILE_PATH);
    
    LARGE_INTEGER bigFileSize1;
    GetFileSizeEx(bigFile1.get(), &bigFileSize1);
    bigFileSize1.QuadPart = align(bigFileSize1.QuadPart, PAGE_SIZE);

    const size_t CHUNKS_PER_MAP_COUNT1 = 1024;
    const size_t MAP_SIZE1 = PAGE_SIZE * CHUNKS_PER_MAP_COUNT1;
    const size_t MAP_COUNT1 = bigFileSize1.QuadPart / MAP_SIZE1;
    const size_t TOTAL_CHUNKS_COUNT = bigFileSize1.QuadPart / PAGE_SIZE;

    // TODO: fix, attempted to align but not working
    //const size_t CHUNKS_PER_MAP_COUNT1 = 1024;
    //const size_t TOTAL_CHUNKS_COUNT1 = align(bigFileSize1.QuadPart / PAGE_SIZE, CHUNKS_PER_MAP_COUNT1);
    //const size_t MAP_SIZE1 = PAGE_SIZE * CHUNKS_PER_MAP_COUNT1;
    //const size_t MAP_COUNT1 = TOTAL_CHUNKS_COUNT1 / CHUNKS_PER_MAP_COUNT1;

    File::ManagedHandle fileMap1 = File::createReadFileMapping(bigFile1.get());
    std::vector<size_t> fat;      /// contains size of the compressed chunks
    fat.resize(TOTAL_CHUNKS_COUNT);
    
    for (size_t i = 0; i < MAP_COUNT1; ++i)
    {
        /// the offset to the current mapping of the file
        LARGE_INTEGER offset;
        offset.QuadPart = i * MAP_SIZE1;

        File::ManagedFileMap mapFile1 = File::createReadMapViewOfFile(fileMap1.get(), offset, MAP_SIZE1);

        auto chunks = splitFile(reinterpret_cast<uint8_t*>(mapFile1.get()), CHUNKS_PER_MAP_COUNT1);
        auto compressedChunks = compressChunks(std::move(chunks));
        writeCompressedChunksToFile(std::move(compressedChunks));

        size_t fatStartingIndex = i * CHUNKS_PER_MAP_COUNT1;
        size_t fatEndIndex = fatStartingIndex + CHUNKS_PER_MAP_COUNT1;
        getFat(fat, std::move(compressedChunks), fatStartingIndex, fatEndIndex);
    }

    writeFatToFile(std::move(fat));
    


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
        
    CHRONO_BEGIN;

    for (size_t i = 0; i < MAP_COUNT; ++i)
    {
        /// the offset to the current mapping of the file
        LARGE_INTEGER offset;
        offset.QuadPart = i * MAP_SIZE;

        HANDLE fileMap = CreateFileMapping(bigFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
        LPVOID mapFile = MapViewOfFile(fileMap, FILE_MAP_READ, offset.HighPart, offset.LowPart, MAP_SIZE);
        assert(mapFile != nullptr);

        auto chunks = splitFile(reinterpret_cast<uint8_t*>(mapFile), CHUNKS_PER_MAP_COUNT);
        auto compressedChunks = compressChunks(std::move(chunks));
        writeCompressedChunksToFile(std::move(compressedChunks));


        CloseHandle(fileMap);
        UnmapViewOfFile(mapFile);
    }
    
    CHRONO_END;

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