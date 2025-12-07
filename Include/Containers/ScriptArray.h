// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file ScriptArray.h
 * @brief Untyped array container for reflection following UE5 TScriptArray patterns
 * 
 * TScriptArray is an untyped dynamic array that:
 * - Mirrors TArray's memory layout
 * - Allows manipulation without knowing element type at compile time
 * - Used by the reflection system for property access
 * - Proxy pattern for TArray
 */

#include "Core/CoreTypes.h"
#include "Core/Templates/TypeTraits.h"
#include "ContainerAllocationPolicies.h"
#include "ContainerFwd.h"

#include <cstring>

namespace MonsterEngine
{

// ============================================================================
// FScriptContainerElement
// ============================================================================

/**
 * Placeholder type for script container elements
 * Used when the actual element type is unknown at compile time
 */
struct FScriptContainerElement
{
};

// ============================================================================
// TScriptArray
// ============================================================================

/**
 * Base dynamic array for script/reflection use
 * 
 * An untyped data array that mirrors TArray's members but doesn't need
 * an exact C++ type for its elements. Used by the reflection system
 * to manipulate arrays without knowing their element type.
 * 
 * @tparam AllocatorType The allocator policy
 */
template<typename AllocatorType = FDefaultAllocator>
class TScriptArray : protected AllocatorType::ForAnyElementType
{
public:
    using SizeType = typename AllocatorType::SizeType;
    
    // ========================================================================
    // Constructors and Destructor
    // ========================================================================
    
    /**
     * Default constructor
     */
    TScriptArray()
        : ArrayNum(0)
        , ArrayMax(0)
    {
    }
    
    /**
     * Move constructor
     */
    TScriptArray(TScriptArray&& Other) noexcept
        : AllocatorType::ForAnyElementType(std::move(static_cast<typename AllocatorType::ForAnyElementType&>(Other)))
        , ArrayNum(Other.ArrayNum)
        , ArrayMax(Other.ArrayMax)
    {
        Other.ArrayNum = 0;
        Other.ArrayMax = 0;
    }
    
    /**
     * Destructor - does NOT destruct elements (caller must handle)
     */
    ~TScriptArray() = default;
    
    // No copy - must be done with element type knowledge
    TScriptArray(const TScriptArray&) = delete;
    TScriptArray& operator=(const TScriptArray&) = delete;
    
    /**
     * Move assignment
     */
    TScriptArray& operator=(TScriptArray&& Other) noexcept
    {
        if (this != &Other)
        {
            static_cast<typename AllocatorType::ForAnyElementType&>(*this) = 
                std::move(static_cast<typename AllocatorType::ForAnyElementType&>(Other));
            ArrayNum = Other.ArrayNum;
            ArrayMax = Other.ArrayMax;
            Other.ArrayNum = 0;
            Other.ArrayMax = 0;
        }
        return *this;
    }
    
    // ========================================================================
    // Data Access
    // ========================================================================
    
    /**
     * Returns pointer to data
     */
    FORCEINLINE void* GetData()
    {
        return this->GetAllocation();
    }
    
    /**
     * Returns const pointer to data
     */
    FORCEINLINE const void* GetData() const
    {
        return this->GetAllocation();
    }
    
    // ========================================================================
    // Size and Capacity
    // ========================================================================
    
    /**
     * Returns number of elements
     */
    FORCEINLINE SizeType Num() const
    {
        return ArrayNum;
    }
    
    /**
     * Returns maximum capacity
     */
    FORCEINLINE SizeType Max() const
    {
        return ArrayMax;
    }
    
    /**
     * Returns true if empty
     */
    FORCEINLINE bool IsEmpty() const
    {
        return ArrayNum == 0;
    }
    
    /**
     * Returns true if index is valid
     */
    FORCEINLINE bool IsValidIndex(SizeType Index) const
    {
        return Index >= 0 && Index < ArrayNum;
    }
    
    // ========================================================================
    // Element Operations (require element size/alignment)
    // ========================================================================
    
    /**
     * Adds uninitialized elements
     * @return Index of first new element
     */
    SizeType Add(SizeType Count, SizeType NumBytesPerElement, uint32 AlignmentOfElement)
    {
        const SizeType OldNum = ArrayNum;
        
        if ((ArrayNum += Count) > ArrayMax)
        {
            ResizeGrow(OldNum, NumBytesPerElement, AlignmentOfElement);
        }
        
        return OldNum;
    }
    
    /**
     * Adds zeroed elements
     * @return Index of first new element
     */
    SizeType AddZeroed(SizeType Count, SizeType NumBytesPerElement, uint32 AlignmentOfElement)
    {
        const SizeType Index = Add(Count, NumBytesPerElement, AlignmentOfElement);
        std::memset(static_cast<uint8*>(GetData()) + Index * NumBytesPerElement, 0, Count * NumBytesPerElement);
        return Index;
    }
    
