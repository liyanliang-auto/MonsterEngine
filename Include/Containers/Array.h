// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file Array.h
 * @brief Dynamic array container following UE5 TArray patterns
 * 
 * TArray is a dynamically sized array with the following features:
 * - Configurable allocator policy
 * - Slack mechanism for efficient growth
 * - Optimized memory operations for trivially copyable types
 * - Serialization support
 * - Range-based for loop support
 */

#include "Core/CoreTypes.h"
#include "Core/Templates/TypeTraits.h"
#include "Core/Templates/TypeHash.h"
#include "Core/Templates/MemoryOps.h"
#include "ContainerAllocationPolicies.h"
#include "ContainerFwd.h"

#include <algorithm>
#include <initializer_list>
#include <type_traits>

namespace MonsterEngine
{

// Forward declaration for serialization
class FArchive;

// ============================================================================
// TIndexedContainerIterator
// ============================================================================

/**
 * Generic iterator for indexed containers
 */
template<typename ContainerType, typename ElementType, typename SizeType>
class TIndexedContainerIterator
{
public:
    TIndexedContainerIterator(ContainerType& InContainer, SizeType StartIndex = 0)
        : Container(InContainer)
        , Index(StartIndex)
    {
    }
    
    TIndexedContainerIterator& operator++()
    {
        ++Index;
        return *this;
    }
    
    TIndexedContainerIterator operator++(int)
    {
        TIndexedContainerIterator Tmp(*this);
        ++Index;
        return Tmp;
    }
    
    TIndexedContainerIterator& operator--()
    {
        --Index;
        return *this;
    }
    
    TIndexedContainerIterator operator--(int)
    {
        TIndexedContainerIterator Tmp(*this);
        --Index;
        return Tmp;
    }
    
    TIndexedContainerIterator& operator+=(SizeType Offset)
    {
        Index += Offset;
        return *this;
    }
    
    TIndexedContainerIterator operator+(SizeType Offset) const
    {
        TIndexedContainerIterator Tmp(*this);
        return Tmp += Offset;
    }
    
    TIndexedContainerIterator& operator-=(SizeType Offset)
    {
        return *this += -Offset;
    }
    
    TIndexedContainerIterator operator-(SizeType Offset) const
    {
        TIndexedContainerIterator Tmp(*this);
        return Tmp -= Offset;
    }
    
    FORCEINLINE ElementType& operator*() const
    {
        return Container[Index];
    }
    
    FORCEINLINE ElementType* operator->() const
    {
        return &Container[Index];
    }
    
    FORCEINLINE explicit operator bool() const
    {
        return Container.IsValidIndex(Index);
    }
    
    SizeType GetIndex() const
    {
        return Index;
    }
    
    void Reset()
    {
        Index = 0;
    }
    
    void SetToEnd()
    {
        Index = Container.Num();
    }
    
    FORCEINLINE bool operator==(const TIndexedContainerIterator& Other) const
    {
        return &Container == &Other.Container && Index == Other.Index;
    }
    
    FORCEINLINE bool operator!=(const TIndexedContainerIterator& Other) const
    {
        return !(*this == Other);
    }
    
private:
    ContainerType& Container;
    SizeType Index;
};

// ============================================================================
// TArray
// ============================================================================

/**
 * Templated dynamic array
 * 
 * A dynamically sized array of typed elements. Makes the assumption that your
 * elements are relocate-able; i.e. that they can be transparently moved to new
 * memory without a copy constructor.
 * 
 * @tparam InElementType The type of elements stored in the array
 * @tparam InAllocatorType The allocator policy to use
 */
template<typename InElementType, typename InAllocatorType>
class TArray
{
    template<typename OtherElementType, typename OtherAllocator>
    friend class TArray;
    
public:
    using SizeType = typename InAllocatorType::SizeType;
    using ElementType = InElementType;
    using AllocatorType = InAllocatorType;
    
private:
    using USizeType = TMakeUnsigned_T<SizeType>;
    
