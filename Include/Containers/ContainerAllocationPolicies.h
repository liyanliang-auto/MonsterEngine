// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file ContainerAllocationPolicies.h
 * @brief Container allocation policies following UE5 patterns
 * 
 * Provides allocator policies for containers with configurable behavior:
 * - TSizedDefaultAllocator: Default allocator with configurable index size
 * - TInlineAllocator: Inline storage with heap fallback
 * - TFixedAllocator: Fixed-size inline storage only
 * 
 * Slack calculation functions for intelligent memory management.
 */

#include "Core/CoreTypes.h"
#include "Core/Templates/TypeTraits.h"
#include "ContainerFwd.h"
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <new>

namespace MonsterEngine
{

// Forward declarations
class FMemory;

// ============================================================================
// Default Alignment
// ============================================================================

#ifndef DEFAULT_ALIGNMENT
    #define DEFAULT_ALIGNMENT 16
#endif

// ============================================================================
// Slack Calculation Functions
// ============================================================================

/**
 * Calculate slack when shrinking an array
 * Returns the new capacity after shrinking
 */
template<typename SizeType>
FORCEINLINE SizeType DefaultCalculateSlackShrink(
    SizeType NumElements, 
    SizeType NumAllocatedElements, 
    size_t BytesPerElement, 
    bool bAllowQuantize, 
    uint32 Alignment = DEFAULT_ALIGNMENT)
{
    SizeType Result;
    
    // If the container has too much slack, shrink it to exactly fit the number of elements
    const SizeType CurrentSlackElements = NumAllocatedElements - NumElements;
    const size_t CurrentSlackBytes = static_cast<size_t>(CurrentSlackElements) * BytesPerElement;
    const bool bTooManySlackBytes = CurrentSlackBytes >= 16384;
    const bool bTooManySlackElements = 3 * NumElements < 2 * NumAllocatedElements;
    
    if ((bTooManySlackBytes || bTooManySlackElements) && (CurrentSlackElements > 64 || !NumElements))
    {
        Result = NumElements;
        if (Result > 0 && bAllowQuantize)
        {
            // Quantize to allocator-friendly size
            size_t QuantizedSize = (static_cast<size_t>(Result) * BytesPerElement + Alignment - 1) & ~(Alignment - 1);
            Result = static_cast<SizeType>(QuantizedSize / BytesPerElement);
        }
    }
    else
    {
        Result = NumAllocatedElements;
    }
    
    return Result;
}

/**
 * Calculate slack when growing an array
 * Returns the new capacity after growing
 */
template<typename SizeType>
FORCEINLINE SizeType DefaultCalculateSlackGrow(
    SizeType NumElements, 
    SizeType NumAllocatedElements, 
    size_t BytesPerElement, 
    bool bAllowQuantize, 
    uint32 Alignment = DEFAULT_ALIGNMENT)
{
    // Growth constants
    constexpr size_t FirstGrow = 4;
    constexpr size_t ConstantGrow = 16;
    
    SizeType Result;
    
    size_t Grow = FirstGrow; // Amount for first allocation
    
    if (NumAllocatedElements > 0)
    {
        // Allocate slack proportional to size: size + 3/8 * size + constant
        Grow = static_cast<size_t>(NumElements) + 3 * static_cast<size_t>(NumElements) / 8 + ConstantGrow;
    }
    else if (static_cast<size_t>(NumElements) > Grow)
    {
        Grow = static_cast<size_t>(NumElements);
    }
    
    if (bAllowQuantize)
    {
        // Quantize to allocator-friendly size
        size_t QuantizedSize = (Grow * BytesPerElement + Alignment - 1) & ~(Alignment - 1);
        Result = static_cast<SizeType>(QuantizedSize / BytesPerElement);
    }
    else
    {
        Result = static_cast<SizeType>(Grow);
    }
    
    // Ensure we don't overflow
    if (NumElements > Result)
    {
        Result = (std::numeric_limits<SizeType>::max)();
    }
    
    return Result;
}

/**
 * Calculate slack for reserve operations
 */
template<typename SizeType>
FORCEINLINE SizeType DefaultCalculateSlackReserve(
    SizeType NumElements, 
    size_t BytesPerElement, 
    bool bAllowQuantize, 
    uint32 Alignment = DEFAULT_ALIGNMENT)
{
    SizeType Result = NumElements;
    
    if (bAllowQuantize)
    {
        size_t QuantizedSize = (static_cast<size_t>(Result) * BytesPerElement + Alignment - 1) & ~(Alignment - 1);
        Result = static_cast<SizeType>(QuantizedSize / BytesPerElement);
        
        // Ensure we don't overflow
        if (NumElements > Result)
        {
            Result = (std::numeric_limits<SizeType>::max)();
        }
    }
    
    return Result;
}

// ============================================================================
// Allocator Traits
// ============================================================================

/**
 * Base allocator traits
 */
template<typename AllocatorType>
struct TAllocatorTraitsBase
{
    enum { IsZeroConstruct = false };
    enum { SupportsFreezeMemoryImage = false };
    enum { SupportsElementAlignment = false };
};

// ============================================================================
// Heap Allocator
// ============================================================================

/**
 * FHeapAllocator - Basic heap allocator using FMemory
 */
class FHeapAllocator
{
public:
    using SizeType = int32;
    
