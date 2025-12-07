// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file Set.h
 * @brief Hash set container following UE5 TSet patterns
 * 
 * TSet is a hash-based set with the following features:
 * - O(1) average case for add, remove, and find operations
 * - Configurable key functions for custom hashing/comparison
 * - Uses TSparseArray for stable element indices
 */

#include "Core/CoreTypes.h"
#include "Core/Templates/TypeTraits.h"
#include "Core/Templates/TypeHash.h"
#include "ContainerAllocationPolicies.h"
#include "ContainerFwd.h"
#include "SparseArray.h"

#include <new>

namespace MonsterEngine
{

// ============================================================================
// FSetElementId
// ============================================================================

/**
 * Identifier for an element in a set
 * Wraps an index that remains valid even after other elements are removed
 */
class FSetElementId
{
public:
    /**
     * Default constructor - creates invalid ID
     */
    FORCEINLINE FSetElementId()
        : Index(INDEX_NONE_VALUE)
    {
    }
    
    /**
     * Returns true if this is a valid ID
     */
    FORCEINLINE bool IsValidId() const
    {
        return Index != INDEX_NONE_VALUE;
    }
    
    /**
     * Returns the index as an integer
     */
    FORCEINLINE int32 AsInteger() const
    {
        return Index;
    }
    
    /**
     * Creates an ID from an integer
     */
    FORCEINLINE static FSetElementId FromInteger(int32 Integer)
    {
        return FSetElementId(Integer);
    }
    
    /**
     * Comparison operators
     */
    FORCEINLINE friend bool operator==(const FSetElementId& A, const FSetElementId& B)
    {
        return A.Index == B.Index;
    }
    
    FORCEINLINE friend bool operator!=(const FSetElementId& A, const FSetElementId& B)
    {
        return A.Index != B.Index;
    }
    
private:
    friend class FSetElementIdPrivate;
    
    template<typename, typename, typename>
    friend class TSet;
    
    FORCEINLINE explicit FSetElementId(int32 InIndex)
        : Index(InIndex)
    {
    }
    
    int32 Index;
};

// ============================================================================
// BaseKeyFuncs
// ============================================================================

/**
 * Base class for key functions
 * Provides common type definitions and settings
 */
template<typename ElementType, typename InKeyType, bool bInAllowDuplicateKeys = false>
struct BaseKeyFuncs
{
    using KeyType = InKeyType;
    using KeyInitType = typename TCallTraits<InKeyType>::ParamType;
    using ElementInitType = typename TCallTraits<ElementType>::ParamType;
    
    enum { bAllowDuplicateKeys = bInAllowDuplicateKeys };
};

// ============================================================================
// DefaultKeyFuncs
// ============================================================================

/**
 * Default key functions - uses element as its own key
 */
template<typename ElementType, bool bInAllowDuplicateKeys = false>
struct DefaultKeyFuncs : BaseKeyFuncs<ElementType, ElementType, bInAllowDuplicateKeys>
{
    // Use ConstInitType (value for small types, const ref for large types) instead of pointer
    using KeyInitType = typename TTypeTraits<ElementType>::ConstInitType;
    using ElementInitType = typename TCallTraits<ElementType>::ParamType;
    
    /**
     * Returns the key for an element
     */
    static FORCEINLINE KeyInitType GetSetKey(ElementInitType Element)
    {
        return Element;
    }
    
    /**
     * Returns true if keys match
     */
    static FORCEINLINE bool Matches(KeyInitType A, KeyInitType B)
    {
        return A == B;
    }
    
    /**
     * Returns hash for a key
     */
    static FORCEINLINE uint32 GetKeyHash(KeyInitType Key)
    {
        return GetTypeHash(Key);
    }
};

// ============================================================================
// TSetElement
// ============================================================================

/**
 * Element stored in a set
 * Contains the value and a hash link for the hash table
 */
template<typename InElementType>
class TSetElement
{
public:
    using ElementType = InElementType;
    
