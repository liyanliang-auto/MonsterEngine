// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file SparseArray.h
 * @brief Sparse array container following UE5 TSparseArray patterns
 * 
 * TSparseArray is a dynamically sized array where element indices aren't
 * necessarily contiguous. Features:
 * - O(1) element removal without invalidating other indices
 * - Efficient iteration over allocated elements
 * - Used internally by TSet for element storage
 */

#include "Core/CoreTypes.h"
#include "Core/Templates/TypeTraits.h"
#include "Core/Templates/MemoryOps.h"
#include "ContainerAllocationPolicies.h"
#include "ContainerFwd.h"
#include "Array.h"
#include "BitArray.h"

#include <new>

namespace MonsterEngine
{

// ============================================================================
// FSparseArrayAllocationInfo
// ============================================================================

/**
 * Result of a sparse array allocation
 */
struct FSparseArrayAllocationInfo
{
    int32 Index;
    void* Pointer;
};

// ============================================================================
// FDefaultSparseArrayAllocator
// ============================================================================

/**
 * Default allocator for sparse arrays
 */
class FDefaultSparseArrayAllocator
{
public:
    using ElementAllocator = FDefaultAllocator;
    using BitArrayAllocator = FDefaultBitArrayAllocator;
};

// ============================================================================
// TSparseArrayElementOrFreeListLink
// ============================================================================

/**
 * Union of element data and free list link
 * Allocated elements store their data, free elements store links to next/prev free
 */
template<typename ElementType>
union TSparseArrayElementOrFreeListLink
{
    /** If allocated, the element data */
    ElementType ElementData;
    
    /** If free, links to adjacent free elements */
    struct
    {
        int32 PrevFreeIndex;
        int32 NextFreeIndex;
    };
    
    // Default constructor - doesn't initialize anything
    TSparseArrayElementOrFreeListLink() {}
    
    // Destructor - doesn't destroy anything (managed by TSparseArray)
    ~TSparseArrayElementOrFreeListLink() {}
};

// ============================================================================
// TSparseArray
// ============================================================================

/**
 * A dynamically sized array where element indices aren't necessarily contiguous
 * 
 * Memory is allocated for all elements in the array's index range, so it doesn't
 * save memory; but it does allow O(1) element removal that doesn't invalidate
 * the indices of subsequent elements.
 * 
 * @tparam InElementType The type of elements stored
 * @tparam Allocator The allocator policy
 */
template<typename InElementType, typename Allocator = FDefaultSparseArrayAllocator>
class TSparseArray
{
public:
    using ElementType = InElementType;
    using SizeType = int32;
    
private:
    using FElementOrFreeListLink = TSparseArrayElementOrFreeListLink<ElementType>;
    using DataArrayType = TArray<FElementOrFreeListLink, typename Allocator::ElementAllocator>;
    using AllocationBitArrayType = TBitArray<typename Allocator::BitArrayAllocator>;
    
public:
    // ========================================================================
    // Constructors and Destructor
    // ========================================================================
    
    /**
     * Default constructor
     */
    TSparseArray()
        : FirstFreeIndex(INDEX_NONE_VALUE)
        , NumFreeIndices(0)
    {
    }
    
    /**
     * Copy constructor
     */
    TSparseArray(const TSparseArray& Other)
        : FirstFreeIndex(INDEX_NONE_VALUE)
        , NumFreeIndices(0)
    {
        *this = Other;
    }
    
    /**
     * Move constructor
     */
    TSparseArray(TSparseArray&& Other) noexcept
        : Data(std::move(Other.Data))
        , AllocationFlags(std::move(Other.AllocationFlags))
        , FirstFreeIndex(Other.FirstFreeIndex)
        , NumFreeIndices(Other.NumFreeIndices)
    {
        Other.FirstFreeIndex = INDEX_NONE_VALUE;
        Other.NumFreeIndices = 0;
    }
    
    /**
     * Destructor
     */
    ~TSparseArray()
    {
        Empty();
    }
    
    // ========================================================================
    // Assignment Operators
    // ========================================================================
    
    /**
     * Copy assignment
     */
    TSparseArray& operator=(const TSparseArray& Other)
    {
        if (this != &Other)
        {
            Empty();
            
            Data = Other.Data;
            AllocationFlags = Other.AllocationFlags;
            FirstFreeIndex = Other.FirstFreeIndex;
            NumFreeIndices = Other.NumFreeIndices;
            
            // Copy construct allocated elements
            const SizeType MaxIndex = GetMaxIndex();
            for (SizeType Index = 0; Index < MaxIndex; ++Index)
            {
                if (IsAllocated(Index))
                {
                    new (&Data[Index].ElementData) ElementType(Other.Data[Index].ElementData);
                }
            }
        }
        return *this;
    }
    