    // Choose the appropriate allocator instance type
    using ElementAllocatorType = TConditional_T<
        InAllocatorType::NeedsElementType,
        typename InAllocatorType::template ForElementType<ElementType>,
        typename InAllocatorType::ForAnyElementType
    >;
    
public:
    // ========================================================================
    // Constructors and Destructor
    // ========================================================================
    
    /**
     * Default constructor
     */
    FORCEINLINE TArray()
        : ArrayNum(0)
        , ArrayMax(AllocatorInstance.GetInitialCapacity())
    {
    }
    
    /**
     * Constructor from initializer list
     */
    TArray(std::initializer_list<ElementType> InitList)
        : ArrayNum(0)
        , ArrayMax(AllocatorInstance.GetInitialCapacity())
    {
        Reserve(static_cast<SizeType>(InitList.size()));
        for (const ElementType& Element : InitList)
        {
            Add(Element);
        }
    }
    
    /**
     * Constructor from raw array
     */
    TArray(const ElementType* Ptr, SizeType Count)
        : ArrayNum(0)
        , ArrayMax(AllocatorInstance.GetInitialCapacity())
    {
        Append(Ptr, Count);
    }
    
    /**
     * Copy constructor
     */
    TArray(const TArray& Other)
        : ArrayNum(0)
        , ArrayMax(AllocatorInstance.GetInitialCapacity())
    {
        CopyFrom(Other);
    }
    
    /**
     * Move constructor
     */
    TArray(TArray&& Other) noexcept
        : ArrayNum(Other.ArrayNum)
        , ArrayMax(Other.ArrayMax)
        , AllocatorInstance(std::move(Other.AllocatorInstance))
    {
        Other.ArrayNum = 0;
        Other.ArrayMax = Other.AllocatorInstance.GetInitialCapacity();
    }
    
    /**
     * Destructor
     */
    ~TArray()
    {
        DestructItems(GetData(), ArrayNum);
        ArrayNum = 0;
        // Allocator destructor handles memory deallocation
    }
    
    // ========================================================================
    // Assignment Operators
    // ========================================================================
    
    /**
     * Copy assignment operator
     */
    TArray& operator=(const TArray& Other)
    {
        if (this != &Other)
        {
            CopyFrom(Other);
        }
        return *this;
    }
    
    /**
     * Move assignment operator
     */
    TArray& operator=(TArray&& Other) noexcept
    {
        if (this != &Other)
        {
            DestructItems(GetData(), ArrayNum);
            AllocatorInstance = std::move(Other.AllocatorInstance);
            ArrayNum = Other.ArrayNum;
            ArrayMax = Other.ArrayMax;
            Other.ArrayNum = 0;
            Other.ArrayMax = Other.AllocatorInstance.GetInitialCapacity();
        }
        return *this;
    }
    
    /**
     * Assignment from initializer list
     */
    TArray& operator=(std::initializer_list<ElementType> InitList)
    {
        Empty(static_cast<SizeType>(InitList.size()));
        for (const ElementType& Element : InitList)
        {
            Add(Element);
        }
        return *this;
    }
    
    // ========================================================================
    // Element Access
    // ========================================================================
    
    /**
     * Array bracket operator - returns reference to element at given index
     */
    FORCEINLINE ElementType& operator[](SizeType Index)
    {
        RangeCheck(Index);
        return GetData()[Index];
    }
    
    /**
     * Array bracket operator - returns const reference to element at given index
     */
    FORCEINLINE const ElementType& operator[](SizeType Index) const
    {
        RangeCheck(Index);
        return GetData()[Index];
    }
    
    /**
     * Returns pointer to the array's data
     */
    FORCEINLINE ElementType* GetData()
    {
        return static_cast<ElementType*>(AllocatorInstance.GetAllocation());
    }
    
    /**
     * Returns const pointer to the array's data
     */
    FORCEINLINE const ElementType* GetData() const
    {
        return static_cast<const ElementType*>(AllocatorInstance.GetAllocation());
    }
    
    /**
     * Returns reference to first element
     */
    FORCEINLINE ElementType& First()
    {
        return GetData()[0];
    }
    
    /**
     * Returns const reference to first element
     */
    FORCEINLINE const ElementType& First() const
    {
        return GetData()[0];
    }
    