    /**
     * Default constructor
     */
    TSetElement() = default;
    
    /**
     * Copy constructor
     */
    TSetElement(const TSetElement&) = default;
    
    /**
     * Move constructor
     */
    TSetElement(TSetElement&&) = default;
    
    /**
     * Value constructor (variadic - for in-place construction)
     * SFINAE excludes TSetElement to avoid ambiguity with copy/move constructors
     */
    template<typename... ArgsType,
             typename = std::enable_if_t<sizeof...(ArgsType) != 1 || 
                                         !std::is_same_v<std::decay_t<std::tuple_element_t<0, std::tuple<ArgsType...>>>, TSetElement>>>
    explicit TSetElement(ArgsType&&... Args)
        : Value(std::forward<ArgsType>(Args)...)
        , HashNextId()
        , HashIndex(INDEX_NONE_VALUE)
    {
    }
    
    /**
     * Copy assignment
     */
    TSetElement& operator=(const TSetElement&) = default;
    
    /**
     * Move assignment
     */
    TSetElement& operator=(TSetElement&&) = default;
    
    /**
     * The element value
     */
    ElementType Value;
    
    /**
     * Next element in hash bucket chain
     */
    mutable FSetElementId HashNextId;
    
    /**
     * Hash bucket index
     */
    mutable int32 HashIndex;
};

// ============================================================================
// TSet
// ============================================================================

/**
 * A set of unique elements
 * 
 * Uses a hash table for O(1) operations and TSparseArray for stable indices.
 * 
 * @tparam InElementType The type of elements
 * @tparam KeyFuncs Functions for getting keys and hashing (defaults to using element as key)
 * @tparam Allocator The allocator policy
 */
template<typename InElementType, typename KeyFuncs, typename Allocator>
class TSet
{
public:
    using ElementType = InElementType;
    using SizeType = int32;
    
private:
    // Use DefaultKeyFuncs if KeyFuncs is void
    using ActualKeyFuncs = TConditional_T<
        TIsSame<KeyFuncs, void>::Value,
        DefaultKeyFuncs<ElementType>,
        KeyFuncs
    >;
    
    using SetElementType = TSetElement<ElementType>;
    using ElementArrayType = TSparseArray<SetElementType, FDefaultSparseArrayAllocator>;
    using HashType = TArray<FSetElementId, typename Allocator::template ForElementType<FSetElementId>>;
    
public:
    // ========================================================================
    // Constructors and Destructor
    // ========================================================================
    
    /**
     * Default constructor
     */
    TSet()
        : HashSize(0)
    {
    }
    
    /**
     * Constructor from initializer list
     */
    TSet(std::initializer_list<ElementType> InitList)
        : HashSize(0)
    {
        Reserve(static_cast<SizeType>(InitList.size()));
        for (const ElementType& Element : InitList)
        {
            Add(Element);
        }
    }
    
    /**
     * Copy constructor
     */
    TSet(const TSet& Other)
        : Elements(Other.Elements)
        , Hash(Other.Hash)
        , HashSize(Other.HashSize)
    {
    }
    
    /**
     * Move constructor
     */
    TSet(TSet&& Other) noexcept
        : Elements(std::move(Other.Elements))
        , Hash(std::move(Other.Hash))
        , HashSize(Other.HashSize)
    {
        Other.HashSize = 0;
    }
    
    /**
     * Destructor
     */
    ~TSet() = default;
    
    // ========================================================================
    // Assignment Operators
    // ========================================================================
    
    /**
     * Copy assignment
     */
    TSet& operator=(const TSet& Other)
    {
        if (this != &Other)
        {
            Elements = Other.Elements;
            Hash = Other.Hash;
            HashSize = Other.HashSize;
        }
        return *this;
    }
    
    /**
     * Move assignment
     */
    TSet& operator=(TSet&& Other) noexcept
    {
        if (this != &Other)
        {
            Elements = std::move(Other.Elements);
            Hash = std::move(Other.Hash);
            HashSize = Other.HashSize;
            Other.HashSize = 0;
        }
        return *this;
    }
    