    /**
     * Move assignment
     */
    TSparseArray& operator=(TSparseArray&& Other) noexcept
    {
        if (this != &Other)
        {
            Empty();
            
            Data = std::move(Other.Data);
            AllocationFlags = std::move(Other.AllocationFlags);
            FirstFreeIndex = Other.FirstFreeIndex;
            NumFreeIndices = Other.NumFreeIndices;
            
            Other.FirstFreeIndex = INDEX_NONE_VALUE;
            Other.NumFreeIndices = 0;
        }
        return *this;
    }
    
    // ========================================================================
    // Size and Capacity
    // ========================================================================
    
    /**
     * Returns number of allocated elements
     */
    FORCEINLINE SizeType Num() const
    {
        return Data.Num() - NumFreeIndices;
    }
    
    /**
     * Returns the maximum valid index + 1
     */
    FORCEINLINE SizeType GetMaxIndex() const
    {
        return Data.Num();
    }
    
    /**
     * Returns true if empty
     */
    FORCEINLINE bool IsEmpty() const
    {
        return Num() == 0;
    }
    
    /**
     * Returns true if index is allocated
     */
    FORCEINLINE bool IsAllocated(SizeType Index) const
    {
        return AllocationFlags.Num() > Index && AllocationFlags[Index];
    }
    
    /**
     * Returns true if index is valid (within range)
     */
    FORCEINLINE bool IsValidIndex(SizeType Index) const
    {
        return Index >= 0 && Index < GetMaxIndex() && IsAllocated(Index);
    }
    
    // ========================================================================
    // Element Access
    // ========================================================================
    
    /**
     * Array bracket operator
     */
    FORCEINLINE ElementType& operator[](SizeType Index)
    {
        return Data[Index].ElementData;
    }
    
    /**
     * Array bracket operator (const)
     */
    FORCEINLINE const ElementType& operator[](SizeType Index) const
    {
        return Data[Index].ElementData;
    }
    
    // ========================================================================
    // Adding Elements
    // ========================================================================
    
    /**
     * Allocates an element at a specific index
     * @return Allocation info with index and pointer
     */
    FSparseArrayAllocationInfo AllocateIndex(SizeType Index)
    {
        // Ensure we have enough space
        if (Index >= Data.Num())
        {
            // Grow data array
            const SizeType OldNum = Data.Num();
            Data.SetNumUninitialized(Index + 1);
            AllocationFlags.Add(false, Index + 1 - OldNum);
            
            // Add new indices to free list
            for (SizeType i = OldNum; i < Index; ++i)
            {
                AddToFreeList(i);
            }
        }
        
        // Mark as allocated
        AllocationFlags.SetBit(Index, true);
        
        // Remove from free list if it was there
        if (Data[Index].PrevFreeIndex != INDEX_NONE_VALUE || 
            Data[Index].NextFreeIndex != INDEX_NONE_VALUE ||
            FirstFreeIndex == Index)
        {
            RemoveFromFreeList(Index);
        }
        
        FSparseArrayAllocationInfo Result;
        Result.Index = Index;
        Result.Pointer = &Data[Index].ElementData;
        return Result;
    }
    
    /**
     * Adds an element, reusing a free index if available
     * @return Allocation info with index and pointer
     */
    FSparseArrayAllocationInfo AddUninitialized()
    {
        SizeType Index;
        
        if (NumFreeIndices > 0)
        {
            // Reuse a free index
            Index = FirstFreeIndex;
            RemoveFromFreeList(Index);
        }
        else
        {
            // Allocate new index
            Index = Data.AddUninitialized(1);
            AllocationFlags.Add(true);
        }
        
        AllocationFlags.SetBit(Index, true);
        
        FSparseArrayAllocationInfo Result;
        Result.Index = Index;
        Result.Pointer = &Data[Index].ElementData;
        return Result;
    }
    
    /**
     * Adds an element (copy)
     * @return Index of the new element
     */
    SizeType Add(const ElementType& Element)
    {
        FSparseArrayAllocationInfo Allocation = AddUninitialized();
        new (Allocation.Pointer) ElementType(Element);
        return Allocation.Index;
    }
    
    /**
     * Adds an element (move)
     * @return Index of the new element
     */
    SizeType Add(ElementType&& Element)
    {
        FSparseArrayAllocationInfo Allocation = AddUninitialized();
        new (Allocation.Pointer) ElementType(std::move(Element));
        return Allocation.Index;
    }
    