    /**
     * Inserts uninitialized elements
     */
    void Insert(SizeType Index, SizeType Count, SizeType NumBytesPerElement, uint32 AlignmentOfElement)
    {
        const SizeType OldNum = ArrayNum;
        
        if ((ArrayNum += Count) > ArrayMax)
        {
            ResizeGrow(OldNum, NumBytesPerElement, AlignmentOfElement);
        }
        
        // Move existing elements
        uint8* Data = static_cast<uint8*>(GetData());
        std::memmove(
            Data + (Index + Count) * NumBytesPerElement,
            Data + Index * NumBytesPerElement,
            (OldNum - Index) * NumBytesPerElement
        );
    }
    
    /**
     * Inserts zeroed elements
     */
    void InsertZeroed(SizeType Index, SizeType Count, SizeType NumBytesPerElement, uint32 AlignmentOfElement)
    {
        Insert(Index, Count, NumBytesPerElement, AlignmentOfElement);
        std::memset(static_cast<uint8*>(GetData()) + Index * NumBytesPerElement, 0, Count * NumBytesPerElement);
    }
    
    /**
     * Removes elements (does NOT destruct - caller must handle)
     */
    void Remove(SizeType Index, SizeType Count, SizeType NumBytesPerElement, uint32 AlignmentOfElement)
    {
        // Move remaining elements
        uint8* Data = static_cast<uint8*>(GetData());
        const SizeType NumToMove = ArrayNum - Index - Count;
        
        if (NumToMove > 0)
        {
            std::memmove(
                Data + Index * NumBytesPerElement,
                Data + (Index + Count) * NumBytesPerElement,
                NumToMove * NumBytesPerElement
            );
        }
        
        ArrayNum -= Count;
    }
    
    /**
     * Removes elements by swapping with end (does NOT destruct)
     */
    void RemoveSwap(SizeType Index, SizeType Count, SizeType NumBytesPerElement)
    {
        uint8* Data = static_cast<uint8*>(GetData());
        const SizeType NumToMove = std::min(Count, ArrayNum - Index - Count);
        
        if (NumToMove > 0)
        {
            std::memcpy(
                Data + Index * NumBytesPerElement,
                Data + (ArrayNum - NumToMove) * NumBytesPerElement,
                NumToMove * NumBytesPerElement
            );
        }
        
        ArrayNum -= Count;
    }
    
    // ========================================================================
    // Memory Management
    // ========================================================================
    
    /**
     * Empties the array (does NOT destruct elements)
     */
    void Empty(SizeType ExpectedNumElements, SizeType NumBytesPerElement, uint32 AlignmentOfElement)
    {
        ArrayNum = 0;
        
        if (ExpectedNumElements != ArrayMax)
        {
            ResizeTo(ExpectedNumElements, NumBytesPerElement, AlignmentOfElement);
        }
    }
    
    /**
     * Shrinks capacity to fit contents
     */
    void Shrink(SizeType NumBytesPerElement, uint32 AlignmentOfElement)
    {
        if (ArrayNum != ArrayMax)
        {
            ResizeTo(ArrayNum, NumBytesPerElement, AlignmentOfElement);
        }
    }
    
    /**
     * Sets number of elements (does NOT construct/destruct)
     */
    void SetNumUninitialized(SizeType NewNum, SizeType NumBytesPerElement, uint32 AlignmentOfElement, bool bAllowShrinking = true)
    {
        if (NewNum > ArrayMax)
        {
            ResizeTo(NewNum, NumBytesPerElement, AlignmentOfElement);
        }
        else if (bAllowShrinking && NewNum < ArrayNum)
        {
            // Optionally shrink
            const SizeType NewMax = this->CalculateSlackShrink(NewNum, ArrayMax, NumBytesPerElement, AlignmentOfElement);
            if (NewMax != ArrayMax)
            {
                ResizeTo(NewMax, NumBytesPerElement, AlignmentOfElement);
            }
        }
        
        ArrayNum = NewNum;
    }
    
    /**
     * Reserves capacity
     */
    void Reserve(SizeType NumElements, SizeType NumBytesPerElement, uint32 AlignmentOfElement)
    {
        if (NumElements > ArrayMax)
        {
            ResizeTo(NumElements, NumBytesPerElement, AlignmentOfElement);
        }
    }
    
    // ========================================================================
    // Raw Access (for reflection system)
    // ========================================================================
    
    /**
     * Gets pointer to element at index
     */
    void* GetElementPtr(SizeType Index, SizeType NumBytesPerElement)
    {
        return static_cast<uint8*>(GetData()) + Index * NumBytesPerElement;
    }
    
    /**
     * Gets const pointer to element at index
     */
    const void* GetElementPtr(SizeType Index, SizeType NumBytesPerElement) const
    {
        return static_cast<const uint8*>(GetData()) + Index * NumBytesPerElement;
    }
    