    /**
     * Assignment from initializer list
     */
    TSet& operator=(std::initializer_list<ElementType> InitList)
    {
        Empty(static_cast<SizeType>(InitList.size()));
        for (const ElementType& Element : InitList)
        {
            Add(Element);
        }
        return *this;
    }
    
    // ========================================================================
    // Size and Capacity
    // ========================================================================
    
    /**
     * Returns number of elements
     */
    FORCEINLINE SizeType Num() const
    {
        return Elements.Num();
    }
    
    /**
     * Returns true if empty
     */
    FORCEINLINE bool IsEmpty() const
    {
        return Num() == 0;
    }
    
    // ========================================================================
    // Adding Elements
    // ========================================================================
    
    /**
     * Adds an element to the set
     * @return ID of the added element (or existing element if duplicate)
     */
    FSetElementId Add(const ElementType& InElement, bool* bIsAlreadyInSetPtr = nullptr)
    {
        return EmplaceImpl(bIsAlreadyInSetPtr, InElement);
    }
    
    /**
     * Adds an element to the set (move)
     * @return ID of the added element (or existing element if duplicate)
     */
    FSetElementId Add(ElementType&& InElement, bool* bIsAlreadyInSetPtr = nullptr)
    {
        return EmplaceImpl(bIsAlreadyInSetPtr, std::move(InElement));
    }
    
    /**
     * Constructs an element in place
     * @return ID of the added element (or existing element if duplicate)
     */
    template<typename... ArgsType>
    FSetElementId Emplace(ArgsType&&... Args)
    {
        return EmplaceImpl(nullptr, std::forward<ArgsType>(Args)...);
    }
    
    // ========================================================================
    // Removing Elements
    // ========================================================================
    
    /**
     * Removes an element by value
     * @return Number of elements removed (0 or 1)
     */
    SizeType Remove(const ElementType& Element)
    {
        FSetElementId Id = FindId(Element);
        if (Id.IsValidId())
        {
            RemoveById(Id);
            return 1;
        }
        return 0;
    }
    
    /**
     * Removes an element by ID
     */
    void RemoveById(FSetElementId Id)
    {
        if (!Id.IsValidId())
        {
            return;
        }
        
        const SetElementType& Element = Elements[Id.Index];
        
        // Remove from hash chain
        RemoveFromHash(Id);
        
        // Remove from elements array
        Elements.RemoveAt(Id.Index);
        
        // Check if we need to shrink hash
        ConditionalRehash();
    }
    
    // ========================================================================
    // Finding Elements
    // ========================================================================
    
    /**
     * Finds an element by value
     * @return Pointer to element, or nullptr if not found
     */
    ElementType* Find(const ElementType& Element)
    {
        FSetElementId Id = FindId(Element);
        if (Id.IsValidId())
        {
            return &Elements[Id.Index].Value;
        }
        return nullptr;
    }
    
    /**
     * Finds an element by value (const)
     */
    const ElementType* Find(const ElementType& Element) const
    {
        return const_cast<TSet*>(this)->Find(Element);
    }
    
    /**
     * Finds the ID of an element
     * @return ID of element, or invalid ID if not found
     */
    FSetElementId FindId(const ElementType& Element) const
    {
        if (HashSize == 0)
        {
            return FSetElementId();
        }
        
        const uint32 KeyHash = ActualKeyFuncs::GetKeyHash(ActualKeyFuncs::GetSetKey(Element));
        const SizeType HashIndex = KeyHash & (HashSize - 1);
        
        FSetElementId Id = Hash[HashIndex];
        while (Id.IsValidId())
        {
            const SetElementType& SetElement = Elements[Id.Index];
            if (ActualKeyFuncs::Matches(ActualKeyFuncs::GetSetKey(SetElement.Value), ActualKeyFuncs::GetSetKey(Element)))
            {
                return Id;
            }
            Id = SetElement.HashNextId;
        }
        
        return FSetElementId();
    }
    
