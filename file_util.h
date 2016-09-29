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
    using ManagedFileMap = std::unique_ptr<void, ViewOfFileCloser>;

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

    ManagedHandle createReadFileMapping(HANDLE file)
    {
        ManagedHandle fileMap(
            safeHandle(CreateFileMapping(file, nullptr, PAGE_READONLY, 0, 0, nullptr)),
            FileHandleCloser());

        if (!fileMap)
        {
            throw std::exception();
        }

        return fileMap;
    }

    ManagedFileMap createReadMapViewOfFile(HANDLE fileMapping, LARGE_INTEGER mapOffset, size_t mapSize)
    {
        ManagedFileMap m(
            MapViewOfFile(fileMapping, FILE_MAP_READ, mapOffset.HighPart, mapOffset.LowPart, mapSize),
            ViewOfFileCloser());

        if (!m)
        {
            throw std::exception();
        }

        return m;
    }

}