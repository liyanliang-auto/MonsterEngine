// Copyright Monster Engine. All Rights Reserved.

#include "Renderer/AsyncTextureLoader.h"
#include "Core/Logging/LogMacros.h"
#include "Core/HAL/FMemory.h"
#include "Math/MonsterMath.h"

// stb_image for texture loading
#include "../3rd-party/stb/stb_image.h"

namespace MonsterEngine
{

// Declare log category
DECLARE_LOG_CATEGORY_EXTERN(LogAsyncTextureLoader, Log, All);
DEFINE_LOG_CATEGORY(LogAsyncTextureLoader);

// ============================================================================
// FTextureLoadResult Implementation
// ============================================================================

void FTextureLoadResult::Cleanup()
{
    for (uint32 i = 0; i < MipData.Num(); ++i)
    {
        if (MipData[i])
        {
            FMemory::Free(MipData[i]);
        }
    }
    MipData.Empty();
    MipSizes.Empty();
}

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Calculate the number of mip levels for a texture
 */
static uint32 CalculateMipLevels(uint32 Width, uint32 Height)
{
    uint32 MaxDimension = FMath::Max(Width, Height);
    uint32 MipLevels = 1;
    
    while (MaxDimension > 1)
    {
        MaxDimension >>= 1;
        MipLevels++;
    }
    
    return MipLevels;
}

/**
 * Downsample image using simple 2x2 box filter
 */
static void DownsampleBoxFilter(
    const unsigned char* SrcData,
    uint32 SrcWidth,
    uint32 SrcHeight,
    unsigned char* DstData,
    uint32 DstWidth,
    uint32 DstHeight)
{
    for (uint32 y = 0; y < DstHeight; ++y)
    {
        for (uint32 x = 0; x < DstWidth; ++x)
        {
            // Sample 2x2 block from source
            uint32 SrcX = x * 2;
            uint32 SrcY = y * 2;
            
            // Accumulate RGBA values from 2x2 block
            uint32 r = 0, g = 0, b = 0, a = 0;
            
            for (uint32 dy = 0; dy < 2; ++dy)
            {
                for (uint32 dx = 0; dx < 2; ++dx)
                {
                    uint32 sx = FMath::Min(SrcX + dx, SrcWidth - 1);
                    uint32 sy = FMath::Min(SrcY + dy, SrcHeight - 1);
                    uint32 SrcIndex = (sy * SrcWidth + sx) * 4;
                    
                    r += SrcData[SrcIndex + 0];
                    g += SrcData[SrcIndex + 1];
                    b += SrcData[SrcIndex + 2];
                    a += SrcData[SrcIndex + 3];
                }
            }
            
            // Average the 2x2 block
            uint32 DstIndex = (y * DstWidth + x) * 4;
            DstData[DstIndex + 0] = static_cast<unsigned char>(r / 4);
            DstData[DstIndex + 1] = static_cast<unsigned char>(g / 4);
            DstData[DstIndex + 2] = static_cast<unsigned char>(b / 4);
            DstData[DstIndex + 3] = static_cast<unsigned char>(a / 4);
        }
    }
}

// ============================================================================
// FAsyncTextureLoader Implementation
// ============================================================================

FAsyncTextureLoader::FAsyncTextureLoader(uint32 NumWorkerThreads)
    : m_bShutdown(false)
    , m_activeLoadCount(0)
{
    MR_LOG(LogAsyncTextureLoader, Log, "Initializing async texture loader with %u worker threads", NumWorkerThreads);
    
    // Create worker threads
    m_workerThreads.Reserve(NumWorkerThreads);
    for (uint32 i = 0; i < NumWorkerThreads; ++i)
    {
        m_workerThreads.Add(std::thread(&FAsyncTextureLoader::_workerThreadFunc, this, i));
    }
    
    MR_LOG(LogAsyncTextureLoader, Log, "Async texture loader initialized successfully");
}

FAsyncTextureLoader::~FAsyncTextureLoader()
{
    shutdown();
}

void FAsyncTextureLoader::loadTextureAsync(
    const FString& FilePath,
    bool bGenerateMips,
    FLoadCompleteCallback Callback)
{
    if (m_bShutdown)
    {
        MR_LOG(LogAsyncTextureLoader, Warning, "Cannot load texture - loader is shut down");
        return;
    }
    
    // Add request to queue
    {
        std::lock_guard<std::mutex> Lock(m_queueMutex);
        m_loadQueue.push(FLoadRequest(FilePath, bGenerateMips, Callback));
    }
    
    // Notify a worker thread
    m_queueCondition.notify_one();
    
    MR_LOG(LogAsyncTextureLoader, Verbose, "Queued async texture load: %ls", *FilePath);
}

void FAsyncTextureLoader::processCompletedLoads()
{
    // Process all completed loads
    std::queue<FTextureLoadResult> CompletedLoads;
    
    {
        std::lock_guard<std::mutex> Lock(m_completedMutex);
        CompletedLoads.swap(m_completedQueue);
    }
    
    while (!CompletedLoads.empty())
    {
        FTextureLoadResult Result = CompletedLoads.front();
        CompletedLoads.pop();
        
        if (Result.bSuccess)
        {
            MR_LOG(LogAsyncTextureLoader, Log, "Processing completed texture load: %ls (%ux%u, %u mips)",
                   *Result.FilePath, Result.Width, Result.Height, Result.MipLevels);
        }
        else
        {
            MR_LOG(LogAsyncTextureLoader, Error, "Texture load failed: %ls - %ls",
                   *Result.FilePath, *Result.ErrorMessage);
        }
        
        // Invoke callback if present
        if (Result.Callback)
        {
            Result.Callback(Result);
        }
        
        // Clean up mip data after callback (callback should copy data if needed)
        Result.Cleanup();
    }
}

void FAsyncTextureLoader::shutdown()
{
    if (m_bShutdown.exchange(true))
    {
        return; // Already shut down
    }
    
    MR_LOG(LogAsyncTextureLoader, Log, "Shutting down async texture loader...");
    
    // Wake up all worker threads
    m_queueCondition.notify_all();
    
    // Wait for all worker threads to finish
    for (auto& Thread : m_workerThreads)
    {
        if (Thread.joinable())
        {
            Thread.join();
        }
    }
    
    m_workerThreads.Empty();
    
    // Clean up any remaining completed loads
    {
        std::lock_guard<std::mutex> Lock(m_completedMutex);
        while (!m_completedQueue.empty())
        {
            FTextureLoadResult& Result = m_completedQueue.front();
            Result.Cleanup();
            m_completedQueue.pop();
        }
    }
    
    MR_LOG(LogAsyncTextureLoader, Log, "Async texture loader shut down");
}

bool FAsyncTextureLoader::isBusy() const
{
    std::lock_guard<std::mutex> Lock(m_queueMutex);
    return !m_loadQueue.empty() || m_activeLoadCount.load() > 0;
}

uint32 FAsyncTextureLoader::getPendingLoadCount() const
{
    std::lock_guard<std::mutex> Lock(m_queueMutex);
    return static_cast<uint32>(m_loadQueue.size());
}

void FAsyncTextureLoader::_workerThreadFunc(uint32 ThreadIndex)
{
    MR_LOG(LogAsyncTextureLoader, Log, "Worker thread %u started", ThreadIndex);
    
    while (!m_bShutdown)
    {
        FLoadRequest Request;
        
        // Wait for a load request
        {
            std::unique_lock<std::mutex> Lock(m_queueMutex);
            
            m_queueCondition.wait(Lock, [this]() {
                return m_bShutdown || !m_loadQueue.empty();
            });
            
            if (m_bShutdown && m_loadQueue.empty())
            {
                break;
            }
            
            if (!m_loadQueue.empty())
            {
                Request = m_loadQueue.front();
                m_loadQueue.pop();
            }
            else
            {
                continue;
            }
        }
        
        // Process the request
        m_activeLoadCount++;
        _processLoadRequest(Request);
        m_activeLoadCount--;
    }
    
    MR_LOG(LogAsyncTextureLoader, Log, "Worker thread %u stopped", ThreadIndex);
}

void FAsyncTextureLoader::_processLoadRequest(const FLoadRequest& Request)
{
    FTextureLoadResult Result;
    Result.FilePath = Request.FilePath;
    Result.Callback = Request.Callback; // Store callback in result
    
    // Load texture from file
    int32 Width = 0;
    int32 Height = 0;
    int32 Channels = 0;
    
    unsigned char* ImageData = _loadTextureFromFile(Request.FilePath, Width, Height, Channels);
    
    if (!ImageData)
    {
        Result.bSuccess = false;
        Result.ErrorMessage = FString("Failed to load texture from file");
        
        // Add to completed queue
        std::lock_guard<std::mutex> Lock(m_completedMutex);
        m_completedQueue.push(Result);
        return;
    }
    
    Result.Width = static_cast<uint32>(Width);
    Result.Height = static_cast<uint32>(Height);
    Result.Channels = static_cast<uint32>(Channels);
    
    // Generate mipmaps if requested
    if (Request.bGenerateMips)
    {
        Result.MipLevels = CalculateMipLevels(Result.Width, Result.Height);
        
        if (!_generateMipmaps(ImageData, Result.Width, Result.Height, Result.MipLevels,
                             Result.MipData, Result.MipSizes))
        {
            Result.bSuccess = false;
            Result.ErrorMessage = FString("Failed to generate mipmaps");
            stbi_image_free(ImageData);
            
            // Add to completed queue
            std::lock_guard<std::mutex> Lock(m_completedMutex);
            m_completedQueue.push(Result);
            return;
        }
    }
    else
    {
        // Single mip level
        Result.MipLevels = 1;
        uint32 ImageSize = Result.Width * Result.Height * 4;
        
        unsigned char* MipData = static_cast<unsigned char*>(FMemory::Malloc(ImageSize));
        FMemory::Memcpy(MipData, ImageData, ImageSize);
        
        Result.MipData.Add(MipData);
        Result.MipSizes.Add(ImageSize);
    }
    
    // Free original image data
    stbi_image_free(ImageData);
    
    Result.bSuccess = true;
    
    // Add to completed queue
    {
        std::lock_guard<std::mutex> Lock(m_completedMutex);
        m_completedQueue.push(Result);
    }
    
    MR_LOG(LogAsyncTextureLoader, Verbose, "Texture load completed: %ls (%ux%u, %u mips)",
           *Request.FilePath, Result.Width, Result.Height, Result.MipLevels);
}

unsigned char* FAsyncTextureLoader::_loadTextureFromFile(
    const FString& FilePath,
    int32& OutWidth,
    int32& OutHeight,
    int32& OutChannels)
{
    // Convert FString to C string
    // Note: FString should have a method to get C string, using a simple conversion here
    const char* PathCStr = TCHAR_TO_UTF8(*FilePath);
    
    // Flip texture vertically for OpenGL/Vulkan compatibility
    stbi_set_flip_vertically_on_load(true);
    
    // Load image data (force RGBA format)
    unsigned char* ImageData = stbi_load(PathCStr, &OutWidth, &OutHeight, &OutChannels, STBI_rgb_alpha);
    
    if (!ImageData)
    {
        MR_LOG(LogAsyncTextureLoader, Error, "stbi_load failed for: %ls", *FilePath);
        return nullptr;
    }
    
    MR_LOG(LogAsyncTextureLoader, VeryVerbose, "Loaded texture: %ls (%dx%d, %d channels)",
           *FilePath, OutWidth, OutHeight, OutChannels);
    
    return ImageData;
}

bool FAsyncTextureLoader::_generateMipmaps(
    const unsigned char* SourceData,
    uint32 Width,
    uint32 Height,
    uint32 MipLevels,
    TArray<unsigned char*>& OutMipData,
    TArray<uint32>& OutMipSizes)
{
    if (!SourceData || Width == 0 || Height == 0 || MipLevels == 0)
    {
        return false;
    }
    
    OutMipData.Reserve(MipLevels);
    OutMipSizes.Reserve(MipLevels);
    
    uint32 CurrentWidth = Width;
    uint32 CurrentHeight = Height;
    
    for (uint32 MipLevel = 0; MipLevel < MipLevels; ++MipLevel)
    {
        uint32 MipSize = CurrentWidth * CurrentHeight * 4; // RGBA format
        unsigned char* MipData = nullptr;
        
        if (MipLevel == 0)
        {
            // First level: copy original data
            MipData = static_cast<unsigned char*>(FMemory::Malloc(MipSize));
            FMemory::Memcpy(MipData, SourceData, MipSize);
        }
        else
        {
            // Generate downsampled mip level using box filter
            MipData = static_cast<unsigned char*>(FMemory::Malloc(MipSize));
            
            uint32 PrevWidth = CurrentWidth * 2;
            uint32 PrevHeight = CurrentHeight * 2;
            const unsigned char* PrevData = OutMipData[MipLevel - 1];
            
            DownsampleBoxFilter(PrevData, PrevWidth, PrevHeight, 
                               MipData, CurrentWidth, CurrentHeight);
        }
        
        OutMipData.Add(MipData);
        OutMipSizes.Add(MipSize);
        
        // Calculate next mip dimensions
        CurrentWidth = FMath::Max(1u, CurrentWidth >> 1);
        CurrentHeight = FMath::Max(1u, CurrentHeight >> 1);
    }
    
    return true;
}

} // namespace MonsterEngine
