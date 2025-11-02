# MonsterEngine Texture Streaming System Development Guide

## Overview

This document describes the Texture Streaming System in MonsterEngine, inspired by Unreal Engine 5's virtual texture streaming architecture.

**Version**: 1.0  
**Date**: 2025-11-01  
**Reference**: UE5 Texture Streaming System

---

## Table of Contents

1. [System Architecture](#system-architecture)
2. [Core Components](#core-components)
3. [Testing System](#testing-system)
4. [Real-World Scenarios](#real-world-scenarios)
5. [Performance Optimization](#performance-optimization)
6. [Debugging Guide](#debugging-guide)

---

## System Architecture

```
┌───────────────────────────────────────────────┐
│     Application / Renderer Layer              │
│  (Register textures, camera updates)          │
└─────────────────┬─────────────────────────────┘
                  │
┌─────────────────▼─────────────────────────────┐
│    FTextureStreamingManager (Singleton)       │
│  - Tracks registered textures                 │
│  - Updates streaming priorities               │
│  - Manages memory budget                      │
└─────────────┬───────────────┬─────────────────┘
              │               │
    ┌─────────▼──────┐  ┌────▼──────────┐
    │  FTexturePool  │  │  FAsyncFileIO │
    │  (Memory Pool) │  │  (Disk I/O)   │
    └────────────────┘  └───────────────┘
```

### Data Flow

```
1. Texture Registration
   └→ Add to streaming manager

2. Per-Frame Update
   ├→ Calculate priorities (distance, screen size)
   ├→ Determine mip levels needed
   └→ Queue streaming requests

3. Stream In Process
   ├→ Check memory budget
   ├→ Evict low-priority if needed
   ├→ Allocate from pool
   └→ Async load from disk

4. Stream Out Process
   ├→ Free unused mips
   └→ Return memory to pool
```

---

## Core Components

### 1. FTextureStreamingManager

**File**: `Include/Renderer/FTextureStreamingManager.h`

**Purpose**: Central manager for all texture streaming operations

**Key Features**:
- Priority-based streaming (distance, screen coverage)
- Memory budget management
- Automatic mip level selection
- Statistics tracking

**Main APIs**:

```cpp
class FTextureStreamingManager {
public:
    static FTextureStreamingManager& Get();
    
    // Initialization
    bool Initialize(SIZE_T TexturePoolSize);
    void Shutdown();
    
    // Texture lifecycle
    void RegisterTexture(FTexture* Texture);
    void UnregisterTexture(FTexture* Texture);
    
    // Per-frame update (call every frame)
    void UpdateResourceStreaming(float DeltaTime);
    
    // Memory management
    SIZE_T GetAllocatedMemory() const;
    SIZE_T GetPoolSize() const;
    void SetPoolSize(SIZE_T NewSize);
    
    // Statistics
    struct FStreamingStats {
        uint32 NumStreamingTextures;
        uint32 NumResidentTextures;
        SIZE_T AllocatedMemory;
        SIZE_T PoolSize;
        SIZE_T PendingStreamIn;
        SIZE_T PendingStreamOut;
        float StreamingBandwidth;  // MB/s
    };
    
    void GetStreamingStats(FStreamingStats& OutStats);
};
```

**Usage Example**:

```cpp
// Initialize system
FTextureStreamingManager& manager = FTextureStreamingManager::Get();
manager.Initialize(256 * 1024 * 1024);  // 256MB pool

// Register texture
FTexture* myTexture = LoadTexture("character_diffuse.dds");
manager.RegisterTexture(myTexture);

// Update every frame
void GameLoop() {
    float deltaTime = GetDeltaTime();
    manager.UpdateResourceStreaming(deltaTime);
}

// Cleanup
manager.UnregisterTexture(myTexture);
manager.Shutdown();
```

---

### 2. FTexturePool

**File**: `Include/Renderer/FTextureStreamingManager.h` (nested class)

**Purpose**: Pre-allocated memory pool for texture data

**Key Features**:
- First-fit allocation strategy
- Free list management
- 256-byte alignment for GPU
- Fragmentation handling

**Main APIs**:

```cpp
class FTexturePool {
public:
    FTexturePool(SIZE_T PoolSize);
    
    // Allocation
    void* Allocate(SIZE_T Size, SIZE_T Alignment = 256);
    void Free(void* Ptr);
    SIZE_T GetAllocationSize(void* Ptr);
    
    // Statistics
    SIZE_T GetTotalSize() const;
    SIZE_T GetUsedSize() const;
    SIZE_T GetFreeSize() const;
    
    // Maintenance
    void Compact();  // Defragmentation
};
```

**Usage Example**:

```cpp
FTexturePool pool(64 * 1024 * 1024);  // 64MB

// Allocate for 2048x2048 RGBA texture
void* textureMemory = pool.Allocate(2048 * 2048 * 4, 256);

// Use memory...

// Free when done
pool.Free(textureMemory);
```

---

### 3. FAsyncFileIO

**File**: `Include/Core/IO/FAsyncFileIO.h`

**Purpose**: Asynchronous file I/O for loading texture data

**Key Features**:
- Worker thread pool
- Non-blocking operations
- Request queuing
- Bandwidth tracking

**Main APIs**:

```cpp
class FAsyncFileIO {
public:
    static FAsyncFileIO& Get();
    
    bool Initialize(uint32 NumWorkerThreads = 2);
    void Shutdown();
    
    // Read request structure
    struct FReadRequest {
        String FilePath;
        SIZE_T Offset;
        SIZE_T Size;
        void* DestBuffer;
        std::function<void(bool Success, SIZE_T BytesRead)> OnComplete;
    };
    
    // Submit async read
    uint64 ReadAsync(const FReadRequest& Request);
    
    // Synchronization
    bool WaitForRequest(uint64 RequestID);
    void WaitForAll();
    bool IsRequestComplete(uint64 RequestID);
    
    // Statistics
    struct FIOStats {
        uint64 TotalRequests;
        uint64 CompletedRequests;
        uint64 PendingRequests;
        uint64 FailedRequests;
        uint64 TotalBytesRead;
        float AverageBandwidthMBps;
    };
    
    void GetStats(FIOStats& OutStats);
};
```

**Usage Example**:

```cpp
FAsyncFileIO& fileIO = FAsyncFileIO::Get();
fileIO.Initialize(2);  // 2 worker threads

// Prepare buffer
std::vector<uint8> buffer(4096);

// Submit async read
FAsyncFileIO::FReadRequest request;
request.FilePath = "texture_mip0.dat";
request.Offset = 0;
request.Size = 4096;
request.DestBuffer = buffer.data();
request.OnComplete = [](bool success, SIZE_T bytesRead) {
    if (success) {
        MR_LOG_INFO("Read " + std::to_string(bytesRead) + " bytes");
    }
};

uint64 requestID = fileIO.ReadAsync(request);

// Continue doing other work...

// Wait if needed
fileIO.WaitForRequest(requestID);
```

---

## Testing System

### Test File

**Location**: `Source/Tests/TextureStreamingSystemTest.cpp`

### Test Categories

#### 1. **FTexturePool Tests**

```
Test: Basic Allocation
- Allocate 1MB, 4MB, 16MB blocks
- Verify memory accounting
- Test deallocation

Test: Fragmentation Handling
- Create fragmented memory
- Test large allocation failure
- Compact and retry

Test: Memory Alignment
- Verify 256-byte alignment
- Test multiple aligned allocations
```

#### 2. **FAsyncFileIO Tests**

```
Test: Initialization
- Create worker threads
- Verify system ready

Test: Basic Read
- Create test file
- Async read
- Verify data integrity

Test: Concurrent Reads
- Submit 4 simultaneous reads
- Verify all complete
- Check no data corruption
```

#### 3. **FTextureStreamingManager Tests**

```
Test: Initialization
- Initialize with 128MB pool
- Verify pool created

Test: Texture Registration
- Register multiple textures
- Verify tracking
- Unregister and cleanup

Test: Priority-based Streaming
- Register textures at different distances
- Update streaming
- Verify high-priority loads first

Test: Memory Budget Management
- Set small budget (16MB)
- Register textures exceeding budget
- Verify low-priority textures evicted
```

#### 4. **Real-World Scenario Tests**

These tests simulate actual game scenarios:

```cpp
// Scenario 1: Open World Terrain Streaming
void TestScenarioOpenWorldStreaming() {
    // Simulate 3x3 terrain grid
    // Camera moves through terrain
    // Verify distant tiles stream out
    // Verify nearby tiles stream in
}

// Scenario 2: Character LOD Streaming
void TestScenarioCharacterLODStreaming() {
    // Multiple characters (5)
    // Each with 3 textures (diffuse, normal, specular)
    // Distance-based LOD changes
    // Verify correct mip levels loaded
}

// Scenario 3: Level Transition
void TestScenarioLevelTransition() {
    // Load old level textures
    // Unload old level
    // Load new level textures
    // Verify memory properly freed and reused
}

// Scenario 4: Cutscene Preloading
void TestScenarioCutscenePreloading() {
    // Preload high-res cutscene textures
    // Measure preload time
    // Verify all mips loaded
}
```

### Running Tests

**Command Line Options**:

```bash
# Run all tests
MonsterEngine.exe --test-all
MonsterEngine.exe -ta

# Run texture tests only
MonsterEngine.exe --test-texture
MonsterEngine.exe -tt

# Run memory tests only
MonsterEngine.exe --test-memory
MonsterEngine.exe -tm

# Default (no args): run all tests
MonsterEngine.exe
```

### Expected Output

```
==========================================
  MonsterEngine Test Suite
==========================================

======================================
  Texture Streaming System Tests
======================================

--- FTexturePool Tests ---
PASSED: FTexturePool::Basic Allocation (0.45ms)
  Used: 21 MB
  Free: 43 MB
PASSED: FTexturePool::Fragmentation Handling (1.23ms)
  Allocated 10 blocks
  Before compact: 10 MB used
  After compact: 10 MB used
PASSED: FTexturePool::Memory Alignment (0.18ms)
  ptr1 aligned: 1
  ptr2 aligned: 1

--- FAsyncFileIO Tests ---
PASSED: FAsyncFileIO::Initialization (0.56ms)
  Worker threads initialized
  Total requests: 0
PASSED: FAsyncFileIO::Basic Read (12.34ms)
  Async read completed: 4096 bytes
PASSED: FAsyncFileIO::Concurrent Reads (15.67ms)
  File 0 completed
  File 1 completed
  File 2 completed
  File 3 completed

--- FTextureStreamingManager Tests ---
PASSED: FTextureStreamingManager::Initialization (2.34ms)
  Pool size: 128 MB
  Initial stats:
    Streaming textures: 0
    Resident textures: 0
PASSED: FTextureStreamingManager::Texture Registration (0.89ms)
  Registered 3 textures
  Streaming textures: 3
PASSED: FTextureStreamingManager::Priority-based Streaming (56.78ms)
  Frame 0:
    Allocated: 0 KB
    Pending stream in: 0 KB
  Frame 1:
    Allocated: 512 KB
    Pending stream in: 1024 KB
  ...
PASSED: FTextureStreamingManager::Memory Budget Management (45.23ms)
  Memory budget: 16 MB
  Allocated: 15 MB
  Budget respected: 1

--- Real-world Scenario Tests ---
PASSED: Scenario::Open World Terrain Streaming (98.76ms)
  Simulating open world with terrain tiles...
  Simulating camera movement...
  Frame 0: 9 streaming
  Frame 3: 7 streaming
  Frame 6: 5 streaming
  Frame 9: 4 streaming

PASSED: Scenario::Character LOD Texture Streaming (67.89ms)
  Simulating multiple character LODs...
  Registered 15 character textures
  Final allocated memory: 42 MB

PASSED: Scenario::Level Transition Streaming (78.90ms)
  Simulating level transition...
  Old level loaded: 20480 KB
  New level loaded: 57344 KB

PASSED: Scenario::Cutscene Texture Preloading (123.45ms)
  Simulating cutscene preload...
  Preload time: 105 ms
  Loaded memory: 64 MB

======================================
  Texture Streaming Test Summary
======================================
Total Tests: 15
Passed: 15
Failed: 0

All texture streaming tests passed!
======================================
```

---

## Real-World Scenarios

### 1. Open World Game

**Challenge**: Stream terrain textures as player moves

**Solution**:
```cpp
class OpenWorldManager {
    void UpdateCamera(const Camera& camera) {
        // Calculate visible terrain tiles
        auto visibleTiles = CalculateVisibleTiles(camera);
        
        // Update texture priorities based on distance
        for (auto& tile : allTiles) {
            float distance = Distance(camera.position, tile.center);
            tile.priority = 1.0f / (1.0f + distance);
        }
        
        // Let streaming manager handle it
        textureStreaming.UpdateResourceStreaming(deltaTime);
    }
};
```

### 2. Character LOD System

**Challenge**: Stream character textures based on distance

**Solution**:
```cpp
class Character {
    void UpdateLOD(float distanceToCamera) {
        if (distanceToCamera < 10.0f) {
            // Near: Load all mips
            diffuseTexture->SetRequestedMips(11);
            normalTexture->SetRequestedMips(11);
        }
        else if (distanceToCamera < 50.0f) {
            // Mid: Load medium mips
            diffuseTexture->SetRequestedMips(7);
            normalTexture->SetRequestedMips(7);
        }
        else {
            // Far: Load minimal mips
            diffuseTexture->SetRequestedMips(3);
            normalTexture->SetRequestedMips(3);
        }
    }
};
```

### 3. Level Loading

**Challenge**: Efficiently transition between levels

**Solution**:
```cpp
class LevelManager {
    void TransitionToLevel(const String& newLevel) {
        // Phase 1: Mark old level textures for unload
        for (auto* texture : currentLevelTextures) {
            streamingManager.UnregisterTexture(texture);
        }
        currentLevelTextures.clear();
        
        // Phase 2: Load critical new level assets
        auto criticalTextures = GetCriticalTextures(newLevel);
        for (auto* texture : criticalTextures) {
            streamingManager.RegisterTexture(texture);
            texture->SetHighPriority();
        }
        
        // Phase 3: Wait for critical assets
        while (!AllTexturesLoaded(criticalTextures)) {
            streamingManager.UpdateResourceStreaming(0.016f);
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        
        // Phase 4: Stream in remaining assets
        auto remainingTextures = GetRemainingTextures(newLevel);
        for (auto* texture : remainingTextures) {
            streamingManager.RegisterTexture(texture);
        }
    }
};
```

### 4. Cutscene System

**Challenge**: Preload high-quality textures for cutscenes

**Solution**:
```cpp
class CutsceneManager {
    void PreloadCutscene(const String& cutsceneName) {
        auto textures = GetCutsceneTextures(cutsceneName);
        
        // Request all mips immediately
        for (auto* texture : textures) {
            texture->SetRequestedMips(texture->TotalMipLevels);
            texture->SetHighPriority();
            streamingManager.RegisterTexture(texture);
        }
        
        // Force immediate loading
        while (!AllFullyLoaded(textures)) {
            streamingManager.UpdateResourceStreaming(0.001f);
        }
        
        MR_LOG_INFO("Cutscene preloaded: " + cutsceneName);
    }
};
```

---

## Performance Optimization

### Memory Budget

```cpp
// Set appropriate budget based on platform
#if PLATFORM_WINDOWS
    SIZE_T budget = 512 * 1024 * 1024;  // 512MB
#elif PLATFORM_MOBILE
    SIZE_T budget = 128 * 1024 * 1024;  // 128MB
#endif

streamingManager.SetPoolSize(budget);
```

### Streaming Priority Calculation

```cpp
float CalculateTexturePriority(const FTexture* texture, const Camera& camera) {
    // Distance factor
    float distance = Distance(texture->worldPosition, camera.position);
    float distanceFactor = 1.0f / (1.0f + distance);
    
    // Screen size factor
    float screenSize = CalculateScreenCoverage(texture, camera);
    
    // LOD bias (user preference)
    float lodBias = GetTextureLODBias();
    
    // Combined priority
    return distanceFactor * screenSize * lodBias;
}
```

### Async Loading Optimization

```cpp
// Use multiple worker threads for faster loading
FAsyncFileIO::Get().Initialize(4);  // 4 workers for SSDs

// Batch multiple small reads into one large read
void LoadMipChain(FTexture* texture) {
    SIZE_T totalSize = CalculateTotalMipSize(texture);
    void* buffer = pool.Allocate(totalSize);
    
    // One large read instead of many small ones
    fileIO.ReadAsync({
        texture->filePath,
        0,
        totalSize,
        buffer,
        OnMipsLoaded
    });
}
```

---

## Debugging Guide

### Enable Detailed Logging

```cpp
// In main.cpp or init code
Logger::getInstance().setMinLevel(ELogLevel::Debug);
```

### Check Streaming Stats

```cpp
void PrintStreamingStats() {
    FTextureStreamingManager::FStreamingStats stats;
    streamingManager.GetStreamingStats(stats);
    
    MR_LOG_INFO("=== Texture Streaming Stats ===");
    MR_LOG_INFO("Streaming: " + std::to_string(stats.NumStreamingTextures));
    MR_LOG_INFO("Resident: " + std::to_string(stats.NumResidentTextures));
    MR_LOG_INFO("Memory: " + std::to_string(stats.AllocatedMemory / 1024 / 1024) + " MB");
    MR_LOG_INFO("Budget: " + std::to_string(stats.PoolSize / 1024 / 1024) + " MB");
    MR_LOG_INFO("Pending In: " + std::to_string(stats.PendingStreamIn / 1024) + " KB");
    MR_LOG_INFO("Pending Out: " + std::to_string(stats.PendingStreamOut / 1024) + " KB");
    MR_LOG_INFO("Bandwidth: " + std::to_string(stats.StreamingBandwidth) + " MB/s");
}
```

### Visualize Texture State

```cpp
void DebugDrawTextures() {
    for (auto& streamingTexture : streamingTextures) {
        auto* texture = streamingTexture.Texture;
        
        // Color code by state
        Color debugColor;
        if (streamingTexture.ResidentMips == texture->TotalMipLevels) {
            debugColor = Color::Green;  // Fully loaded
        }
        else if (streamingTexture.ResidentMips > 0) {
            debugColor = Color::Yellow;  // Partially loaded
        }
        else {
            debugColor = Color::Red;  // Not loaded
        }
        
        DrawDebugBox(texture->worldPosition, texture->bounds, debugColor);
    }
}
```

### Common Issues

**Issue 1: Textures not streaming in**
```cpp
// Check if texture is registered
bool isRegistered = streamingManager.IsTextureRegistered(myTexture);

// Check priority
float priority = CalculatePriority(myTexture);
MR_LOG_DEBUG("Texture priority: " + std::to_string(priority));

// Check memory budget
if (streamingManager.GetAllocatedMemory() >= streamingManager.GetPoolSize()) {
    MR_LOG_WARNING("Memory budget exceeded!");
}
```

**Issue 2: Poor streaming performance**
```cpp
// Check IO stats
FAsyncFileIO::FIOStats ioStats;
fileIO.GetStats(ioStats);

MR_LOG_INFO("Pending requests: " + std::to_string(ioStats.PendingRequests));
MR_LOG_INFO("Failed requests: " + std::to_string(ioStats.FailedRequests));
MR_LOG_INFO("Bandwidth: " + std::to_string(ioStats.AverageBandwidthMBps) + " MB/s");

// Increase worker threads if IO-bound
if (ioStats.PendingRequests > 10) {
    fileIO.Shutdown();
    fileIO.Initialize(4);  // Increase to 4 workers
}
```

---

## References

### Unreal Engine 5

- `Engine/Source/Runtime/Renderer/Private/Streaming/TextureStreamingManager.h`
- `Engine/Source/Runtime/RenderCore/Public/TextureResource.h`
- `Engine/Source/Runtime/Core/Public/Async/AsyncFileHandle.h`

### Additional Resources

1. **GDC Talks**:
   - "Texture Streaming in Unreal Engine 4"
   - "Virtual Texturing in UE5"

2. **Papers**:
   - "Adaptive Texture Streaming for Real-Time Rendering"
   - "Virtual Texturing in Software and Hardware"

3. **Books**:
   - "Real-Time Rendering" by Tomas Akenine-Möller
   - "GPU Gems 2: Chapter 2 - Texture Streaming"

---

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2025-11-01 | Initial release with comprehensive test suite |

---

**Last Updated**: 2025-11-01  
**Maintained By**: MonsterEngine Development Team

