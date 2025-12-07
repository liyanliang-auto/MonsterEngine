// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file ContainerFwd.h
 * @brief Forward declarations for container types
 * 
 * This file provides forward declarations for all container types
 * to minimize header dependencies and improve compile times.
 */

#include "Core/CoreTypes.h"

namespace MonsterEngine
{

// ============================================================================
// Allocator Forward Declarations
// ============================================================================

class FHeapAllocator;

template<int IndexSize>
class TSizedDefaultAllocator;

using FDefaultAllocator = TSizedDefaultAllocator<32>;
using FDefaultAllocator64 = TSizedDefaultAllocator<64>;

template<uint32 NumInlineElements, typename SecondaryAllocator>
class TInlineAllocator;

template<uint32 NumInlineElements>
class TFixedAllocator;

// ============================================================================
// Container Forward Declarations
// ============================================================================

// TArray - Dynamic array
template<typename InElementType, typename InAllocatorType = FDefaultAllocator>
class TArray;

// TArray64 - Dynamic array with 64-bit indices
template<typename InElementType>
using TArray64 = TArray<InElementType, FDefaultAllocator64>;

// TSparseArray - Sparse array with O(1) removal
template<typename InElementType, typename InAllocatorType>
class TSparseArray;

// TSet - Hash set
template<typename InElementType, typename KeyFuncs = void, typename Allocator = FDefaultAllocator>
class TSet;

// TMap - Hash map
template<typename KeyType, typename ValueType, typename Allocator = FDefaultAllocator, typename KeyFuncs = void>
class TMap;

// TMultiMap - Hash multimap (allows duplicate keys)
template<typename KeyType, typename ValueType, typename Allocator = FDefaultAllocator, typename KeyFuncs = void>
class TMultiMap;

// ============================================================================
// String Forward Declarations
// ============================================================================

class FString;
class FName;
class FText;

// ============================================================================
// Pair and Tuple Forward Declarations
// ============================================================================

template<typename KeyType, typename ValueType>
struct TPair;

template<typename... Types>
class TTuple;

// ============================================================================
// Iterator Forward Declarations
// ============================================================================

template<typename ContainerType, typename ElementType, typename SizeType>
class TIndexedContainerIterator;

// ============================================================================
// Script Container Forward Declarations (for reflection)
// ============================================================================

template<typename AllocatorType>
class TScriptArray;

template<typename AllocatorType>
class TScriptSet;

template<typename AllocatorType>
class TScriptMap;

// ============================================================================
// Bit Array Forward Declarations
// ============================================================================

template<typename Allocator>
class TBitArray;

// ============================================================================
// Sparse Array Allocator
// ============================================================================

class FDefaultSparseArrayAllocator;
class FDefaultBitArrayAllocator;

// ============================================================================
// Common Type Aliases
// ============================================================================

// Index type for containers - invalid index constant
constexpr int32 INDEX_NONE = -1;

// Set element ID
class FSetElementId;

// ============================================================================
// Helper Macros
// ============================================================================

/**
 * INDEX_NONE value for invalid indices
 */
#ifndef INDEX_NONE_VALUE
    #define INDEX_NONE_VALUE (-1)
#endif

} // namespace MonsterEngine
