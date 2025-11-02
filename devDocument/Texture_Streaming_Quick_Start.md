# Texture Streaming System - Quick Start Guide

## Quick Setup (5 Minutes)

### 1. Initialize Systems

```cpp
#include "Renderer/FTextureStreamingManager.h"
#include "Core/IO/FAsyncFileIO.h"

int main() {
    // Initialize file IO system
    FAsyncFileIO::Get().Initialize(2);
    
    // Initialize streaming manager (256MB pool)
    FTextureStreamingManager::Get().Initialize(256 * 1024 * 1024);
    
    // Your game loop...
    
    // Cleanup
    FTextureStreamingManager::Get().Shutdown();
    FAsyncFileIO::Get().Shutdown();
}
```

### 2. Register Textures

```cpp
// Create/Load texture
FTexture* myTexture = LoadTexture("character.dds");

// Register for streaming
FTextureStreamingManager::Get().RegisterTexture(myTexture);
```

### 3. Update Per Frame

```cpp
void GameLoop() {
    float deltaTime = GetDeltaTime();
    
    // Update streaming system
    FTextureStreamingManager::Get().UpdateResourceStreaming(deltaTime);
    
    // Render...
}
```

---

## Running Tests

### Command Line

```bash
# All tests
MonsterEngine.exe --test-all

# Texture tests only
MonsterEngine.exe --test-texture

# Memory tests only
MonsterEngine.exe --test-memory
```

### Expected Result

```
======================================
  Texture Streaming System Tests
======================================

PASSED: FTexturePool::Basic Allocation (0.45ms)
PASSED: FAsyncFileIO::Basic Read (12.34ms)
PASSED: FTextureStreamingManager::Initialization (2.34ms)
PASSED: Scenario::Open World Terrain Streaming (98.76ms)

Total Tests: 15
Passed: 15
Failed: 0
```

---

## Common Usage Patterns

### Pattern 1: Open World Streaming

```cpp
class OpenWorldGame {
    void Update(float deltaTime) {
        // Get camera position
        Vector3 cameraPos = camera.GetPosition();
        
        // Update texture priorities based on distance
        for (auto& tile : terrainTiles) {
            float distance = Distance(cameraPos, tile.center);
            tile.texture->priority = 1.0f / (1.0f + distance);
        }
        
        // Streaming manager handles the rest
        textureStreaming.UpdateResourceStreaming(deltaTime);
    }
};
```

### Pattern 2: Character LOD

```cpp
class Character {
    void UpdateTextureLOD(float distanceToCamera) {
        if (distanceToCamera < 10.0f) {
            skinTexture->SetRequestedMips(11);  // Full quality
        }
        else if (distanceToCamera < 50.0f) {
            skinTexture->SetRequestedMips(7);   // Medium quality
        }
        else {
            skinTexture->SetRequestedMips(3);   // Low quality
        }
    }
};
```

### Pattern 3: Level Loading

```cpp
void LoadLevel(const String& levelName) {
    // Unload old level
    for (auto* tex : oldTextures) {
        streamingManager.UnregisterTexture(tex);
    }
    
    // Load new level
    auto newTextures = GetLevelTextures(levelName);
    for (auto* tex : newTextures) {
        streamingManager.RegisterTexture(tex);
    }
    
    // Wait for critical textures
    WaitForTexturesLoaded(newTextures);
}
```

---

## Debugging

### Check Stats

```cpp
FTextureStreamingManager::FStreamingStats stats;
streamingManager.GetStreamingStats(stats);

MR_LOG_INFO("Memory: " + std::to_string(stats.AllocatedMemory / 1024 / 1024) + " MB");
MR_LOG_INFO("Streaming: " + std::to_string(stats.NumStreamingTextures));
```

### Enable Debug Logging

```cpp
Logger::getInstance().setMinLevel(ELogLevel::Debug);
```

---

## Performance Tips

### 1. Set Appropriate Budget

```cpp
// Desktop: 512MB
streamingManager.SetPoolSize(512 * 1024 * 1024);

// Mobile: 128MB
streamingManager.SetPoolSize(128 * 1024 * 1024);
```

### 2. Use Multiple IO Workers

```cpp
// For SSD
FAsyncFileIO::Get().Initialize(4);

// For HDD
FAsyncFileIO::Get().Initialize(2);
```

### 3. Prioritize Important Textures

```cpp
characterTexture->SetHighPriority();
uiTexture->SetHighPriority();
```

---

## Troubleshooting

### Problem: Textures not loading

**Check 1**: Is texture registered?
```cpp
bool registered = streamingManager.IsTextureRegistered(myTexture);
```

**Check 2**: Is there enough memory?
```cpp
SIZE_T free = streamingManager.GetPoolSize() - streamingManager.GetAllocatedMemory();
MR_LOG_INFO("Free memory: " + std::to_string(free / 1024 / 1024) + " MB");
```

### Problem: Slow loading

**Solution**: Increase worker threads
```cpp
FAsyncFileIO::Get().Shutdown();
FAsyncFileIO::Get().Initialize(4);  // More workers
```

---

## Test Scenarios

The test suite includes 4 real-world scenarios:

1. **Open World**: 3x3 terrain grid with camera movement
2. **Character LOD**: 5 characters with distance-based streaming
3. **Level Transition**: Unload old level, load new level
4. **Cutscene Preload**: High-res texture preloading

Run them with:
```bash
MonsterEngine.exe --test-texture
```

---

## Next Steps

1. **Read Full Guide**: `Texture_Streaming_System_Guide.md`
2. **Run Tests**: `MonsterEngine.exe --test-texture`
3. **Integrate into your game**: Follow patterns above

---

**Quick Reference**:
- Initialize: `FTextureStreamingManager::Get().Initialize(256 * 1024 * 1024)`
- Register: `streamingManager.RegisterTexture(texture)`
- Update: `streamingManager.UpdateResourceStreaming(deltaTime)`
- Stats: `streamingManager.GetStreamingStats(stats)`