    enum { NeedsElementType = false };
    enum { RequireRangeCheck = true };
    
    class ForAnyElementType
    {
    public:
        /** Default constructor. */
        ForAnyElementType()
            : Data(nullptr)
        {
        }
        
        // Disable copy (UE5 pattern)
        ForAnyElementType(const ForAnyElementType&) = delete;
        ForAnyElementType& operator=(const ForAnyElementType&) = delete;
        
        /**
         * Moves the state of another allocator into this one.
         * Assumes that the allocator is currently empty, i.e. memory may be allocated 
         * but any existing elements have already been destructed (if necessary).
         * @param Other - The allocator to move the state from. This allocator should be left in a valid empty state.
         */
        FORCEINLINE void MoveToEmpty(ForAnyElementType& Other)
        {
            // UE5 uses checkSlow here, we use assert in debug builds
            assert(this != &Other);
            
            if (Data)
            {
                std::free(Data);
            }
            
            Data = Other.Data;
            Other.Data = nullptr;
        }
        
        /** Destructor. */
        FORCEINLINE ~ForAnyElementType()
        {
            if (Data)
            {
                std::free(Data);
            }
        }
        
        FORCEINLINE void* GetAllocation() const
        {
            return Data;
        }
        
        void ResizeAllocation(SizeType PreviousNumElements, SizeType NumElements, size_t NumBytesPerElement, uint32 AlignmentOfElement = DEFAULT_ALIGNMENT)
        {
            // UE5 pattern: Avoid calling realloc(nullptr, 0) as ANSI C mandates returning a valid pointer
            // which is not what we want.
            if (Data || NumElements)
            {
                size_t NewSize = static_cast<size_t>(NumElements) * NumBytesPerElement;
                Data = std::realloc(Data, NewSize);
            }
        }
        
        FORCEINLINE SizeType CalculateSlackReserve(SizeType NumElements, size_t NumBytesPerElement, uint32 AlignmentOfElement = DEFAULT_ALIGNMENT) const
        {
            return DefaultCalculateSlackReserve(NumElements, NumBytesPerElement, true, AlignmentOfElement);
        }
        
        FORCEINLINE SizeType CalculateSlackShrink(SizeType NumElements, SizeType NumAllocatedElements, size_t NumBytesPerElement, uint32 AlignmentOfElement = DEFAULT_ALIGNMENT) const
        {
            return DefaultCalculateSlackShrink(NumElements, NumAllocatedElements, NumBytesPerElement, true, AlignmentOfElement);
        }
        
        FORCEINLINE SizeType CalculateSlackGrow(SizeType NumElements, SizeType NumAllocatedElements, size_t NumBytesPerElement, uint32 AlignmentOfElement = DEFAULT_ALIGNMENT) const
        {
            return DefaultCalculateSlackGrow(NumElements, NumAllocatedElements, NumBytesPerElement, true, AlignmentOfElement);
        }
        
        FORCEINLINE SizeType GetInitialCapacity() const
        {
            return 0;
        }
        
    private:
        void* Data = nullptr;
    };
    
    template<typename ElementType>
    class ForElementType : public ForAnyElementType
    {
    public:
        ForElementType() = default;
        