    /**
     * Returns reference to last element
     */
    FORCEINLINE ElementType& Last()
    {
        return GetData()[ArrayNum - 1];
    }
    
    /**
     * Returns const reference to last element
     */
    FORCEINLINE const ElementType& Last() const
    {
        return GetData()[ArrayNum - 1];
    }
    
    /**
     * Returns reference to element at index from the end
     */
    FORCEINLINE ElementType& Last(SizeType IndexFromEnd)
    {
        return GetData()[ArrayNum - IndexFromEnd - 1];
    }
    
    /**
     * Returns const reference to element at index from the end
     */
    FORCEINLINE const ElementType& Last(SizeType IndexFromEnd) const
    {
        return GetData()[ArrayNum - IndexFromEnd - 1];
    }
    
    /**
     * Returns reference to top element (same as Last)
     */
    FORCEINLINE ElementType& Top()
    {
        return Last();
    }
    
    /**
     * Returns const reference to top element (same as Last)
     */
    FORCEINLINE const ElementType& Top() const
    {
        return Last();
    }
    
    // ========================================================================
    // Size and Capacity
    // ========================================================================
    
    /**
     * Returns number of elements in array
     */
    FORCEINLINE SizeType Num() const
    {
        return ArrayNum;
    }
    
    /**
     * Returns maximum number of elements array can hold without reallocation
     */
    FORCEINLINE SizeType Max() const
    {
        return ArrayMax;
    }
    
    /**
     * Returns true if array is empty
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
    
    /**
     * Returns the amount of slack (unused capacity)
     */
    FORCEINLINE SizeType GetSlack() const
    {
        return ArrayMax - ArrayNum;
    }
    
    // ========================================================================
    // Adding Elements
    // ========================================================================
    
    /**
     * Adds a new element to the end of the array (copy)
     * @return Index of the new element
     */
    SizeType Add(const ElementType& Item)
    {
        const SizeType Index = AddUninitialized(1);
        new (GetData() + Index) ElementType(Item);
        return Index;
    }
    
    /**
     * Adds a new element to the end of the array (move)
     * @return Index of the new element
     */
    SizeType Add(ElementType&& Item)
    {
        const SizeType Index = AddUninitialized(1);
        new (GetData() + Index) ElementType(std::move(Item));
        return Index;
    }
    
    /**
     * Constructs a new element at the end of the array
     * @return Reference to the new element
     */
    template<typename... ArgsType>
    ElementType& Emplace(ArgsType&&... Args)
    {
        const SizeType Index = AddUninitialized(1);
        new (GetData() + Index) ElementType(std::forward<ArgsType>(Args)...);
        return GetData()[Index];
    }
    
    /**
     * Adds a new element at the end, returning its index
     */
    template<typename... ArgsType>
    SizeType Emplace_GetRef(ArgsType&&... Args)
    {
        const SizeType Index = AddUninitialized(1);
        new (GetData() + Index) ElementType(std::forward<ArgsType>(Args)...);
        return Index;
    }
    
    /**
     * Adds uninitialized elements to the end of the array
     * @return Index of the first new element
     */
    SizeType AddUninitialized(SizeType Count = 1)
    {
        const SizeType OldNum = ArrayNum;
        const SizeType NewNum = OldNum + Count;
        
        if (NewNum > ArrayMax)
        {
            ResizeGrow(OldNum);
        }
        
        ArrayNum = NewNum;
        return OldNum;
    }
    
    /**
     * Adds zeroed elements to the end of the array
     * @return Index of the first new element
     */
    SizeType AddZeroed(SizeType Count = 1)
    {
        const SizeType Index = AddUninitialized(Count);
        std::memset(GetData() + Index, 0, sizeof(ElementType) * Count);
        return Index;
    }
    
    /**
     * Adds default-constructed elements to the end of the array
     * @return Index of the first new element
     */
    SizeType AddDefaulted(SizeType Count = 1)
    {
        const SizeType Index = AddUninitialized(Count);
        DefaultConstructItems(GetData() + Index, Count);
        return Index;
    }
    
