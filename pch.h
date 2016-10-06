#pragma once
#include <iostream>
#include <cstdint>
#include <chrono>
#include <Windows.h>
#include <memory>
#include <cassert>
#include <vector>
#include "zlib.h"

static const LPCTSTR FAT_FILE_PATH = L"compressedFat.fat";
static const LPCTSTR BIG_FILE_PATH = L"bigFile.bin";
static const LPCWSTR COMPRESSED_BIG_FILE = L"compressedBigFile.bin";
static const LPCWSTR DECOMPRESSED_BIG_FILE = L"decompressedBigFile.bin";
static const size_t PAGE_SIZE = 64 * 1024;
static const int COMPRESSION_LEVEL = 9;
static const size_t CHUNKS_PER_MAP_COUNT1 = 1024;
static const size_t MAP_SIZE1 = PAGE_SIZE * CHUNKS_PER_MAP_COUNT1;

namespace
{
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

        Chunk(size_t pageSize) : m_memory(VirtualAlloc(nullptr, pageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE), Deleter()),
            chunkSize(pageSize)
        {
        }

        Chunk(void* m, size_t pageSize) : m_memory(m, Deleter()), chunkSize(pageSize)
        {
        }

        std::unique_ptr<void, Deleter> m_memory;
        size_t chunkSize;
    };
}

namespace
{
    size_t align(const size_t num, const size_t alignment)
    {
        return ((num + alignment - 1) / alignment) * alignment;
    }

    size_t alignDown(const size_t num, const size_t alignment)
    {
        return (num / alignment) * alignment;
    }

    LARGE_INTEGER alignDown(const LARGE_INTEGER& size, const size_t alignment)
    {
        LARGE_INTEGER newSize;
        newSize.QuadPart = alignDown(size.QuadPart, alignment);

        return newSize;
    }

    bool isAligned(const size_t num, const size_t alignment)
    {
        return (num / alignment) * alignment == num;
    }

    bool isAligned(const LARGE_INTEGER& num, const size_t alignment)
    {
        return isAligned(num.QuadPart, alignment);
    }
}

namespace
{
    struct FileHandleCloser
    {
        void operator()(HANDLE handle)
        {
            if (handle)
            {
                CloseHandle(handle);
            }
        }
    };

    struct ViewOfFileCloser
    {
        void operator()(void* file)
        {
            if (file)
            {
                UnmapViewOfFile(file);
            }
        }
    };

    using ManagedHandle = std::unique_ptr<void, FileHandleCloser>;
    using ManagedViewHandle = std::unique_ptr<void, ViewOfFileCloser>;

    HANDLE safeHandle(HANDLE handle)
    {
        return handle == INVALID_HANDLE_VALUE ? 0 : handle;
    }

    ManagedHandle createReadFile(LPCWSTR fileName)
    {
        ManagedHandle file(
            safeHandle(CreateFile(
                fileName,
                GENERIC_READ,
                0,
                nullptr,
                OPEN_ALWAYS,            /// open file if exist, else create new
                FILE_ATTRIBUTE_NORMAL,
                nullptr)),
            FileHandleCloser());

        if (!file)
        {
            throw std::exception();
        }

        return file;
    }

    ManagedHandle createWriteFile(LPCWSTR fileName)
    {
        ManagedHandle file(
            safeHandle(
                CreateFile(fileName, FILE_APPEND_DATA, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)),
                FileHandleCloser());

        if (!file)
        {
            throw std::exception();
        }

        return file;
    }

    ManagedHandle createReadFileMapping(HANDLE file, size_t mapSize)
    {
        LARGE_INTEGER size;
        size.QuadPart = mapSize;

        ManagedHandle fileMap(
            safeHandle(CreateFileMapping(file, nullptr, PAGE_READONLY, size.HighPart, size.LowPart, nullptr)),
            FileHandleCloser());

        if (!fileMap)
        {
            throw std::exception();
        }

        return fileMap;
    }

    ManagedHandle createWriteFileMapping(HANDLE file, size_t mapSize)
    {
        LARGE_INTEGER size;
        size.QuadPart = mapSize;

        ManagedHandle fileMap(
            safeHandle(CreateFileMapping(file, nullptr, PAGE_READWRITE, size.HighPart, size.LowPart, nullptr)),
            FileHandleCloser());

        if (!fileMap)
        {
            throw std::exception();
        }

        return fileMap;
    }

    ManagedViewHandle createReadMapViewOfFile(HANDLE fileMapping, LARGE_INTEGER mapOffset, size_t mapSize)
    {
        ManagedViewHandle m(
            MapViewOfFile(fileMapping, FILE_MAP_READ, mapOffset.HighPart, mapOffset.LowPart, mapSize),
            ViewOfFileCloser());

        if (!m)
        {
            throw std::exception();
        }

        return m;
    }

    ManagedViewHandle createWriteMapViewOfFile(HANDLE fileMapping, LARGE_INTEGER mapOffset, size_t mapSize)
    {
        ManagedViewHandle m(
            MapViewOfFile(fileMapping, FILE_MAP_WRITE, mapOffset.HighPart, mapOffset.LowPart, mapSize),
            ViewOfFileCloser());

        if (!m)
        {
            throw std::exception();
        }

        return m;
    }


    LARGE_INTEGER fileSize(const HANDLE& file)
    {
        LARGE_INTEGER size;
        GetFileSizeEx(file, &size);

        return size;
    }

    void setFileSize(const HANDLE& file, size_t extendSize)
    {
        LARGE_INTEGER size;
        size.QuadPart = extendSize;

        auto ret = SetFilePointerEx(file, size, nullptr, FILE_BEGIN);
        assert(ret != INVALID_SET_FILE_POINTER);

        SetEndOfFile(file);

        /// reset back pointer to beginning of file
        ret = SetFilePointerEx(file, { 0 }, nullptr, FILE_BEGIN);
        assert(ret != INVALID_SET_FILE_POINTER);
    }
}

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
        fopen_s(&file, reinterpret_cast<const char*>(BIG_FILE_PATH), "a+b");

        Coord* c = new Coord[9000];

        for (int i = 0; i < 10000 * 4; ++i)
            fwrite(c, sizeof(Coord), 9000, file);

        delete[] c;
    }
}