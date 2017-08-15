#include "pch.h"
#include "Decompressor.h"
#include "CompressedFileMap.h"
#include <ppl.h>
#include <concurrent_queue.h>

Decompressor::Decompressor() 
    //: m_decompressTaskThreadGuard(m_decompressTaskThread)
    //: m_decompressTaskThread(&Decompressor::doTasksFromQueue, this),
    //  m_decompressTaskThreadGuard(m_decompressTaskThread)
{
    //m_decompressTaskThread = std::thread(&Decompressor::doTasksFromQueue, this);
}

void Decompressor::init(ID3D11DeviceX* const device)
{
    auto desc = createDmaContext2Desc();
    throwIfFailed(device->CreateDmaEngineContext(&desc, m_dmaContext2.GetAddressOf()));

    m_dmaErrorCodeBuffer = ManagedMemArray<UINT>(
        reinterpret_cast<UINT*>(
            VirtualAlloc(
                nullptr,
                64 * 1024,
                MEM_RESERVE | MEM_COMMIT | MEM_GRAPHICS | MEM_LARGE_PAGES,
                PAGE_READWRITE | PAGE_GPU_COHERENT)));

    assert(m_dmaErrorCodeBuffer != nullptr);

    m_fat.readFromFile(FAT_FILE);
}

void Decompressor::decompress(LPCWSTR inputCompressedFilePath, LPCWSTR outputDecompressedFilePath, ID3D11DeviceX* const device)
{
    //Fat fat;
    //fat.readFromFile(L"DataPCFat.fat");

    createBigDecompressedFile(OUTPUT_DECOMPRESSED_FILE, m_fat.m_originalFileSize);

    CompressedFileMap compressedFileMap(inputCompressedFilePath);

    size_t fatIndex = 0;

    while (fatIndex + CHUNKS_PER_MAP_COUNT < m_fat.m_chunksOffsetsCount)
    {
        size_t viewSize = getViewSize(fatIndex, m_fat.m_chunksOffsets);
        uint8_t* compressedFileContent = reinterpret_cast<uint8_t*>(compressedFileMap.readMem(m_fat.m_chunksOffsets[fatIndex], viewSize));

        submitTaskToQueue(compressedFileContent, fatIndex, device);
        //auto decompressedChunks = decompressChunks1(compressedFileContent, m_fat, fatIndex, device);
        //writeDecompressedChunksToFile(std::move(decompressedChunks), outputDecompressedFilePath);

        fatIndex += CHUNKS_PER_MAP_COUNT;
    }

    /// decompress the rest unaligned chunks
    if (fatIndex < m_fat.m_chunksOffsetsCount - 1)
    {
        size_t viewSize = m_fat.m_chunksOffsets[m_fat.m_chunksOffsetsCount - 1] - m_fat.m_chunksOffsets[fatIndex];
        uint8_t* compressedFileContent = reinterpret_cast<uint8_t*>(compressedFileMap.readMem(m_fat.m_chunksOffsets[fatIndex], viewSize));

        submitTaskToQueue(compressedFileContent, fatIndex, device);
        //auto decompressedChunks = decompressChunks1(compressedFileContent, m_fat, fatIndex, device);
        //writeDecompressedChunksToFile(std::move(decompressedChunks), outputDecompressedFilePath);
    }

}

//void Decompressor::dmaDecompress( void* source, void* dest, UINT sourceSize, ID3D11DeviceX* const device)
//{
//    throwIfFailed(m_dmaContext2->LZDecompressMemory(dest, source, sourceSize, 0));
//    m_dmaContext2->CopyLastErrorCodeToMemory(&m_dmaErrorCodeBuffer[0]);
//
//    /// insert fence and kick off
//    UINT64 fence = m_dmaContext2->InsertFence(0);
//    m_dmaContext2->InsertWaitOnFence(0, fence);
//    
//    //while (device->IsFencePending(fence))
//    //{
//    //    SwitchToThread();
//    //}
//
//    if (m_dmaErrorCodeBuffer[0] != 0)
//    {
//        throw std::exception();
//    }
//}

