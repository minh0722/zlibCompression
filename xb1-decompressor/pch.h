//
// pch.h
// Header for standard system include files.
//

#pragma once

// Use the C++ standard templated min/max
#define NOMINMAX

#include <xdk.h>
#include <wrl.h>
#include <d3d11_x.h>
#include <DirectXMath.h>
#include <DirectXColors.h>

#include <algorithm>
#include <memory>
#include <exception>

#include <pix.h>

#include <vector>
#include <cassert>
#include <memory>
#include <chrono>
#include <thread>
#include <ppl.h>
#include <concurrent_queue.h>

static const size_t PAGE_SIZE = 64 * 1024;
static const int COMPRESSION_LEVEL = 9;
static const size_t CHUNKS_PER_MAP_COUNT = 10;
static const size_t MAP_SIZE = PAGE_SIZE * CHUNKS_PER_MAP_COUNT;
static const LPCWSTR INPUT_COMPRESSED_FILE = L"DataPCCompressed.forge";
static const LPCWSTR OUTPUT_DECOMPRESSED_FILE = L"DataPCDecompressed.forge";
static const LPCWSTR FAT_FILE = L"DataPCFat.fat";


namespace DX
{
    inline void ThrowIfFailed(HRESULT hr)
    {
        if (FAILED(hr))
        {
            // Set a breakpoint on this line to catch DirectX API errors
            throw Platform::Exception::CreateException(hr);
        }
    }
}

inline void throwIfFailed(HRESULT hr)
{
    if (hr != S_OK)
    {
        throw std::exception();
    }
}

inline void throwIfFalse(bool flag)
{
    if (!flag)
    {
        throw std::exception();
    }
}

inline void throwIfFalse(BOOL flag)
{
    if (!flag)
    {
        throw std::exception();
    }
}

namespace
{
    class ThreadGuard
    {
    public:
        ThreadGuard(std::thread& thread) : m_thread(thread) {}

        ~ThreadGuard() 
        { 
            if (m_thread.joinable())
            {
                m_thread.join();
            }
        }

        ThreadGuard(const ThreadGuard&) = delete;
        ThreadGuard& operator=(const ThreadGuard&) = delete;

    private:
        std::thread& m_thread;
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

    void printToDebugger(char* msg)
    {
        OutputDebugStringA(msg);
    }
}

namespace
{
    struct MemCloser
    {
        void operator()(void* ptr)
        {
            if (ptr)
            {
                VirtualFree(ptr, 0, MEM_RELEASE);
            }
        }
    };

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

    template <typename T>
    using ManagedMemArray = std::unique_ptr<T[], MemCloser>;

    template <typename T>
    using ManagedMem = std::unique_ptr<T, MemCloser>;

    template <typename T>
    using ManagedMemShared = std::shared_ptr<T>;

    using ManagedHandle = std::unique_ptr<void, FileHandleCloser>;

    template <typename T>
    ManagedMemShared<T> createManagedMemShared(void* mem)
    {
        return ManagedMemShared<T>(reinterpret_cast<T*>(mem), MemCloser());
    }

    struct Chunk
    {
        Chunk(size_t pageSize)
        {
            m_memory = createManagedMemShared<uint8_t>(
                VirtualAlloc(
                    nullptr,
                    pageSize,
                    MEM_RESERVE | MEM_COMMIT | MEM_GRAPHICS | MEM_LARGE_PAGES,
                    PAGE_READWRITE | PAGE_GPU_COHERENT));
            chunkSize = pageSize;
        }

        Chunk(Chunk&& other)
        {
            m_memory = std::move(other.m_memory);
            chunkSize = other.chunkSize;
        }

        Chunk(const Chunk& other)
        {
            m_memory = other.m_memory;
            chunkSize = other.chunkSize;
        }

        Chunk(uint8_t* m, size_t pageSize)
        {
            m_memory = createManagedMemShared<uint8_t>(m);
            chunkSize = pageSize;
        }

        Chunk(ManagedMemShared<uint8_t>& ptr, size_t pageSize)
        {
            m_memory = ptr;
            chunkSize = pageSize;
        }

        ManagedMemShared<uint8_t> m_memory;
        size_t chunkSize;
    };


    HANDLE safeHandle(HANDLE handle)
    {
        return handle == INVALID_HANDLE_VALUE ? 0 : handle;
    }

    ManagedHandle createReadFile(LPCWSTR fileName, size_t offset)
    {
        ManagedHandle handle(
            safeHandle(
                CreateFile(
                    fileName,
                    GENERIC_READ,
                    FILE_SHARE_READ,
                    nullptr,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr)),
                FileHandleCloser());

        if (!handle)
        {
            throw std::exception();
        }

        LARGE_INTEGER largeOffset = { offset };

        auto ret = SetFilePointer(handle.get(), largeOffset.LowPart, &largeOffset.HighPart, FILE_BEGIN);

        assert(ret != INVALID_SET_FILE_POINTER);

        return handle;
    }

    ManagedHandle createWriteFile(LPCWSTR fileName)
    {
        ManagedHandle file(
            safeHandle(
                CreateFile(
                    fileName, 
                    FILE_APPEND_DATA, 
                    0, 
                    nullptr, 
                    OPEN_ALWAYS, 
                    FILE_ATTRIBUTE_NORMAL, 
                    nullptr)),
                FileHandleCloser());

        if (!file)
        {
            throw std::exception();
        }

        return file;
    }

    LARGE_INTEGER fileSize(const HANDLE& file)
    {
        LARGE_INTEGER size;
        GetFileSizeEx(file, &size);

        return size;
    }

    void writeNumberToFile(LPCWSTR fileName, size_t num)
    {
        ManagedHandle file = createWriteFile(fileName);

        DWORD written;
        BOOL ret = WriteFile(file.get(), &num, sizeof(num), &written, nullptr);
        throwIfFalse(ret);
    }
}