    /**
     * Adds unique element (only if not already present)
     * @return Index of the element (existing or new)
     */
    SizeType AddUnique(const ElementType& Item)
    {
        SizeType Index = Find(Item);
        if (Index == INDEX_NONE_VALUE)
        {
            Index = Add(Item);
        }
        return Index;
    }
    
    /**
     * Appends elements from another array
     */
    void Append(const TArray& Source)
    {
        Append(Source.GetData(), Source.Num());
    }
    
    /**
     * Appends elements from a raw array
     */
    void Append(const ElementType* Ptr, SizeType Count)
    {
        if (Count > 0)
        {
            const SizeType Index = AddUninitialized(Count);
            CopyConstructItems(GetData() + Index, Ptr, Count);
        }
    }
    
    /**
     * Appends elements from an initializer list
     */
    void Append(std::initializer_list<ElementType> InitList)
    {
        Append(InitList.begin(), static_cast<SizeType>(InitList.size()));
    }
    
    /**
     * Operator += for appending
     */
    TArray& operator+=(const TArray& Other)
    {
        Append(Other);
        return *this;
    }
    
    /**
     * Operator += for appending single element
     */
    TArray& operator+=(const ElementType& Item)
    {
        Add(Item);
        return *this;
    }
    
    /**
     * Push element to the end (same as Add)
     */
    void Push(const ElementType& Item)
    {
        Add(Item);
    }
    
    /**
     * Push element to the end (move version)
     */
    void Push(ElementType&& Item)
    {
        Add(std::move(Item));
    }
    
    // ========================================================================
    // Inserting Elements
    // ========================================================================
    
    /**
     * Inserts an element at the given index
     */
    void Insert(const ElementType& Item, SizeType Index)
    {
        InsertUninitialized(Index, 1);
        new (GetData() + Index) ElementType(Item);
    }
    
    /**
     * Inserts an element at the given index (move)
     */
    void Insert(ElementType&& Item, SizeType Index)
    {
        InsertUninitialized(Index, 1);
        new (GetData() + Index) ElementType(std::move(Item));
    }
    
    /**
     * Inserts uninitialized elements at the given index
     */
    void InsertUninitialized(SizeType Index, SizeType Count = 1)
    {
        RangeCheck(Index <= ArrayNum);
        
        const SizeType OldNum = ArrayNum;
        const SizeType NewNum = OldNum + Count;
        
        if (NewNum > ArrayMax)
        {
            ResizeGrow(OldNum);
        }
        
        ArrayNum = NewNum;
        
        // Move existing elements to make room
        ElementType* Data = GetData();
        if (Index < OldNum)
        {
            MoveItems(Data + Index + Count, Data + Index, OldNum - Index);
        }
    }
    
    /**
     * Inserts zeroed elements at the given index
     */
    void InsertZeroed(SizeType Index, SizeType Count = 1)
    {
        InsertUninitialized(Index, Count);
        std::memset(GetData() + Index, 0, sizeof(ElementType) * Count);
    }
    
    /**
     * Inserts default-constructed elements at the given index
     */
    void InsertDefaulted(SizeType Index, SizeType Count = 1)
    {
        InsertUninitialized(Index, Count);
        DefaultConstructItems(GetData() + Index, Count);
    }
    
    // ========================================================================
    // Removing Elements
    // ========================================================================
    
    /**
     * Removes element at the given index, shifting remaining elements
     */
    void RemoveAt(SizeType Index, SizeType Count = 1, bool bAllowShrinking = true)
    {
        RangeCheck(Index >= 0 && Index + Count <= ArrayNum);
        
        ElementType* Data = GetData();
        
        // Destruct elements being removed
        DestructItems(Data + Index, Count);
        
        // Move remaining elements
        const SizeType NumToMove = ArrayNum - Index - Count;
        if (NumToMove > 0)
        {
            MoveItems(Data + Index, Data + Index + Count, NumToMove);
        }
        
        ArrayNum -= Count;
        
        if (bAllowShrinking)
        {
            ResizeShrink();
        }
    }
    
