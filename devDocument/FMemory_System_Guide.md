# MonsterEngine FMemory System Development Guide

## Overview

This document describes the FMemory management system in MonsterEngine, based on Unreal Engine 5's memory architecture.

**Version**: 1.0  
**Date**: 2025-11-01  
**Reference**: UE5 Memory Management System

---

## System Architecture

```
Application Layer
        |
        v
    FMemory (Static Interface)
        |
        v
  FMemoryManager (Singleton)
        |
        v
    FMalloc (Abstract Base)
        |
        v
  FMallocBinned2 (Implementation)
```

---

## Core Components

### 1. FMemory - Static Memory Operations

**File**: `Include/Core/HAL/FMemory.h`

**Key Features**:
- Global static interface for all memory operations
- Thread-safe and high-performance
- Based on UE5's FMemory class

**Main APIs**:

```cpp
class FMemory {
public:
    // Basic operations
    static void* Memcpy(void* Dest, const void* Src, SIZE_T Count);
    static void* Memset(void* Dest, uint8 Value, SIZE_T Count);
    static void Memzero(void* Dest, SIZE_T Count);
    
    // Memory allocation
    static void* Malloc(SIZE_T Count, uint32 Alignment = DEFAULT_ALIGNMENT);
    static void* Realloc(void* Original, SIZE_T Count, uint32 Alignment = DEFAULT_ALIGNMENT);
    static void Free(void* Original);
    
    // Alignment utilities
    static bool IsAligned(const void* Ptr, SIZE_T Alignment);
    
    // Template helpers
    template<typename T, typename... Args>
    static T* New(Args&&... InArgs);
    
    template<typename T>
    static void Delete(T* Obj);
};
```

**Usage Example**:

```cpp
// Basic allocation
void* buffer = FMemory::Malloc(1024);
FMemory::Memset(buffer, 0, 1024);
FMemory::Free(buffer);

// Type-safe allocation
struct MyData { int x, y, z; };
MyData* data = FMemory::New<MyData>(10, 20, 30);
FMemory::Delete(data);
```

---

### 2. FMemoryManager - Global Memory Manager

**File**: `Include/Core/HAL/FMemoryManager.h`

**Key Features**:
- Singleton pattern
- System initialization and shutdown
- Memory statistics collection
- Huge Pages support

**Main APIs**:

```cpp
class FMemoryManager {
public:
    static FMemoryManager& Get();
    
    bool Initialize();
    void Shutdown();
    
    FMalloc* GetAllocator() const;
    void SetAllocator(TUniquePtr<FMalloc> NewAllocator);
    
    struct FGlobalMemoryStats {
        uint64 TotalPhysicalMemory;
        uint64 AvailablePhysicalMemory;
        uint64 PageSize;
        // ...
    };
    
    void GetGlobalMemoryStats(FGlobalMemoryStats& OutStats);
};
```

---

### 3. FMalloc - Allocator Base Class

**File**: `Include/Core/HAL/FMalloc.h`

**Key Features**:
- Abstract interface for memory allocators
- Statistics and debugging support

**Main APIs**:

```cpp
class FMalloc {
public:
    virtual void* Malloc(SIZE_T Size, uint32 Alignment = DEFAULT_ALIGNMENT) = 0;
    virtual void* Realloc(void* Original, SIZE_T Size, uint32 Alignment = DEFAULT_ALIGNMENT) = 0;
    virtual void Free(void* Original) = 0;
    
    virtual SIZE_T GetAllocationSize(void* Original);
    virtual bool ValidateHeap();
    virtual uint64 GetTotalAllocatedMemory();
    
    struct FMemoryStats {
        uint64 TotalAllocated;
        uint64 TotalReserved;
        uint64 AllocationCount;
        uint64 FreeCount;
    };
    
    virtual void GetMemoryStats(FMemoryStats& OutStats);
};
```

---

### 4. FMallocBinned2 - High-Performance Binned Allocator

**File**: `Include/Core/HAL/FMallocBinned2.h`

**Key Features**:
- Based on UE5's FMallocBinned2
- Optimized for small objects (16-1024 bytes)
- Multi-threaded with TLS caching
- Large objects use direct system allocation

**Architecture**:

```
Size Classes (Bins):
  Bin 0:  16 bytes
  Bin 1:  32 bytes
  Bin 2:  64 bytes
  Bin 3: 128 bytes
  Bin 4: 256 bytes
  Bin 5: 512 bytes
  Bin 6: 1024 bytes

TLS Cache:
  - Thread-local storage
  - Lock-free fast path
  - 16 objects per bin

Page Management:
  - 64KB page size
  - Free list management
  - Lazy deallocation
```

**Performance Features**:

| Feature | Description | Benefit |
|---------|-------------|---------|
| Binned Allocation | Size-based categorization | Reduced fragmentation |
| TLS Cache | Thread-local storage | Lock-free, fast allocation |
| Page Pooling | 64KB page reuse | Fewer system calls |
| Alignment | Cache-line aligned | Avoid false sharing |

---

## Testing System

### Test Files

**Location**: `Source/Tests/FMemorySystemTest.cpp`