        FORCEINLINE ElementType* GetAllocation() const
        {
            return static_cast<ElementType*>(ForAnyElementType::GetAllocation());
        }
    };
};

// ============================================================================
// Sized Default Allocator
// ============================================================================

/**
 * TSizedDefaultAllocator - Default allocator with configurable index size
 * IndexSize can be 32 or 64 bits
 */
template<int IndexSize>
class TSizedDefaultAllocator : public FHeapAllocator
{
public:
    using SizeType = TConditional_T<IndexSize == 64, int64, int32>;
    
    static_assert(IndexSize == 32 || IndexSize == 64, "IndexSize must be 32 or 64");
};

/**
 * FDefaultAllocator - Standard 32-bit index allocator
 */
using FDefaultAllocator = TSizedDefaultAllocator<32>;

/**
 * FDefaultAllocator64 - 64-bit index allocator for large arrays
 */
using FDefaultAllocator64 = TSizedDefaultAllocator<64>;

// ============================================================================
// Inline Allocator
// ============================================================================

/**
 * TInlineAllocator - Inline storage with heap fallback
 * Stores up to NumInlineElements inline, falls back to heap for more
 */
template<uint32 NumInlineElements, typename SecondaryAllocator = FDefaultAllocator>
class TInlineAllocator
{
public:
    using SizeType = typename SecondaryAllocator::SizeType;
    
    enum { NeedsElementType = true };
    enum { RequireRangeCheck = true };
    
    template<typename ElementType>
    class ForElementType
    {
    public:
        ForElementType() = default;
        
        ForElementType(const ForElementType&) = delete;
        ForElementType& operator=(const ForElementType&) = delete;
        
        ForElementType(ForElementType&& Other) noexcept
        {
            MoveFrom(std::move(Other));
        }
        
        ForElementType& operator=(ForElementType&& Other) noexcept
        {
            if (this != &Other)
            {
                MoveFrom(std::move(Other));
            }
            return *this;
        }
        
        ~ForElementType()
        {
            if (SecondaryData.GetAllocation())
            {
                // Using secondary allocator
            }
        }
        
        FORCEINLINE ElementType* GetAllocation() const
        {
            ElementType* SecondaryAlloc = SecondaryData.GetAllocation();
            if (SecondaryAlloc)
            {
                return SecondaryAlloc;
            }
            return const_cast<ElementType*>(reinterpret_cast<const ElementType*>(InlineData));
        }
        
        void ResizeAllocation(SizeType PreviousNumElements, SizeType NumElements, size_t NumBytesPerElement, uint32 AlignmentOfElement = DEFAULT_ALIGNMENT)
        {
            if (NumElements <= static_cast<SizeType>(NumInlineElements))
            {
                // Fits in inline storage
                if (SecondaryData.GetAllocation())
                {
                    // Move from secondary to inline
                    if (PreviousNumElements > 0)
                    {
                        size_t CopySize = static_cast<size_t>(std::min(PreviousNumElements, NumElements)) * NumBytesPerElement;
                        std::memcpy(InlineData, SecondaryData.GetAllocation(), CopySize);
                    }
                    SecondaryData.ResizeAllocation(0, 0, NumBytesPerElement, AlignmentOfElement);
                }
            }
            else
            {
                // Needs secondary storage
                if (!SecondaryData.GetAllocation() && PreviousNumElements > 0)
                {
                    // Moving from inline to secondary
                    SecondaryData.ResizeAllocation(0, NumElements, NumBytesPerElement, AlignmentOfElement);
                    size_t CopySize = static_cast<size_t>(std::min(PreviousNumElements, NumElements)) * NumBytesPerElement;
                    std::memcpy(SecondaryData.GetAllocation(), InlineData, CopySize);
                }
                else
                {
                    SecondaryData.ResizeAllocation(PreviousNumElements, NumElements, NumBytesPerElement, AlignmentOfElement);
                }
            }
        }
        
        FORCEINLINE SizeType CalculateSlackReserve(SizeType NumElements, size_t NumBytesPerElement, uint32 AlignmentOfElement = DEFAULT_ALIGNMENT) const
        {
            if (NumElements <= static_cast<SizeType>(NumInlineElements))
            {
                return static_cast<SizeType>(NumInlineElements);
            }
            return SecondaryData.CalculateSlackReserve(NumElements, NumBytesPerElement, AlignmentOfElement);
        }
        