    /**
     * Removes element at the given index by swapping with last element (faster, doesn't preserve order)
     */
    void RemoveAtSwap(SizeType Index, SizeType Count = 1, bool bAllowShrinking = true)
    {
        RangeCheck(Index >= 0 && Index + Count <= ArrayNum);
        
        ElementType* Data = GetData();
        
        // Destruct elements being removed
        DestructItems(Data + Index, Count);
        
        // Move elements from end to fill gap
        const SizeType NumToMove = std::min(Count, ArrayNum - Index - Count);
        if (NumToMove > 0)
        {
            RelocateConstructItems(Data + Index, Data + ArrayNum - NumToMove, NumToMove);
        }
        
        ArrayNum -= Count;
        
        if (bAllowShrinking)
        {
            ResizeShrink();
        }
    }
    
    /**
     * Removes first occurrence of element
     * @return Number of elements removed (0 or 1)
     */
    SizeType Remove(const ElementType& Item)
    {
        SizeType Index = Find(Item);
        if (Index != INDEX_NONE_VALUE)
        {
            RemoveAt(Index);
            return 1;
        }
        return 0;
    }
    
    /**
     * Removes first occurrence of element using swap (faster, doesn't preserve order)
     * @return Number of elements removed (0 or 1)
     */
    SizeType RemoveSwap(const ElementType& Item)
    {
        SizeType Index = Find(Item);
        if (Index != INDEX_NONE_VALUE)
        {
            RemoveAtSwap(Index);
            return 1;
        }
        return 0;
    }
    
    /**
     * Removes all occurrences of element
     * @return Number of elements removed
     */
    SizeType RemoveAll(const ElementType& Item)
    {
        SizeType NumRemoved = 0;
        SizeType Index = Find(Item);
        while (Index != INDEX_NONE_VALUE)
        {
            RemoveAt(Index);
            ++NumRemoved;
            Index = Find(Item);
        }
        return NumRemoved;
    }
    
    /**
     * Pops element from the end
     * @return The removed element
     */
    ElementType Pop(bool bAllowShrinking = true)
    {
        ElementType Result = std::move(Last());
        RemoveAt(ArrayNum - 1, 1, bAllowShrinking);
        return Result;
    }
    
    // ========================================================================
    // Finding Elements
    // ========================================================================
    
    /**
     * Finds first occurrence of element
     * @return Index of element, or INDEX_NONE_VALUE if not found
     */
    SizeType Find(const ElementType& Item) const
    {
        const ElementType* Data = GetData();
        for (SizeType i = 0; i < ArrayNum; ++i)
        {
            if (Data[i] == Item)
            {
                return i;
            }
        }
        return INDEX_NONE_VALUE;
    }
    
    /**
     * Finds last occurrence of element
     * @return Index of element, or INDEX_NONE_VALUE if not found
     */
    SizeType FindLast(const ElementType& Item) const
    {
        const ElementType* Data = GetData();
        for (SizeType i = ArrayNum - 1; i >= 0; --i)
        {
            if (Data[i] == Item)
            {
                return i;
            }
        }
        return INDEX_NONE_VALUE;
    }
    
    /**
     * Checks if array contains element
     */
    bool Contains(const ElementType& Item) const
    {
        return Find(Item) != INDEX_NONE_VALUE;
    }
    
    /**
     * Finds element matching predicate
     * @return Pointer to element, or nullptr if not found
     */
    template<typename Predicate>
    ElementType* FindByPredicate(Predicate Pred)
    {
        ElementType* Data = GetData();
        for (SizeType i = 0; i < ArrayNum; ++i)
        {
            if (Pred(Data[i]))
            {
                return &Data[i];
            }
        }
        return nullptr;
    }
    
    /**
     * Finds element matching predicate (const version)
     */
    template<typename Predicate>
    const ElementType* FindByPredicate(Predicate Pred) const
    {
        return const_cast<TArray*>(this)->FindByPredicate(Pred);
    }
    