//std::vector<std::unique_ptr<Chunk>> Decompressor::decompressChunks(uint8_t * compressedFileContent, Fat& fat, size_t fatStartIndex)
//{
//    std::vector<std::unique_ptr<Chunk>> decompressedChunks;
//
//    size_t fatEndIndex = (fatStartIndex + CHUNKS_PER_MAP_COUNT) >= fat.m_chunksOffsetsCount ? fat.m_chunksOffsetsCount - 1 : fatStartIndex + CHUNKS_PER_MAP_COUNT;
//
//    decompressedChunks.resize(fatEndIndex - fatStartIndex);
//
//    //concurrency::parallel_for(fatStartIndex, fatEndIndex, [this, &fat, &decompressedChunks, &compressedFileContent, &fatStartIndex](size_t i)
//    //{
//    for (size_t i = fatStartIndex; i < fatEndIndex; ++i)
//    {
//        std::unique_ptr<uint8_t[]> mem(
//            reinterpret_cast<uint8_t*>(
//                VirtualAlloc(
//                    nullptr,
//                    PAGE_SIZE,
//                    MEM_RESERVE | MEM_COMMIT | MEM_GRAPHICS | MEM_LARGE_PAGES,
//                    PAGE_READWRITE | PAGE_GPU_COHERENT)));
//        memset(mem.get(), 0, PAGE_SIZE);
//
//        size_t compressedChunkSize = fat.m_chunksOffsets[i + 1] - fat.m_chunksOffsets[i];
//        size_t offset = fat.m_chunksOffsets[i] - fat.m_chunksOffsets[fatStartIndex];
//
//        UINT64 fence = dmaDecompress(compressedFileContent + offset, mem.get(), compressedChunkSize);
//        size_t decompressedSize = strlen(reinterpret_cast<const char*>(mem.get()));
//
//        decompressedChunks[i % CHUNKS_PER_MAP_COUNT] = std::make_unique<Chunk>(mem.release(), decompressedSize);
//    }
//    //});
//
//    return decompressedChunks;
//}

void Decompressor::decompressChunks1(uint8_t* compressedFileContent, Fat& fat, size_t fatStartIndex, ID3D11DeviceX* const device)
{
    //std::vector<std::unique_ptr<Chunk>> decompressedChunks;

    size_t fatEndIndex = (fatStartIndex + CHUNKS_PER_MAP_COUNT) >= fat.m_chunksOffsetsCount 
        ? fat.m_chunksOffsetsCount - 1 
        : fatStartIndex + CHUNKS_PER_MAP_COUNT;
    
    //decompressedChunks.resize(fatEndIndex - fatStartIndex);

    concurrency::concurrent_queue<DecompressTask> taskQueue;

    concurrency::parallel_for(fatStartIndex, fatEndIndex, [this, &fat/*, &decompressedChunks*/, &compressedFileContent, &fatStartIndex, &fatEndIndex, &device, &taskQueue](size_t i)
    {
    //for (size_t i = fatStartIndex; i < fatEndIndex; ++i)
    //{
        size_t compressedChunkSize = fat.m_chunksOffsets[i + 1] - fat.m_chunksOffsets[i] - sizeof(UINT);
        
        size_t decompressDestSize = (fatEndIndex == fat.m_chunksOffsetsCount - 1 && i == fatEndIndex - 1 && fat.m_lastChunkSizeBeforeCompression) 
            ? fat.m_lastChunkSizeBeforeCompression 
            : PAGE_SIZE;

        size_t offset = fat.m_chunksOffsets[i] - fat.m_chunksOffsets[fatStartIndex] + sizeof(UINT);
        size_t decompressedChunkIndex = i % CHUNKS_PER_MAP_COUNT;
        uint8_t* compressedChunkInitialData = compressedFileContent + offset;

        //ManagedMem<uint8_t> decompressSource(
        //    reinterpret_cast<uint8_t*>(
        //        VirtualAlloc(
        //            nullptr,
        //            compressedChunkSize,
        //            MEM_RESERVE | MEM_COMMIT | MEM_GRAPHICS | MEM_LARGE_PAGES,
        //            PAGE_READWRITE | PAGE_GPU_COHERENT)));
        //
        //ManagedMem<uint8_t> decompressDest(
        //    reinterpret_cast<uint8_t*>(
        //        VirtualAlloc(
        //            nullptr,
        //            decompressDestSize,
        //            MEM_RESERVE | MEM_COMMIT | MEM_GRAPHICS | MEM_LARGE_PAGES,
        //            PAGE_READWRITE | PAGE_GPU_COHERENT)));
        //
        //ZeroMemory(decompressDest.get(), decompressDestSize);
        //CopyMemory(decompressSource.get(), compressedFileContent + offset, compressedChunkSize);

        DecompressTask task;
        task.initTask(
            compressedChunkSize, 
            decompressDestSize, 
            decompressedChunkIndex, 
            m_dmaErrorCodeBuffer.get(), 
            compressedChunkInitialData,
            device);

        taskQueue.push(std::move(task));

        //dmaDecompress(decompressSource.get(), decompressDest.get(), compressedChunkSize, device);        
        //decompressedChunks[i % CHUNKS_PER_MAP_COUNT] = std::make_unique<Chunk>(decompressDest.release(), decompressDestSize);
    //}
    });
    
    //while (!taskQueue.empty())
    //{
    //    DecompressTask task;
    //    taskQueue.try_pop(task);

    //    task.doWork(m_dmaContext2.Get());

    //    decompressedChunks[task.m_destChunkIndex] = std::make_unique<Chunk>(task.m_decompressDest, task.m_destSize);
    //}

    //return decompressedChunks;
}

size_t Decompressor::getViewSize(size_t fatIndex, ManagedMemArray<size_t>& fatChunksOffsets)
{
    return fatChunksOffsets[fatIndex + CHUNKS_PER_MAP_COUNT] - fatChunksOffsets[fatIndex];
}