    /**
     * Constructs an element in place
     * @return Index of the new element
     */
    template<typename... ArgsType>
    SizeType Emplace(ArgsType&&... Args)
    {
        FSparseArrayAllocationInfo Allocation = AddUninitialized();
        new (Allocation.Pointer) ElementType(std::forward<ArgsType>(Args)...);
        return Allocation.Index;
    }
    
    // ========================================================================
    // Removing Elements
    // ========================================================================
    
    /**
     * Removes element at the given index
     */
    void RemoveAt(SizeType Index)
    {
        // Destruct the element
        Data[Index].ElementData.~ElementType();
        
        // Mark as unallocated
        AllocationFlags.SetBit(Index, false);
        
        // Add to free list
        AddToFreeList(Index);
    }
    
    /**
     * Removes element at the given index without calling destructor
     */
    void RemoveAtUninitialized(SizeType Index)
    {
        // Mark as unallocated
        AllocationFlags.SetBit(Index, false);
        
        // Add to free list
        AddToFreeList(Index);
    }
    
    // ========================================================================
    // Memory Management
    // ========================================================================
    
    /**
     * Empties the array
     */
    void Empty(SizeType ExpectedNumElements = 0)
    {
        // Destruct all allocated elements
        const SizeType MaxIndex = GetMaxIndex();
        for (SizeType Index = 0; Index < MaxIndex; ++Index)
        {
            if (IsAllocated(Index))
            {
                Data[Index].ElementData.~ElementType();
            }
        }
        
        Data.Empty(ExpectedNumElements);
        AllocationFlags.Empty(ExpectedNumElements);
        FirstFreeIndex = INDEX_NONE_VALUE;
        NumFreeIndices = 0;
    }
    
    /**
     * Resets the array without deallocating memory
     */
    void Reset()
    {
        // Destruct all allocated elements
        const SizeType MaxIndex = GetMaxIndex();
        for (SizeType Index = 0; Index < MaxIndex; ++Index)
        {
            if (IsAllocated(Index))
            {
                Data[Index].ElementData.~ElementType();
            }
        }
        
        Data.Reset();
        AllocationFlags.Reset();
        FirstFreeIndex = INDEX_NONE_VALUE;
        NumFreeIndices = 0;
    }
    
    /**
     * Reserves capacity
     */
    void Reserve(SizeType ExpectedNumElements)
    {
        if (ExpectedNumElements > Data.Max())
        {
            Data.Reserve(ExpectedNumElements);
        }
    }
    
    /**
     * Shrinks the array to fit its contents
     */
    void Shrink()
    {
        // Compact by removing trailing free elements
        while (Data.Num() > 0 && !IsAllocated(Data.Num() - 1))
        {
            // Remove from free list
            RemoveFromFreeList(Data.Num() - 1);
            
            // Remove from data array
            Data.SetNumUninitialized(Data.Num() - 1);
            AllocationFlags.RemoveAt(AllocationFlags.Num() - 1);
        }
        
        Data.Shrink();
    }
    
    /**
     * Compacts the array, removing gaps
     * Warning: This invalidates existing indices!
     */
    void Compact()
    {
        if (NumFreeIndices == 0)
        {
            return;
        }
        
        // Create new compacted array
        TSparseArray NewArray;
        NewArray.Reserve(Num());
        
        const SizeType MaxIndex = GetMaxIndex();
        for (SizeType Index = 0; Index < MaxIndex; ++Index)
        {
            if (IsAllocated(Index))
            {
                NewArray.Add(std::move(Data[Index].ElementData));
            }
        }
        
        *this = std::move(NewArray);
    }
    
    // ========================================================================
    // Iteration
    // ========================================================================
    
    /**
     * Iterator for sparse array
     */
    class TIterator
    {
    public:
        TIterator(TSparseArray& InArray, SizeType StartIndex = 0)
            : Array(InArray)
            , Index(StartIndex)
        {
            // Find first allocated element
            while (Index < Array.GetMaxIndex() && !Array.IsAllocated(Index))
            {
                ++Index;
            }
        }
        
        TIterator& operator++()
        {
            ++Index;
            while (Index < Array.GetMaxIndex() && !Array.IsAllocated(Index))
            {
                ++Index;
            }
            return *this;
        }
        
        FORCEINLINE explicit operator bool() const
        {
            return Index < Array.GetMaxIndex();
        }
        
        FORCEINLINE ElementType& operator*() const
        {
            return Array[Index];
        }
        
        FORCEINLINE ElementType* operator->() const
        {
            return &Array[Index];
        }
        
        FORCEINLINE SizeType GetIndex() const
        {
            return Index;
        }
        
        void RemoveCurrent()
        {
            Array.RemoveAt(Index);
        }
        
