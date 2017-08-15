#pragma once

#include "pch.h"
#include "Fat.h"

class Decompressor
{
    struct DecompressTask
    {
        DecompressTask() : m_decompressSource(nullptr), m_decompressDest(nullptr), 
            m_sourceSize(0), m_destSize(0), m_destChunkIndex(0), m_dmaErrorCodeBuffer(nullptr), m_device(nullptr) {}

        DecompressTask(const DecompressTask& other)
            : m_decompressSource(other.m_decompressSource), m_decompressDest(other.m_decompressDest), m_sourceSize(other.m_sourceSize), 
            m_destSize(other.m_destSize), m_destChunkIndex(other.m_destChunkIndex), m_dmaErrorCodeBuffer(other.m_dmaErrorCodeBuffer), m_device(other.m_device)
        {
        }

        void initTask(UINT sourceSize, UINT destSize, size_t destIndex, UINT* dmaErrorCodeBuffer, uint8_t* sourceInitData, ID3D11DeviceX* device)
        {
            m_sourceSize = sourceSize;
            m_destSize = destSize;
            m_destChunkIndex = destIndex;
            m_dmaErrorCodeBuffer = dmaErrorCodeBuffer;
            m_device = device;

            m_decompressSource = createManagedMemShared<uint8_t>(
                VirtualAlloc(
                    nullptr,
                    sourceSize,
                    MEM_RESERVE | MEM_COMMIT | MEM_GRAPHICS | MEM_LARGE_PAGES,
                    PAGE_READWRITE | PAGE_GPU_COHERENT));

            m_decompressDest = createManagedMemShared<uint8_t>(
                VirtualAlloc(
                    nullptr,
                    destSize,
                    MEM_RESERVE | MEM_COMMIT | MEM_GRAPHICS | MEM_LARGE_PAGES,
                    PAGE_READWRITE | PAGE_GPU_COHERENT));

            ZeroMemory(m_decompressDest.get(), destSize);
            CopyMemory(m_decompressSource.get(), sourceInitData, sourceSize);
        }

        void doWork(ID3D11DmaEngineContextX* const dmaContext)
        {
            throwIfFailed(dmaContext->LZDecompressMemory(m_decompressDest.get(), m_decompressSource.get(), m_sourceSize, 0));
            dmaContext->CopyLastErrorCodeToMemory(m_dmaErrorCodeBuffer);

            /// insert fence and kick off
            UINT64 fence = dmaContext->InsertFence(0);
            //dmaContext->InsertWaitOnFence(0, fence);

            while (m_device->IsFencePending(fence))
            {
                SwitchToThread();
                //;
            }

            if (m_dmaErrorCodeBuffer[0] != 0)
            {
                char buf[30];
                snprintf(buf, 30, "Decompress error %d\n", m_dmaErrorCodeBuffer[0]);

                printToDebugger(buf);
                throw std::exception();
            }
        }

        ManagedMemShared<uint8_t> m_decompressSource;
        ManagedMemShared<uint8_t> m_decompressDest;
        UINT m_sourceSize;
        UINT m_destSize;
        size_t m_destChunkIndex;
        UINT* m_dmaErrorCodeBuffer;
        ID3D11DeviceX* m_device;
    };

public:
    Decompressor();

    void init(ID3D11DeviceX* const device);

    void decompress(LPCWSTR inputCompressedFilePath, LPCWSTR outputDecompressedFilePath, ID3D11DeviceX* const device);

    void doTasksFromQueue();

private:
    //void dmaDecompress(void* source, void* dest, UINT sourceSize, ID3D11DeviceX* const device);

    //std::vector<std::unique_ptr<Chunk>> decompressChunks(uint8_t* compressedFileContent, Fat& fat, size_t fatStartIndex);
    
    void decompressChunks1(uint8_t* compressedFileContent, Fat& fat, size_t fatStartIndex, ID3D11DeviceX* const device);

    size_t getViewSize(size_t fatIndex, ManagedMemArray<size_t>& fatChunksSizes);

    void writeDecompressedChunksToFile(std::vector<std::unique_ptr<Chunk>>&& decompressedChunks, LPCWSTR filePath);

    D3D11_DMA_ENGINE_CONTEXT_DESC createDmaContext2Desc();

    void createBigDecompressedFile(LPCWSTR fileName, size_t size);

    void submitTaskToQueue(uint8_t* compressedFileContent, size_t fatStartIndex, ID3D11DeviceX* const device);

    void writeTaskResultToFile(DecompressTask& task, HANDLE outputFile);
private:
    Microsoft::WRL::ComPtr<ID3D11DmaEngineContextX> m_dmaContext2;
    ManagedMemArray<UINT> m_dmaErrorCodeBuffer;
    Fat m_fat;

    //std::thread m_decompressTaskThread;
    //ThreadGuard m_decompressTaskThreadGuard;

    concurrency::concurrent_queue<DecompressTask> m_taskQueue;
};