    /**
     * Copies element from source to destination index
     */
    void CopyElement(SizeType DestIndex, SizeType SrcIndex, SizeType NumBytesPerElement)
    {
        uint8* Data = static_cast<uint8*>(GetData());
        std::memcpy(
            Data + DestIndex * NumBytesPerElement,
            Data + SrcIndex * NumBytesPerElement,
            NumBytesPerElement
        );
    }
    
    /**
     * Swaps two elements
     */
    void SwapElements(SizeType IndexA, SizeType IndexB, SizeType NumBytesPerElement)
    {
        if (IndexA != IndexB)
        {
            uint8* Data = static_cast<uint8*>(GetData());
            uint8* PtrA = Data + IndexA * NumBytesPerElement;
            uint8* PtrB = Data + IndexB * NumBytesPerElement;
            
            // Swap using temporary buffer
            for (SizeType i = 0; i < NumBytesPerElement; ++i)
            {
                uint8 Temp = PtrA[i];
                PtrA[i] = PtrB[i];
                PtrB[i] = Temp;
            }
        }
    }
    
private:
    void ResizeGrow(SizeType OldNum, SizeType NumBytesPerElement, uint32 AlignmentOfElement)
    {
        ArrayMax = this->CalculateSlackGrow(ArrayNum, ArrayMax, NumBytesPerElement, AlignmentOfElement);
        this->ResizeAllocation(OldNum, ArrayMax, NumBytesPerElement, AlignmentOfElement);
    }
    
    void ResizeTo(SizeType NewMax, SizeType NumBytesPerElement, uint32 AlignmentOfElement)
    {
        if (NewMax != ArrayMax)
        {
            this->ResizeAllocation(ArrayNum, NewMax, NumBytesPerElement, AlignmentOfElement);
            ArrayMax = NewMax;
        }
    }
    
protected:
    SizeType ArrayNum;
    SizeType ArrayMax;
};

// ============================================================================
// FScriptArray
// ============================================================================

/**
 * Default script array using default allocator
 */
using FScriptArray = TScriptArray<FDefaultAllocator>;

// ============================================================================
// Script Array Helper
// ============================================================================

/**
 * Helper class for manipulating TArray through reflection
 * Wraps a TArray and provides type-erased access
 */
class FScriptArrayHelper
{
public:
    /**
     * Constructor
     * @param InArray Pointer to the TScriptArray
     * @param InElementSize Size of each element in bytes
     * @param InElementAlignment Alignment of elements
     */
    FScriptArrayHelper(FScriptArray* InArray, int32 InElementSize, uint32 InElementAlignment)
        : Array(InArray)
        , ElementSize(InElementSize)
        , ElementAlignment(InElementAlignment)
    {
    }
    
    /**
     * Returns number of elements
     */
    int32 Num() const
    {
        return Array->Num();
    }
    
    /**
     * Returns true if empty
     */
    bool IsEmpty() const
    {
        return Array->IsEmpty();
    }
    
    /**
     * Returns true if index is valid
     */
    bool IsValidIndex(int32 Index) const
    {
        return Array->IsValidIndex(Index);
    }
    
    /**
     * Gets raw pointer to element
     */
    void* GetRawPtr(int32 Index)
    {
        return Array->GetElementPtr(Index, ElementSize);
    }
    
    /**
     * Gets const raw pointer to element
     */
    const void* GetRawPtr(int32 Index) const
    {
        return Array->GetElementPtr(Index, ElementSize);
    }
    
    /**
     * Adds uninitialized element
     * @return Index of new element
     */
    int32 AddUninitializedValue()
    {
        return Array->Add(1, ElementSize, ElementAlignment);
    }
    
    /**
     * Adds multiple uninitialized elements
     * @return Index of first new element
     */
    int32 AddUninitializedValues(int32 Count)
    {
        return Array->Add(Count, ElementSize, ElementAlignment);
    }
    
    /**
     * Inserts uninitialized element
     */
    void InsertUninitializedValue(int32 Index)
    {
        Array->Insert(Index, 1, ElementSize, ElementAlignment);
    }
    
    /**
     * Removes element (does NOT destruct)
     */
    void RemoveValues(int32 Index, int32 Count = 1)
    {
        Array->Remove(Index, Count, ElementSize, ElementAlignment);
    }
    
    /**
     * Empties array (does NOT destruct elements)
     */
    void EmptyValues(int32 ExpectedNumElements = 0)
    {
        Array->Empty(ExpectedNumElements, ElementSize, ElementAlignment);
    }
    
    /**
     * Resizes array (does NOT construct/destruct)
     */
    void Resize(int32 NewNum)
    {
        Array->SetNumUninitialized(NewNum, ElementSize, ElementAlignment);
    }
    
private:
    FScriptArray* Array;
    int32 ElementSize;
    uint32 ElementAlignment;
};

} // namespace MonsterEngine
