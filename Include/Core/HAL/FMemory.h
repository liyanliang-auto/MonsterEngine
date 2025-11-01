// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Memory Operations (UE5-style)

#pragma once

#include "Core/CoreTypes.h"
#include <cstring>
#include <new>

namespace MonsterRender {

/**
 * FMemory - Static class for memory operations (similar to UE5's FMemory)
 * 
 * Reference: UE5 Engine/Source/Runtime/Core/Public/HAL/FMemory.h
 * 
 * Provides platform-optimized memory operations and allocation helpers
 */
class FMemory {
public:
    // Memory operations
    static FORCEINLINE void* Memcpy(void* Dest, const void* Src, SIZE_T Count) {
        return ::memcpy(Dest, Src, Count);
    }

    static FORCEINLINE void* Memmove(void* Dest, const void* Src, SIZE_T Count) {
        return ::memmove(Dest, Src, Count);
    }

    static FORCEINLINE int32 Memcmp(const void* Buf1, const void* Buf2, SIZE_T Count) {
        return ::memcmp(Buf1, Buf2, Count);
    }

    static FORCEINLINE void* Memset(void* Dest, uint8 Value, SIZE_T Count) {
        return ::memset(Dest, Value, Count);
    }

    static FORCEINLINE void Memzero(void* Dest, SIZE_T Count) {
        ::memset(Dest, 0, Count);
    }

    static FORCEINLINE void* Memswap(void* Ptr1, void* Ptr2, SIZE_T Size) {
        void* Temp = ::alloca(Size);
        Memcpy(Temp, Ptr1, Size);
        Memcpy(Ptr1, Ptr2, Size);
        Memcpy(Ptr2, Temp, Size);
        return Ptr1;
    }

    // Memory allocation (delegates to global allocator)
    static void* Malloc(SIZE_T Count, uint32 Alignment = DEFAULT_ALIGNMENT);
    static void* Realloc(void* Original, SIZE_T Count, uint32 Alignment = DEFAULT_ALIGNMENT);
    static void Free(void* Original);
    static SIZE_T GetAllocSize(void* Original);

    // System memory allocation (bypasses custom allocator)
    static void* SystemMalloc(SIZE_T Size);
    static void SystemFree(void* Ptr);

    // Memory validation
    static bool IsAligned(const void* Ptr, SIZE_T Alignment) {
        return (reinterpret_cast<SIZE_T>(Ptr) & (Alignment - 1)) == 0;
    }

    static void* Align(void* Ptr, SIZE_T Alignment) {
        SIZE_T Mask = Alignment - 1;
        return reinterpret_cast<void*>((reinterpret_cast<SIZE_T>(Ptr) + Mask) & ~Mask);
    }

    // Template allocation helpers (UE5-style)
    template<typename T>
    static FORCEINLINE T* MallocArray(SIZE_T Count, uint32 Alignment = alignof(T)) {
        return static_cast<T*>(Malloc(Count * sizeof(T), Alignment));
    }

    template<typename T>
    static FORCEINLINE T* ReallocArray(T* Original, SIZE_T Count, uint32 Alignment = alignof(T)) {
        return static_cast<T*>(Realloc(Original, Count * sizeof(T), Alignment));
    }

    // Construct/Destruct helpers
    template<typename T, typename... Args>
    static FORCEINLINE T* New(Args&&... InArgs) {
        void* Mem = Malloc(sizeof(T), alignof(T));
        return new(Mem) T(std::forward<Args>(InArgs)...);
    }

    template<typename T>
    static FORCEINLINE void Delete(T* Obj) {
        if (Obj) {
            Obj->~T();
            Free(Obj);
        }
    }

    template<typename T>
    static FORCEINLINE T* NewArray(SIZE_T Count) {
        T* Array = MallocArray<T>(Count);
        for (SIZE_T i = 0; i < Count; ++i) {
            new(&Array[i]) T();
        }
        return Array;
    }

    template<typename T>
    static FORCEINLINE void DeleteArray(T* Array, SIZE_T Count) {
        if (Array) {
            for (SIZE_T i = 0; i < Count; ++i) {
                Array[i].~T();
            }
            Free(Array);
        }
    }

    // Stats
    static uint64 GetTotalAllocatedMemory();
    static void GetMemoryStats(struct FMemoryStats& OutStats);

    static constexpr uint32 DEFAULT_ALIGNMENT = alignof(std::max_align_t);

private:
    FMemory() = delete;  // Static class, no instances
};

} // namespace MonsterRender

