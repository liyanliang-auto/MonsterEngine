# FMemory System Quick Start Guide

## Quick Setup

### 1. Initialize Memory System

```cpp
#include "Core/HAL/FMemoryManager.h"

int main() {
    // Initialize memory system
    FMemoryManager::Get().Initialize();
    
    // Your code here
    
    return 0;
}
```

### 2. Basic Usage

```cpp
#include "Core/HAL/FMemory.h"

// Allocate memory
void* buffer = FMemory::Malloc(1024);

// Use memory
FMemory::Memset(buffer, 0, 1024);

// Free memory
FMemory::Free(buffer);
```

### 3. Type-Safe Allocation

```cpp
struct Player {
    String name;
    int health;
    
    Player(const String& n, int h) : name(n), health(h) {}
};

// Create object
Player* player = FMemory::New<Player>("Hero", 100);

// Use object
player->health -= 10;

// Delete object
FMemory::Delete(player);
```

---

## Running Tests

### Command Line

```bash
MonsterEngine.exe --test-memory
```

### Expected Output

```
======================================
  FMemory System Tests
======================================

PASSED: FMemory::Basic Operations (0.12ms)
PASSED: FMemoryManager::Initialization (1.23ms)
PASSED: FMemory::Basic Allocation (0.34ms)
PASSED: FMallocBinned2::Small Allocations (0.56ms)
PASSED: Multi-threaded Allocations (45.67ms)

All FMemory tests passed!
```

---

## Common Operations

### Memory Copy

```cpp
char src[100] = "Hello";
char dst[100];
FMemory::Memcpy(dst, src, 100);
```

### Memory Fill

```cpp
uint8 buffer[1024];
FMemory::Memset(buffer, 0xFF, 1024);
```

### Memory Zero

```cpp
char data[256];
FMemory::Memzero(data, 256);
```

### Check Alignment

```cpp
void* ptr = FMemory::Malloc(256, 16);
if (FMemory::IsAligned(ptr, 16)) {
    // Properly aligned
}
FMemory::Free(ptr);
```

---

## Array Operations

```cpp
// Allocate array
int32* numbers = FMemory::NewArray<int32>(100);

// Use array
for (int i = 0; i < 100; ++i) {
    numbers[i] = i * 2;
}

// Free array
FMemory::DeleteArray(numbers, 100);
```

---

## Debug Tips

### Enable Debug Logging

```cpp
Logger::getInstance().setMinLevel(ELogLevel::Debug);
```

### Check Memory Stats

```cpp
FMalloc* allocator = FMemoryManager::Get().GetAllocator();
FMalloc::FMemoryStats stats;
allocator->GetMemoryStats(stats);

MR_LOG_INFO("Allocated: " + std::to_string(stats.TotalAllocated / 1024) + " KB");
```

### Validate Heap

```cpp
if (!allocator->ValidateHeap()) {
    MR_LOG_ERROR("Heap corruption!");
}
```

---

## Best Practices

1. **Always check allocation results**
```cpp
void* ptr = FMemory::Malloc(size);
if (!ptr) {
    // Handle error
}
```

2. **Use type-safe helpers**
```cpp
// Good
MyClass* obj = FMemory::New<MyClass>();

// Avoid
MyClass* obj = (MyClass*)FMemory::Malloc(sizeof(MyClass));
```

3. **Prefer stack over heap**
```cpp
// Good (stack)
char buffer[256];

// Avoid if possible (heap)
char* buffer = (char*)FMemory::Malloc(256);
```

4. **Free memory in reverse order**
```cpp
void* a = FMemory::Malloc(100);
void* b = FMemory::Malloc(200);
FMemory::Free(b);  // Free last allocated first
FMemory::Free(a);
```

---

## Troubleshooting

### Problem: Allocation fails

**Solution**: Check available memory
```cpp
FMemoryManager::FGlobalMemoryStats stats;
FMemoryManager::Get().GetGlobalMemoryStats(stats);
MR_LOG_INFO("Available: " + std::to_string(stats.AvailablePhysicalMemory / 1024 / 1024) + " MB");
```

### Problem: Memory leak suspected

**Solution**: Enable CRT debug
```cpp
#ifdef _DEBUG
    #define _CRTDBG_MAP_ALLOC
    #include <crtdbg.h>
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
```

### Problem: Crash on free

**Solution**: Check for double-free
```cpp
void* ptr = FMemory::Malloc(100);
FMemory::Free(ptr);
ptr = nullptr;  // Set to null after free
// FMemory::Free(ptr);  // This is safe now
```

---

## Performance Tips

### 1. Reduce allocations

```cpp
// Bad
std::vector<int> vec;
for (int i = 0; i < 1000; ++i) {
    vec.push_back(i);  // Many reallocations
}

// Good
std::vector<int> vec;
vec.reserve(1000);  // Pre-allocate
for (int i = 0; i < 1000; ++i) {
    vec.push_back(i);
}
```

### 2. Use alignment wisely

```cpp
// For GPU buffers
struct alignas(256) GPUData {
    float values[64];
};
```

### 3. Batch operations

```cpp
// Allocate once
void* bulk = FMemory::Malloc(1024 * objects);
// Subdivide internally
```

---

## Next Steps

1. Read full documentation: `FMemory_System_Guide.md`
2. Run test suite: `MonsterEngine.exe --test-memory`
3. Review UE5 source code for reference

---

**Quick Reference**:
- Initialize: `FMemoryManager::Get().Initialize()`
- Allocate: `FMemory::Malloc(size)`
- Free: `FMemory::Free(ptr)`
- Test: `--test-memory` flag