### Test Suite Structure

```
FMemory System Tests
|
+-- FMemory Basic Tests
|   +-- Basic Operations (Memcpy, Memset, Memzero)
|
+-- FMemoryManager Tests
|   +-- Initialization
|   +-- Basic Allocation
|
+-- FMallocBinned2 Tests
|   +-- Small Allocations (16-1024B)
|
+-- Stress Tests
    +-- Multi-threaded Allocations (4 threads)
```

### Running Tests

**Method 1: Command Line**
```bash
MonsterEngine.exe --test-memory
# or
MonsterEngine.exe -tm
```

**Method 2: Visual Studio**
1. Right-click project -> Properties
2. Debugging -> Command Arguments
3. Enter: `--test-memory`
4. Press F5 to debug

### Test Output Example

```
======================================
  FMemory System Tests
======================================

--- FMemory Basic Tests ---
PASSED: FMemory::Basic Operations (0.12ms)

--- FMemoryManager Tests ---
PASSED: FMemoryManager::Initialization (1.23ms)
  Total Physical Memory: 16384 MB
PASSED: FMemory::Basic Allocation (0.34ms)

--- FMallocBinned2 Tests ---
PASSED: FMallocBinned2::Small Allocations (0.56ms)

--- Stress Tests ---
PASSED: Multi-threaded Allocations (45.67ms)

======================================
  FMemory System Test Summary
======================================
Total Tests: 5
Passed: 5
Failed: 0

All FMemory tests passed!
======================================
```

---

## Debugging Guide

### Enable Detailed Logging

```cpp
// In main.cpp
Logger::getInstance().setMinLevel(ELogLevel::Debug);
```

### Memory Leak Detection

**Windows (Visual Studio)**:
```cpp
#ifdef _DEBUG
    #define _CRTDBG_MAP_ALLOC
    #include <stdlib.h>
    #include <crtdbg.h>
#endif

int main() {
    #ifdef _DEBUG
        _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    #endif
    // ... normal code
}
```

### Heap Validation

```cpp
FMalloc* allocator = FMemoryManager::Get().GetAllocator();
if (!allocator->ValidateHeap()) {
    MR_LOG_ERROR("Heap corruption detected!");
}
```

### Performance Analysis

```cpp
FMalloc::FMemoryStats stats;
allocator->GetMemoryStats(stats);

MR_LOG_INFO("=== Memory Statistics ===");
MR_LOG_INFO("Total Allocated: " + std::to_string(stats.TotalAllocated / 1024) + " KB");
MR_LOG_INFO("Allocation Count: " + std::to_string(stats.AllocationCount));
```

---

## Performance Optimization Tips

### 1. Reduce Small Object Allocations

**Bad**:
```cpp
for (int i = 0; i < 1000; ++i) {
    MyObject* obj = new MyObject();
    delete obj;
}
```

**Good**:
```cpp
std::vector<MyObject> objects;
objects.reserve(1000);
for (int i = 0; i < 1000; ++i) {
    objects.emplace_back();
}
```

### 2. Object Pooling

```cpp
template<typename T>
class TObjectPool {
    std::vector<T*> pool;
public:
    T* Acquire() {
        if (pool.empty()) return FMemory::New<T>();
        T* obj = pool.back();
        pool.pop_back();
        return obj;
    }
    void Release(T* obj) {
        pool.push_back(obj);
    }
};
```

### 3. Memory Alignment

```cpp
struct alignas(256) GPUBuffer {
    float data[64];
};

GPUBuffer* buffer = FMemory::New<GPUBuffer>();
MR_ASSERT(FMemory::IsAligned(buffer, 256));
```

---

## FAQ

### Q1: What to do if allocation fails?

**A**: Check:
1. System memory availability
2. Requested size is reasonable
3. Alignment requirements (must be power of 2)

```cpp
void* ptr = FMemory::Malloc(size);
if (!ptr) {
    MR_LOG_ERROR("Out of memory! Requested: " + std::to_string(size));
    FMemoryManager::Get().GetAllocator()->Trim();
    ptr = FMemory::Malloc(size);  // Retry
}
```

### Q2: Is it thread-safe?

**A**: Yes, FMallocBinned2 is fully thread-safe:
- TLS cache provides lock-free fast path
- Each bin has independent mutex
- Large objects use thread-safe system allocator

### Q3: How to detect memory leaks?

**A**: Use these methods:
1. Visual Studio's CRT Debug Heap
2. Check statistics before/after program
3. Use Valgrind (Linux) or Dr. Memory (Windows)

---

## References

### Unreal Engine 5

- `Engine/Source/Runtime/Core/Public/HAL/MallocBinned2.h`
- `Engine/Source/Runtime/Core/Public/HAL/FMemory.h`

### Learning Resources

1. **Papers**:
   - "Scalable Memory Allocation using jemalloc"
   - "TCMalloc: Thread-Caching Malloc"

2. **Books**:
   - "Game Engine Architecture" by Jason Gregory

---

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2025-11-01 | Initial release |

---

**Last Updated**: 2025-11-01  
**Maintained By**: MonsterEngine Development Team