    /**
     * Checks if set contains an element
     */
    bool Contains(const ElementType& Element) const
    {
        return FindId(Element).IsValidId();
    }
    
    // ========================================================================
    // Memory Management
    // ========================================================================
    
    /**
     * Empties the set
     */
    void Empty(SizeType ExpectedNumElements = 0)
    {
        Elements.Empty(ExpectedNumElements);
        
        if (ExpectedNumElements > 0)
        {
            HashSize = GetHashSize(ExpectedNumElements);
            // CRITICAL: Initialize hash buckets to invalid ID, not zero!
            Hash.SetNum(HashSize);
            for (SizeType i = 0; i < HashSize; ++i)
            {
                Hash[i] = FSetElementId(); // INDEX_NONE_VALUE
            }
        }
        else
        {
            HashSize = 0;
            Hash.Empty();
        }
    }
    
    /**
     * Resets the set without deallocating
     */
    void Reset()
    {
        Elements.Reset();
        Hash.Reset();
        HashSize = 0;
    }
    
    /**
     * Reserves capacity
     */
    void Reserve(SizeType ExpectedNumElements)
    {
        if (ExpectedNumElements > Elements.Num())
        {
            Elements.Reserve(ExpectedNumElements);
            
            const SizeType NewHashSize = GetHashSize(ExpectedNumElements);
            if (NewHashSize > HashSize)
            {
                Rehash(NewHashSize);
            }
        }
    }
    
    /**
     * Shrinks the set to fit its contents
     */
    void Shrink()
    {
        Elements.Shrink();
        
        const SizeType NewHashSize = GetHashSize(Num());
        if (NewHashSize < HashSize)
        {
            Rehash(NewHashSize);
        }
    }
    
    /**
     * Compacts the set, removing gaps in element storage
     */
    void Compact()
    {
        Elements.Compact();
        Rehash(HashSize);
    }
    
    // ========================================================================
    // Element Access
    // ========================================================================
    
    /**
     * Gets element by ID
     */
    FORCEINLINE ElementType& operator[](FSetElementId Id)
    {
        return Elements[Id.Index].Value;
    }
    
    /**
     * Gets element by ID (const)
     */
    FORCEINLINE const ElementType& operator[](FSetElementId Id) const
    {
        return Elements[Id.Index].Value;
    }
    
    // ========================================================================
    // Iteration
    // ========================================================================
    
    /**
     * Iterator for set
     */
    class TIterator
    {
    public:
        TIterator(TSet& InSet)
            : Set(InSet)
            , ElementIt(InSet.Elements.CreateIterator())
        {
        }
        
        TIterator& operator++()
        {
            ++ElementIt;
            return *this;
        }
        
        FORCEINLINE explicit operator bool() const
        {
            return static_cast<bool>(ElementIt);
        }
        
        FORCEINLINE ElementType& operator*() const
        {
            return (*ElementIt).Value;
        }
        
        FORCEINLINE ElementType* operator->() const
        {
            return &(*ElementIt).Value;
        }
        
        FORCEINLINE FSetElementId GetId() const
        {
            return FSetElementId(ElementIt.GetIndex());
        }
        
        void RemoveCurrent()
        {
            Set.RemoveById(GetId());
        }
        
    private:
        TSet& Set;
        typename ElementArrayType::TIterator ElementIt;
    };
    
    /**
     * Const iterator for set
     */
    class TConstIterator
    {
    public:
        TConstIterator(const TSet& InSet)
            : ElementIt(InSet.Elements.CreateConstIterator())
        {
        }
        
        TConstIterator& operator++()
        {
            ++ElementIt;
            return *this;
        }
        
        FORCEINLINE explicit operator bool() const
        {
            return static_cast<bool>(ElementIt);
        }
        
        FORCEINLINE const ElementType& operator*() const
        {
            return (*ElementIt).Value;
        }
        