    private:
        TSparseArray& Array;
        SizeType Index;
    };
    
    /**
     * Const iterator for sparse array
     */
    class TConstIterator
    {
    public:
        TConstIterator(const TSparseArray& InArray, SizeType StartIndex = 0)
            : Array(InArray)
            , Index(StartIndex)
        {
            // Find first allocated element
            while (Index < Array.GetMaxIndex() && !Array.IsAllocated(Index))
            {
                ++Index;
            }
        }
        
        TConstIterator& operator++()
        {
            ++Index;
            while (Index < Array.GetMaxIndex() && !Array.IsAllocated(Index))
            {
                ++Index;
            }
            return *this;
        }
        
        FORCEINLINE explicit operator bool() const
        {
            return Index < Array.GetMaxIndex();
        }
        
        FORCEINLINE const ElementType& operator*() const
        {
            return Array[Index];
        }
        
        FORCEINLINE const ElementType* operator->() const
        {
            return &Array[Index];
        }
        
        FORCEINLINE SizeType GetIndex() const
        {
            return Index;
        }
        
    private:
        const TSparseArray& Array;
        SizeType Index;
    };
    
    TIterator CreateIterator()
    {
        return TIterator(*this);
    }
    
    TConstIterator CreateConstIterator() const
    {
        return TConstIterator(*this);
    }
    
    // Range-based for loop support (iterates only over allocated elements)
    class FElementIterator
    {
    public:
        FElementIterator(TSparseArray* InArray, SizeType InIndex)
            : Array(InArray), Index(InIndex)
        {
            AdvanceToAllocated();
        }
        
        FElementIterator& operator++()
        {
            ++Index;
            AdvanceToAllocated();
            return *this;
        }
        
        bool operator!=(const FElementIterator& Other) const
        {
            return Index != Other.Index;
        }
        
        ElementType& operator*() const
        {
            return (*Array)[Index];
        }
        
    private:
        void AdvanceToAllocated()
        {
            while (Index < Array->GetMaxIndex() && !Array->IsAllocated(Index))
            {
                ++Index;
            }
        }
        
        TSparseArray* Array;
        SizeType Index;
    };
    
    class FConstElementIterator
    {
    public:
        FConstElementIterator(const TSparseArray* InArray, SizeType InIndex)
            : Array(InArray), Index(InIndex)
        {
            AdvanceToAllocated();
        }
        
        FConstElementIterator& operator++()
        {
            ++Index;
            AdvanceToAllocated();
            return *this;
        }
        
        bool operator!=(const FConstElementIterator& Other) const
        {
            return Index != Other.Index;
        }
        
        const ElementType& operator*() const
        {
            return (*Array)[Index];
        }
        
    private:
        void AdvanceToAllocated()
        {
            while (Index < Array->GetMaxIndex() && !Array->IsAllocated(Index))
            {
                ++Index;
            }
        }
        
        const TSparseArray* Array;
        SizeType Index;
    };
    
    FElementIterator begin() { return FElementIterator(this, 0); }
    FElementIterator end() { return FElementIterator(this, GetMaxIndex()); }
    FConstElementIterator begin() const { return FConstElementIterator(this, 0); }
    FConstElementIterator end() const { return FConstElementIterator(this, GetMaxIndex()); }
    
private:
    // ========================================================================
    // Free List Management
    // ========================================================================
    
    void AddToFreeList(SizeType Index)
    {
        // Add to front of free list
        Data[Index].PrevFreeIndex = INDEX_NONE_VALUE;
        Data[Index].NextFreeIndex = FirstFreeIndex;
        
        if (FirstFreeIndex != INDEX_NONE_VALUE)
        {
            Data[FirstFreeIndex].PrevFreeIndex = Index;
        }
        
        FirstFreeIndex = Index;
        ++NumFreeIndices;
    }
    
    void RemoveFromFreeList(SizeType Index)
    {
        const SizeType PrevIndex = Data[Index].PrevFreeIndex;
        const SizeType NextIndex = Data[Index].NextFreeIndex;
        
        if (PrevIndex != INDEX_NONE_VALUE)
        {
            Data[PrevIndex].NextFreeIndex = NextIndex;
        }
        else
        {
            FirstFreeIndex = NextIndex;
        }
        
        if (NextIndex != INDEX_NONE_VALUE)
        {
            Data[NextIndex].PrevFreeIndex = PrevIndex;
        }
        
        --NumFreeIndices;
    }
    
private:
    DataArrayType Data;
    AllocationBitArrayType AllocationFlags;
    SizeType FirstFreeIndex;
    SizeType NumFreeIndices;
};

} // namespace MonsterEngine
