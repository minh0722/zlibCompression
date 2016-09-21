#include <iostream>
#include <cstdio>
#include <cstdint>
#include <cassert>
#include <Windows.h>
#include "zlib.h"
using namespace std;

static const char* TEST_FILE = "test.bin";
static const char* FILE_PATH = "bigFile.bin";
static const size_t PAGE_SIZE = 64 * 1024;
static const size_t MAP_SIZE = PAGE_SIZE * 10;


namespace Creation
{
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

    void createFile()
    {
        FILE* file;
        fopen_s(&file, FILE_PATH, "a+b");

        Coord* c = new Coord[10000];

        for (int i = 0; i < 10000*4; ++i)
            fwrite(c, sizeof(Coord), 10000, file);

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

}

void throwIfFailed(int ret)
{
    if (ret != Z_OK)
        throw std::exception();
}



const uint32_t TEST_MAP_CHUNK_SIZE = 10 * sizeof(Creation::Coord);

OFSTRUCT ofstruct;
HANDLE inputFile = reinterpret_cast<HANDLE>(OpenFile(TEST_FILE, &ofstruct, OF_READ));
HANDLE fileMap = CreateFileMapping(inputFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
LPVOID mapFile = MapViewOfFile(fileMap, FILE_MAP_READ, 0, 0, TEST_MAP_CHUNK_SIZE);


void compress1()
{
    if (!mapFile)
        throw std::exception();

    const int deflateLevel = 6;
    int ret, flush;
    z_stream stream;

    const uint32_t CHUNK_SIZE = sizeof(Creation::Coord) * 2;

    uint8_t outputBuffer[TEST_MAP_CHUNK_SIZE];

    uint8_t* input = reinterpret_cast<uint8_t*>(mapFile);
    uint8_t* output = outputBuffer;

    stream.zalloc = nullptr;
    stream.zfree = nullptr;
    stream.opaque = nullptr;

    ret = deflateInit(&stream, deflateLevel);
    throwIfFailed(ret);
    
    int chunksCount = TEST_MAP_CHUNK_SIZE / CHUNK_SIZE;

    stream.next_out = output;
    for (int i = 0; i < chunksCount; ++i)
    {
        do
        {
            stream.avail_in = CHUNK_SIZE;
            stream.next_in = input + i * CHUNK_SIZE;
            stream.avail_out = CHUNK_SIZE;
            stream.next_out = output + stream.total_out;
            flush = Z_NO_FLUSH;

            if (i == chunksCount - 1)
            {
                flush = Z_FINISH;
            }

            ret = deflate(&stream, flush);
            assert(ret != Z_STREAM_ERROR);

        } while (stream.avail_out == 0);
    }

    FILE* compressedFile;
    fopen_s(&compressedFile, "testCompressed.bin", "w");

    fwrite(output, sizeof(uint8_t), stream.total_out, compressedFile);
    fclose(compressedFile);

    deflateEnd(&stream);
}

void decompress1()
{

}


int main()
{
    compress1();



    UnmapViewOfFile(mapFile);
    CloseHandle(inputFile);
    return 0;
}