    /**
     * Finds index of element matching predicate
     * @return Index of element, or INDEX_NONE_VALUE if not found
     */
    template<typename Predicate>
    SizeType IndexOfByPredicate(Predicate Pred) const
    {
        const ElementType* Data = GetData();
        for (SizeType i = 0; i < ArrayNum; ++i)
        {
            if (Pred(Data[i]))
            {
                return i;
            }
        }
        return INDEX_NONE_VALUE;
    }
    
    /**
     * Checks if any element matches predicate
     */
    template<typename Predicate>
    bool ContainsByPredicate(Predicate Pred) const
    {
        return FindByPredicate(Pred) != nullptr;
    }
    
    // ========================================================================
    // Memory Management
    // ========================================================================
    
    /**
     * Empties the array, optionally reserving capacity
     */
    void Empty(SizeType ExpectedNumElements = 0)
    {
        DestructItems(GetData(), ArrayNum);
        ArrayNum = 0;
        
        if (ExpectedNumElements > ArrayMax || ExpectedNumElements == 0)
        {
            ResizeTo(ExpectedNumElements);
        }
    }
    
    /**
     * Resets the array without deallocating memory
     */
    void Reset(SizeType NewSize = 0)
    {
        DestructItems(GetData(), ArrayNum);
        ArrayNum = 0;
        
        if (NewSize > ArrayMax)
        {
            ResizeTo(NewSize);
        }
    }
    
    /**
     * Reserves capacity for at least NumElements
     */
    void Reserve(SizeType NumElements)
    {
        if (NumElements > ArrayMax)
        {
            ResizeTo(NumElements);
        }
    }
    
    /**
     * Shrinks the array's capacity to fit its contents
     */
    void Shrink()
    {
        if (ArrayMax != ArrayNum)
        {
            ResizeTo(ArrayNum);
        }
    }
    
    /**
     * Sets the number of elements, constructing or destructing as needed
     */
    void SetNum(SizeType NewNum, bool bAllowShrinking = true)
    {
        if (NewNum > ArrayNum)
        {
            const SizeType NumToAdd = NewNum - ArrayNum;
            AddDefaulted(NumToAdd);
        }
        else if (NewNum < ArrayNum)
        {
            RemoveAt(NewNum, ArrayNum - NewNum, bAllowShrinking);
        }
    }
    
    /**
     * Sets the number of elements without constructing new elements
     */
    void SetNumUninitialized(SizeType NewNum, bool bAllowShrinking = true)
    {
        if (NewNum > ArrayNum)
        {
            AddUninitialized(NewNum - ArrayNum);
        }
        else if (NewNum < ArrayNum)
        {
            RemoveAt(NewNum, ArrayNum - NewNum, bAllowShrinking);
        }
    }
    
    /**
     * Sets the number of elements, zeroing new elements
     */
    void SetNumZeroed(SizeType NewNum, bool bAllowShrinking = true)
    {
        if (NewNum > ArrayNum)
        {
            AddZeroed(NewNum - ArrayNum);
        }
        else if (NewNum < ArrayNum)
        {
            RemoveAt(NewNum, ArrayNum - NewNum, bAllowShrinking);
        }
    }
    
    // ========================================================================
    // Sorting
    // ========================================================================
    
    /**
     * Sorts the array using default comparison
     */
    void Sort()
    {
        std::sort(GetData(), GetData() + ArrayNum);
    }
    
    /**
     * Sorts the array using custom comparison
     */
    template<typename Compare>
    void Sort(Compare Comp)
    {
        std::sort(GetData(), GetData() + ArrayNum, Comp);
    }
    
    /**
     * Stable sorts the array using default comparison
     */
    void StableSort()
    {
        std::stable_sort(GetData(), GetData() + ArrayNum);
    }
    
    /**
     * Stable sorts the array using custom comparison
     */
    template<typename Compare>
    void StableSort(Compare Comp)
    {
        std::stable_sort(GetData(), GetData() + ArrayNum, Comp);
    }
    
    // ========================================================================
    // Comparison
    // ========================================================================
    
    /**
     * Equality comparison
     */
    bool operator==(const TArray& Other) const
    {
        if (ArrayNum != Other.ArrayNum)
        {
            return false;
        }
        return CompareItems(GetData(), Other.GetData(), ArrayNum);
    }
    
