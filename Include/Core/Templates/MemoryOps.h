// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file MemoryOps.h
 * @brief Memory operation utilities for containers following UE5 patterns
 * 
 * Provides optimized memory operations for constructing, destructing,
 * copying, and moving elements in containers.
 */

#include "TypeTraits.h"
#include <cstring>
#include <new>
#include <utility>

namespace MonsterEngine
{

// ============================================================================
// Default Construction
// ============================================================================

/**
 * Constructs a range of elements using default constructor
 * Optimized for trivially constructible types (no-op)
 */
template<typename ElementType>
FORCEINLINE void DefaultConstructItems(ElementType* Dest, int32 Count)
{
    if constexpr (TIsZeroConstructType<ElementType>::Value)
    {
        // Zero-constructible types can be memset to zero
        std::memset(Dest, 0, sizeof(ElementType) * Count);
    }
    else if constexpr (std::is_trivially_default_constructible_v<ElementType>)
    {
        // Trivially default constructible - no action needed
        // Memory is already allocated, default values are undefined
    }
    else
    {
        // Non-trivial types need placement new
        for (int32 i = 0; i < Count; ++i)
        {
            new (Dest + i) ElementType();
        }
    }
}

// ============================================================================
// Destruction
// ============================================================================

/**
 * Destructs a range of elements
 * Optimized for trivially destructible types (no-op)
 */
template<typename ElementType>
FORCEINLINE void DestructItems(ElementType* Dest, int32 Count)
{
    if constexpr (!TIsTriviallyDestructible<ElementType>::Value)
    {
        // Non-trivial types need explicit destructor calls
        for (int32 i = 0; i < Count; ++i)
        {
            Dest[i].~ElementType();
        }
    }
    // Trivially destructible types don't need destructor calls
}

/**
 * Destructs a single element
 */
template<typename ElementType>
FORCEINLINE void DestructItem(ElementType* Dest)
{
    if constexpr (!TIsTriviallyDestructible<ElementType>::Value)
    {
        Dest->~ElementType();
    }
}

// ============================================================================
// Copy Construction
// ============================================================================

/**
 * Copy constructs a range of elements from source to destination
 * Optimized for trivially copyable types (memcpy)
 */
template<typename DestinationType, typename SourceType>
FORCEINLINE void CopyConstructItems(DestinationType* Dest, const SourceType* Source, int32 Count)
{
    if constexpr (TIsSame<DestinationType, SourceType>::Value && 
                  TIsTriviallyCopyConstructible<DestinationType>::Value)
    {
        // Trivially copy constructible - use memcpy
        std::memcpy(Dest, Source, sizeof(DestinationType) * Count);
    }
    else
    {
        // Non-trivial types need placement new with copy constructor
        for (int32 i = 0; i < Count; ++i)
        {
            new (Dest + i) DestinationType(Source[i]);
        }
    }
}

// ============================================================================
// Move Construction
// ============================================================================

/**
 * Move constructs a range of elements from source to destination
 * Optimized for trivially movable types (memcpy)
 */
template<typename DestinationType, typename SourceType>
FORCEINLINE void MoveConstructItems(DestinationType* Dest, SourceType* Source, int32 Count)
{
    if constexpr (TIsSame<DestinationType, SourceType>::Value && 
                  TIsTriviallyMoveConstructible<DestinationType>::Value)
    {
        // Trivially move constructible - use memcpy
        std::memcpy(Dest, Source, sizeof(DestinationType) * Count);
    }
    else
    {
        // Non-trivial types need placement new with move constructor
        for (int32 i = 0; i < Count; ++i)
        {
            new (Dest + i) DestinationType(std::move(Source[i]));
        }
    }
}

// ============================================================================
// Relocation (Move + Destruct Source)
// ============================================================================

/**
 * Relocates elements from source to destination
 * This is a move followed by destruction of the source
 * Optimized for trivially relocatable types (memcpy, no destruct needed)
 */
template<typename ElementType>
FORCEINLINE void RelocateConstructItems(ElementType* Dest, ElementType* Source, int32 Count)
{
    if constexpr (TIsTriviallyCopyable<ElementType>::Value)
    {
        // Trivially copyable types can be memcpy'd and don't need destruction
        std::memcpy(Dest, Source, sizeof(ElementType) * Count);
    }
    else
    {
        // Non-trivial types: move construct then destruct source
        for (int32 i = 0; i < Count; ++i)
        {
            new (Dest + i) ElementType(std::move(Source[i]));
            Source[i].~ElementType();
        }
    }
}

// ============================================================================
// Copy Assignment
// ============================================================================

/**
 * Copy assigns a range of elements from source to destination
 * Assumes destination is already constructed
 */
template<typename DestinationType, typename SourceType>
FORCEINLINE void CopyAssignItems(DestinationType* Dest, const SourceType* Source, int32 Count)
{
    if constexpr (TIsSame<DestinationType, SourceType>::Value && 
                  TIsTriviallyCopyAssignable<DestinationType>::Value)
    {
        // Trivially copy assignable - use memcpy
        std::memcpy(Dest, Source, sizeof(DestinationType) * Count);
    }
    else
    {
        // Non-trivial types need assignment operator
        for (int32 i = 0; i < Count; ++i)
        {
            Dest[i] = Source[i];
        }
    }
}

// ============================================================================
// Move Assignment
// ============================================================================

/**
 * Move assigns a range of elements from source to destination
 * Assumes destination is already constructed
 */
template<typename DestinationType, typename SourceType>
FORCEINLINE void MoveAssignItems(DestinationType* Dest, SourceType* Source, int32 Count)
{
    if constexpr (TIsSame<DestinationType, SourceType>::Value && 
                  TIsTriviallyMoveAssignable<DestinationType>::Value)
    {
        // Trivially move assignable - use memcpy
        std::memcpy(Dest, Source, sizeof(DestinationType) * Count);
    }
    else
    {
        // Non-trivial types need move assignment operator
        for (int32 i = 0; i < Count; ++i)
        {
            Dest[i] = std::move(Source[i]);
        }
    }
}

// ============================================================================
// Memory Move (Overlapping Regions)
// ============================================================================

/**
 * Moves elements within the same array (handles overlapping regions)
 * Used for insert/remove operations
 */
template<typename ElementType>
FORCEINLINE void MoveItems(ElementType* Dest, const ElementType* Source, int32 Count)
{
    if constexpr (TIsTriviallyCopyable<ElementType>::Value)
    {
        // Trivially copyable - use memmove for overlapping regions
        std::memmove(Dest, Source, sizeof(ElementType) * Count);
    }
    else
    {
        // Non-trivial types: handle overlap manually
        if (Dest < Source)
        {
            // Forward copy
            for (int32 i = 0; i < Count; ++i)
            {
                new (Dest + i) ElementType(std::move(const_cast<ElementType&>(Source[i])));
                const_cast<ElementType&>(Source[i]).~ElementType();
            }
        }
        else if (Dest > Source)
        {
            // Backward copy
            for (int32 i = Count - 1; i >= 0; --i)
            {
                new (Dest + i) ElementType(std::move(const_cast<ElementType&>(Source[i])));
                const_cast<ElementType&>(Source[i]).~ElementType();
            }
        }
    }
}

// ============================================================================
// Comparison
// ============================================================================

/**
 * Compares two ranges of elements for equality
 */
template<typename ElementType>
FORCEINLINE bool CompareItems(const ElementType* A, const ElementType* B, int32 Count)
{
    if constexpr (TIsPODType<ElementType>::Value)
    {
        // POD types can use memcmp
        return std::memcmp(A, B, sizeof(ElementType) * Count) == 0;
    }
    else
    {
        // Non-POD types need element-wise comparison
        for (int32 i = 0; i < Count; ++i)
        {
            if (!(A[i] == B[i]))
            {
                return false;
            }
        }
        return true;
    }
}

// ============================================================================
// Swap
// ============================================================================

/**
 * Swaps two elements
 */
template<typename ElementType>
FORCEINLINE void SwapItems(ElementType& A, ElementType& B)
{
    if constexpr (TIsTriviallyCopyable<ElementType>::Value)
    {
        // Trivially copyable - use memcpy through temp buffer
        alignas(ElementType) uint8 Temp[sizeof(ElementType)];
        std::memcpy(Temp, &A, sizeof(ElementType));
        std::memcpy(&A, &B, sizeof(ElementType));
        std::memcpy(&B, Temp, sizeof(ElementType));
    }
    else
    {
        // Non-trivial types use std::swap
        std::swap(A, B);
    }
}

} // namespace MonsterEngine