        FORCEINLINE const ElementType* operator->() const
        {
            return &(*ElementIt).Value;
        }
        
        FORCEINLINE FSetElementId GetId() const
        {
            return FSetElementId(ElementIt.GetIndex());
        }
        
    private:
        typename ElementArrayType::TConstIterator ElementIt;
    };
    
    TIterator CreateIterator()
    {
        return TIterator(*this);
    }
    
    TConstIterator CreateConstIterator() const
    {
        return TConstIterator(*this);
    }
    
    // Range-based for loop support
    class FElementIterator
    {
    public:
        FElementIterator(TSet* InSet, typename ElementArrayType::FElementIterator InIt)
            : It(InIt)
        {
        }
        
        FElementIterator& operator++()
        {
            ++It;
            return *this;
        }
        
        bool operator!=(const FElementIterator& Other) const
        {
            return It != Other.It;
        }
        
        ElementType& operator*() const
        {
            return (*It).Value;
        }
        
    private:
        typename ElementArrayType::FElementIterator It;
    };
    
    class FConstElementIterator
    {
    public:
        FConstElementIterator(const TSet* InSet, typename ElementArrayType::FConstElementIterator InIt)
            : It(InIt)
        {
        }
        
        FConstElementIterator& operator++()
        {
            ++It;
            return *this;
        }
        
        bool operator!=(const FConstElementIterator& Other) const
        {
            return It != Other.It;
        }
        
        const ElementType& operator*() const
        {
            return (*It).Value;
        }
        
    private:
        typename ElementArrayType::FConstElementIterator It;
    };
    
    FElementIterator begin() { return FElementIterator(this, Elements.begin()); }
    FElementIterator end() { return FElementIterator(this, Elements.end()); }
    FConstElementIterator begin() const { return FConstElementIterator(this, Elements.begin()); }
    FConstElementIterator end() const { return FConstElementIterator(this, Elements.end()); }
    
    // ========================================================================
    // Set Operations
    // ========================================================================
    
    /**
     * Returns intersection of this set with another
     */
    TSet Intersect(const TSet& Other) const
    {
        TSet Result;
        for (const ElementType& Element : *this)
        {
            if (Other.Contains(Element))
            {
                Result.Add(Element);
            }
        }
        return Result;
    }
    
    /**
     * Returns union of this set with another
     */
    TSet Union(const TSet& Other) const
    {
        TSet Result = *this;
        for (const ElementType& Element : Other)
        {
            Result.Add(Element);
        }
        return Result;
    }
    
    /**
     * Returns difference of this set with another (elements in this but not in other)
     */
    TSet Difference(const TSet& Other) const
    {
        TSet Result;
        for (const ElementType& Element : *this)
        {
            if (!Other.Contains(Element))
            {
                Result.Add(Element);
            }
        }
        return Result;
    }
    
    // ========================================================================
    // Comparison
    // ========================================================================
    
    /**
     * Equality comparison
     */
    bool operator==(const TSet& Other) const
    {
        if (Num() != Other.Num())
        {
            return false;
        }
        
        for (const ElementType& Element : *this)
        {
            if (!Other.Contains(Element))
            {
                return false;
            }
        }
        
        return true;
    }
    
    /**
     * Inequality comparison
     */
    bool operator!=(const TSet& Other) const
    {
        return !(*this == Other);
    }
    
private:
    // ========================================================================
    // Internal Helpers
    // ========================================================================
    