void Decompressor::writeDecompressedChunksToFile(std::vector<std::unique_ptr<Chunk>>&& decompressedChunks, LPCWSTR filePath)
{
    ManagedHandle outputFile = createWriteFile(filePath);

    for (size_t i = 0; i < decompressedChunks.size(); ++i)
    {
        uint8_t* chunkMem = reinterpret_cast<uint8_t*>(decompressedChunks[i]->m_memory.get());
        DWORD size = static_cast<DWORD>(decompressedChunks[i]->chunkSize);

        DWORD written;
        BOOL ret = WriteFile(outputFile.get(), chunkMem, size, &written, nullptr);
        assert(ret == TRUE);
    }
}

D3D11_DMA_ENGINE_CONTEXT_DESC Decompressor::createDmaContext2Desc()
{
    D3D11_DMA_ENGINE_CONTEXT_DESC desc = {};
    desc.CreateFlags = D3D11_DMA_ENGINE_CONTEXT_CREATE_SDMA_2;

    return desc;
}

void Decompressor::createBigDecompressedFile(LPCWSTR fileName, size_t size)
{
    DeleteFile(OUTPUT_DECOMPRESSED_FILE);

    HANDLE file = CreateFile(
        fileName,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    LARGE_INTEGER fileSize;
    fileSize.QuadPart = size;

    auto ret = SetFilePointer(file, fileSize.LowPart, &fileSize.HighPart, FILE_BEGIN);
    assert(ret != INVALID_SET_FILE_POINTER);

    SetEndOfFile(file);
    CloseHandle(file);
}

void Decompressor::doTasksFromQueue()
{
    std::chrono::time_point<std::chrono::steady_clock> t1;
    std::chrono::time_point<std::chrono::steady_clock> t2;
    size_t chunksCount = 0;

    DeleteFile(L"timeLog.txt");
    DeleteFile(L"timeLog.bin");

    ManagedHandle file;
    do
    {
        file = ManagedHandle(
            CreateFile(
                OUTPUT_DECOMPRESSED_FILE,
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr));
    } while (file.get() == INVALID_HANDLE_VALUE);

    while (true)
    {
        DecompressTask task;
        BOOL flag = m_taskQueue.try_pop(task);
        
        if (flag)
        {
            if (chunksCount == 0)
            {
                t1 = std::chrono::high_resolution_clock::now();
            }

            task.doWork(m_dmaContext2.Get());
            ++chunksCount;

            writeTaskResultToFile(task, file.get());

            if (chunksCount == m_fat.m_chunksOffsetsCount - 1)
            {
                t2 = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
                //char buf[30];
                //snprintf(buf, 30, "Time: %d\n", duration);

                writeNumberToFile(L"timeLog.bin", duration);

                return;
            }
        }
    }
}

void Decompressor::submitTaskToQueue(uint8_t* compressedFileContent, size_t fatStartIndex, ID3D11DeviceX* const device)
{
    size_t fatEndIndex = (fatStartIndex + CHUNKS_PER_MAP_COUNT) >= m_fat.m_chunksOffsetsCount
        ? m_fat.m_chunksOffsetsCount - 1
        : fatStartIndex + CHUNKS_PER_MAP_COUNT;

    concurrency::parallel_for(fatStartIndex, fatEndIndex, [this, &compressedFileContent, &fatStartIndex, &fatEndIndex, &device](size_t i)
    {
        size_t offset = m_fat.m_chunksOffsets[i] - m_fat.m_chunksOffsets[fatStartIndex] + sizeof(UINT);

        size_t compressedChunkSize = m_fat.m_chunksOffsets[i + 1] - m_fat.m_chunksOffsets[i] - sizeof(UINT);

        size_t decompressDestSize = (fatEndIndex == m_fat.m_chunksOffsetsCount - 1 && i == fatEndIndex - 1 && m_fat.m_lastChunkSizeBeforeCompression)
            ? m_fat.m_lastChunkSizeBeforeCompression
            : PAGE_SIZE;

        size_t decompressedChunkIndex = i;
        uint8_t* compressedChunkInitialData = compressedFileContent + offset;

        DecompressTask task;
        task.initTask(
            compressedChunkSize,
            decompressDestSize,
            decompressedChunkIndex,
            m_dmaErrorCodeBuffer.get(),
            compressedChunkInitialData,
            device);

        m_taskQueue.push(std::move(task));
    });
}

void Decompressor::writeTaskResultToFile(DecompressTask& task, HANDLE outputFile)
{
    LARGE_INTEGER moveOffset;
    moveOffset.QuadPart = task.m_destChunkIndex * PAGE_SIZE;

    auto ret = SetFilePointerEx(outputFile, moveOffset, nullptr, FILE_BEGIN);
    assert(ret != INVALID_SET_FILE_POINTER);

    DWORD written;
    BOOL flag = WriteFile(outputFile, task.m_decompressDest.get(), task.m_destSize, &written, nullptr);
    assert(flag);
}