    /**
     * Inequality comparison
     */
    bool operator!=(const TArray& Other) const
    {
        return !(*this == Other);
    }
    
    // ========================================================================
    // Iterators
    // ========================================================================
    
    using Iterator = TIndexedContainerIterator<TArray, ElementType, SizeType>;
    using ConstIterator = TIndexedContainerIterator<const TArray, const ElementType, SizeType>;
    
    Iterator CreateIterator()
    {
        return Iterator(*this);
    }
    
    ConstIterator CreateConstIterator() const
    {
        return ConstIterator(*this);
    }
    
    // Range-based for loop support
    FORCEINLINE ElementType* begin() { return GetData(); }
    FORCEINLINE const ElementType* begin() const { return GetData(); }
    FORCEINLINE ElementType* end() { return GetData() + ArrayNum; }
    FORCEINLINE const ElementType* end() const { return GetData() + ArrayNum; }
    
    // ========================================================================
    // Hashing
    // ========================================================================
    
    friend uint32 GetTypeHash(const TArray& Array)
    {
        uint32 Hash = GetTypeHash(Array.Num());
        for (SizeType i = 0; i < Array.Num(); ++i)
        {
            Hash = HashCombineFast(Hash, GetTypeHash(Array[i]));
        }
        return Hash;
    }
    
private:
    // ========================================================================
    // Internal Helpers
    // ========================================================================
    
    void RangeCheck(SizeType Index) const
    {
        // In debug builds, check bounds
#if !defined(NDEBUG)
        if (Index < 0 || Index >= ArrayNum)
        {
            // Bounds check failed
        }
#endif
    }
    
    void CopyFrom(const TArray& Other)
    {
        if (ArrayNum > 0)
        {
            DestructItems(GetData(), ArrayNum);
        }
        
        ArrayNum = Other.ArrayNum;
        
        if (ArrayNum > ArrayMax)
        {
            ResizeTo(ArrayNum);
        }
        
        if (ArrayNum > 0)
        {
            CopyConstructItems(GetData(), Other.GetData(), ArrayNum);
        }
    }
    
    void ResizeGrow(SizeType OldNum)
    {
        ArrayMax = AllocatorInstance.CalculateSlackGrow(ArrayNum, ArrayMax, sizeof(ElementType), alignof(ElementType));
        AllocatorInstance.ResizeAllocation(OldNum, ArrayMax, sizeof(ElementType), alignof(ElementType));
    }
    
    void ResizeShrink()
    {
        const SizeType NewMax = AllocatorInstance.CalculateSlackShrink(ArrayNum, ArrayMax, sizeof(ElementType), alignof(ElementType));
        if (NewMax != ArrayMax)
        {
            ArrayMax = NewMax;
            AllocatorInstance.ResizeAllocation(ArrayNum, ArrayMax, sizeof(ElementType), alignof(ElementType));
        }
    }
    
    void ResizeTo(SizeType NewMax)
    {
        if (NewMax != ArrayMax)
        {
            ArrayMax = NewMax;
            AllocatorInstance.ResizeAllocation(ArrayNum, ArrayMax, sizeof(ElementType), alignof(ElementType));
        }
    }
    
private:
    SizeType ArrayNum;
    SizeType ArrayMax;
    ElementAllocatorType AllocatorInstance;
};

// ============================================================================
// Free Functions
// ============================================================================

/**
 * GetData - Returns pointer to array data
 */
template<typename ElementType, typename Allocator>
FORCEINLINE ElementType* GetData(TArray<ElementType, Allocator>& Array)
{
    return Array.GetData();
}

template<typename ElementType, typename Allocator>
FORCEINLINE const ElementType* GetData(const TArray<ElementType, Allocator>& Array)
{
    return Array.GetData();
}

/**
 * GetNum - Returns number of elements
 */
template<typename ElementType, typename Allocator>
FORCEINLINE typename TArray<ElementType, Allocator>::SizeType GetNum(const TArray<ElementType, Allocator>& Array)
{
    return Array.Num();
}

} // namespace MonsterEngine