    template<typename... ArgsType>
    FSetElementId EmplaceImpl(bool* bIsAlreadyInSetPtr, ArgsType&&... Args)
    {
        // Construct element to get its key
        SetElementType NewElement(std::forward<ArgsType>(Args)...);
        
        // Check for existing element
        if (!ActualKeyFuncs::bAllowDuplicateKeys)
        {
            FSetElementId ExistingId = FindId(NewElement.Value);
            if (ExistingId.IsValidId())
            {
                if (bIsAlreadyInSetPtr)
                {
                    *bIsAlreadyInSetPtr = true;
                }
                return ExistingId;
            }
        }
        
        if (bIsAlreadyInSetPtr)
        {
            *bIsAlreadyInSetPtr = false;
        }
        
        // Check if we need to grow hash
        if (ShouldRehash())
        {
            Rehash(GetHashSize(Num() + 1));
        }
        
        // Add to elements array
        FSparseArrayAllocationInfo Allocation = Elements.AddUninitialized();
        new (Allocation.Pointer) SetElementType(std::move(NewElement));
        
        FSetElementId NewId = FSetElementId(Allocation.Index);
        
        // Add to hash
        LinkElement(NewId);
        
        return NewId;
    }
    
    void LinkElement(FSetElementId Id)
    {
        SetElementType& Element = Elements[Id.Index];
        
        const uint32 KeyHash = ActualKeyFuncs::GetKeyHash(ActualKeyFuncs::GetSetKey(Element.Value));
        const SizeType HashIndex = KeyHash & (HashSize - 1);
        
        Element.HashIndex = HashIndex;
        Element.HashNextId = Hash[HashIndex];
        Hash[HashIndex] = Id;
    }
    
    void RemoveFromHash(FSetElementId Id)
    {
        const SetElementType& Element = Elements[Id.Index];
        const SizeType HashIndex = Element.HashIndex;
        
        if (HashIndex == INDEX_NONE_VALUE)
        {
            return;
        }
        
        // Find and remove from hash chain
        FSetElementId* PrevLink = &Hash[HashIndex];
        while (PrevLink->IsValidId())
        {
            if (*PrevLink == Id)
            {
                *PrevLink = Element.HashNextId;
                return;
            }
            PrevLink = &Elements[PrevLink->Index].HashNextId;
        }
    }
    
    bool ShouldRehash() const
    {
        // Rehash when load factor exceeds 0.75
        return HashSize == 0 || Num() * 4 >= HashSize * 3;
    }
    
    void ConditionalRehash()
    {
        // Shrink hash when load factor drops below 0.25
        if (HashSize > 0 && Num() * 4 < HashSize)
        {
            const SizeType NewHashSize = GetHashSize(Num());
            if (NewHashSize < HashSize)
            {
                Rehash(NewHashSize);
            }
        }
    }
    
    void Rehash(SizeType NewHashSize)
    {
        if (NewHashSize == 0)
        {
            Hash.Empty();
            HashSize = 0;
            return;
        }
        
        // Ensure power of 2
        NewHashSize = RoundUpToPowerOfTwo(NewHashSize);
        
        HashSize = NewHashSize;
        
        // CRITICAL: Initialize hash buckets to invalid ID (INDEX_NONE_VALUE), not zero!
        // Zero is a valid index, so using SetNumZeroed would cause infinite loops in FindId
        Hash.SetNum(HashSize);
        for (SizeType i = 0; i < HashSize; ++i)
        {
            Hash[i] = FSetElementId(); // Default constructor sets Index to INDEX_NONE_VALUE
        }
        
        // Re-link all elements
        for (auto It = Elements.CreateIterator(); It; ++It)
        {
            LinkElement(FSetElementId(It.GetIndex()));
        }
    }
    
    static SizeType GetHashSize(SizeType NumElements)
    {
        if (NumElements == 0)
        {
            return 0;
        }
        
        // Target load factor of 0.5
        return RoundUpToPowerOfTwo(NumElements * 2);
    }
    
    static SizeType RoundUpToPowerOfTwo(SizeType Value)
    {
        if (Value <= 0)
        {
            return 0;
        }
        
        --Value;
        Value |= Value >> 1;
        Value |= Value >> 2;
        Value |= Value >> 4;
        Value |= Value >> 8;
        Value |= Value >> 16;
        ++Value;
        
        return Value;
    }
    
private:
    ElementArrayType Elements;
    TArray<FSetElementId> Hash;
    SizeType HashSize;
};

} // namespace MonsterEngine
