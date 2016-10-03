#pragma once
#include <Windows.h>
#include <memory>

namespace File
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