        FORCEINLINE SizeType CalculateSlackShrink(SizeType NumElements, SizeType NumAllocatedElements, size_t NumBytesPerElement, uint32 AlignmentOfElement = DEFAULT_ALIGNMENT) const
        {
            if (NumElements <= static_cast<SizeType>(NumInlineElements))
            {
                return static_cast<SizeType>(NumInlineElements);
            }
            return SecondaryData.CalculateSlackShrink(NumElements, NumAllocatedElements, NumBytesPerElement, AlignmentOfElement);
        }
        
        FORCEINLINE SizeType CalculateSlackGrow(SizeType NumElements, SizeType NumAllocatedElements, size_t NumBytesPerElement, uint32 AlignmentOfElement = DEFAULT_ALIGNMENT) const
        {
            if (NumElements <= static_cast<SizeType>(NumInlineElements))
            {
                return static_cast<SizeType>(NumInlineElements);
            }
            return SecondaryData.CalculateSlackGrow(NumElements, NumAllocatedElements, NumBytesPerElement, AlignmentOfElement);
        }
        
        FORCEINLINE SizeType GetInitialCapacity() const
        {
            return static_cast<SizeType>(NumInlineElements);
        }
        
    private:
        void MoveFrom(ForElementType&& Other)
        {
            if (Other.SecondaryData.GetAllocation())
            {
                SecondaryData = std::move(Other.SecondaryData);
            }
            else
            {
                std::memcpy(InlineData, Other.InlineData, sizeof(InlineData));
            }
        }
        
        alignas(ElementType) uint8 InlineData[sizeof(ElementType) * NumInlineElements];
        typename SecondaryAllocator::template ForElementType<ElementType> SecondaryData;
    };
};

// ============================================================================
// Fixed Allocator
// ============================================================================

/**
 * TFixedAllocator - Fixed-size inline storage only (no heap fallback)
 */
template<uint32 NumInlineElements>
class TFixedAllocator
{
public:
    using SizeType = int32;
    
    enum { NeedsElementType = true };
    enum { RequireRangeCheck = true };
    
    template<typename ElementType>
    class ForElementType
    {
    public:
        ForElementType() = default;
        
        FORCEINLINE ElementType* GetAllocation() const
        {
            return const_cast<ElementType*>(reinterpret_cast<const ElementType*>(InlineData));
        }
        
        void ResizeAllocation(SizeType PreviousNumElements, SizeType NumElements, size_t NumBytesPerElement, uint32 AlignmentOfElement = DEFAULT_ALIGNMENT)
        {
            // Fixed allocator cannot resize beyond inline capacity
            // This is a no-op, caller must ensure NumElements <= NumInlineElements
        }
        
        FORCEINLINE SizeType CalculateSlackReserve(SizeType NumElements, size_t NumBytesPerElement, uint32 AlignmentOfElement = DEFAULT_ALIGNMENT) const
        {
            return static_cast<SizeType>(NumInlineElements);
        }
        
        FORCEINLINE SizeType CalculateSlackShrink(SizeType NumElements, SizeType NumAllocatedElements, size_t NumBytesPerElement, uint32 AlignmentOfElement = DEFAULT_ALIGNMENT) const
        {
            return static_cast<SizeType>(NumInlineElements);
        }
        
        FORCEINLINE SizeType CalculateSlackGrow(SizeType NumElements, SizeType NumAllocatedElements, size_t NumBytesPerElement, uint32 AlignmentOfElement = DEFAULT_ALIGNMENT) const
        {
            return static_cast<SizeType>(NumInlineElements);
        }
        
        FORCEINLINE SizeType GetInitialCapacity() const
        {
            return static_cast<SizeType>(NumInlineElements);
        }
        
    private:
        alignas(ElementType) uint8 InlineData[sizeof(ElementType) * NumInlineElements];
    };
};

// ============================================================================
// Allocator Traits Specializations
// ============================================================================

template<>
struct TAllocatorTraitsBase<FHeapAllocator>
{
    enum { IsZeroConstruct = false };
    enum { SupportsFreezeMemoryImage = false };
    enum { SupportsElementAlignment = true };
};

template<int IndexSize>
struct TAllocatorTraitsBase<TSizedDefaultAllocator<IndexSize>>
{
    enum { IsZeroConstruct = false };
    enum { SupportsFreezeMemoryImage = false };
    enum { SupportsElementAlignment = true };
};

} // namespace MonsterEngine
