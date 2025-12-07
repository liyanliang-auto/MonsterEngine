// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file Map.h
 * @brief Hash map container following UE5 TMap patterns
 * 
 * TMap is a hash-based associative container with the following features:
 * - O(1) average case for add, remove, and find operations
 * - Based on TSet with key-value pairs
 * - Configurable key functions for custom hashing/comparison
 */

#include "Core/CoreTypes.h"
#include "Core/Templates/TypeTraits.h"
#include "Core/Templates/TypeHash.h"
#include "ContainerAllocationPolicies.h"
#include "ContainerFwd.h"
#include "Set.h"

#include <utility>

namespace MonsterEngine
{

// ============================================================================
// TPair
// ============================================================================

/**
 * A key-value pair
 */
template<typename KeyType, typename ValueType>
struct TPair
{
    KeyType Key;
    ValueType Value;
    
    /**
     * Default constructor
     */
    TPair() = default;
    
    /**
     * Constructor from key and value
     */
    template<typename KeyArg, typename ValueArg>
    TPair(KeyArg&& InKey, ValueArg&& InValue)
        : Key(std::forward<KeyArg>(InKey))
        , Value(std::forward<ValueArg>(InValue))
    {
    }
    
    /**
     * Constructor from key only (value is default constructed)
     */
    template<typename KeyArg>
    explicit TPair(KeyArg&& InKey)
        : Key(std::forward<KeyArg>(InKey))
        , Value()
    {
    }
    
    /**
     * Copy constructor
     */
    TPair(const TPair&) = default;
    
    /**
     * Move constructor
     */
    TPair(TPair&&) = default;
    
    /**
     * Copy assignment
     */
    TPair& operator=(const TPair&) = default;
    
    /**
     * Move assignment
     */
    TPair& operator=(TPair&&) = default;
    
    /**
     * Equality comparison (compares both key and value)
     */
    bool operator==(const TPair& Other) const
    {
        return Key == Other.Key && Value == Other.Value;
    }
    
    /**
     * Inequality comparison
     */
    bool operator!=(const TPair& Other) const
    {
        return !(*this == Other);
    }
};

/**
 * Hash function for TPair
 */
template<typename KeyType, typename ValueType>
FORCEINLINE uint32 GetTypeHash(const TPair<KeyType, ValueType>& Pair)
{
    return HashCombineFast(GetTypeHash(Pair.Key), GetTypeHash(Pair.Value));
}

// ============================================================================
// TDefaultMapKeyFuncs
// ============================================================================

/**
 * Default key functions for TMap
 * Uses the Key member of the pair for hashing and comparison
 */
template<typename KeyType, typename ValueType, bool bInAllowDuplicateKeys>
struct TDefaultMapKeyFuncs : BaseKeyFuncs<TPair<KeyType, ValueType>, KeyType, bInAllowDuplicateKeys>
{
    using KeyInitType = typename TTypeTraits<KeyType>::ConstPointerType;
    using ElementInitType = const TPair<KeyType, ValueType>&;
    
    /**
     * Returns the key for a pair
     */
    static FORCEINLINE KeyInitType GetSetKey(ElementInitType Element)
    {
        return Element.Key;
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
// TMapBase
// ============================================================================

/**
 * Base class for TMap and TMultiMap
 * 
 * @tparam KeyType The key type
 * @tparam ValueType The value type
 * @tparam SetAllocator The allocator for the underlying set
 * @tparam KeyFuncs Functions for getting keys and hashing
 */
template<typename KeyType, typename ValueType, typename SetAllocator, typename KeyFuncs>
class TMapBase
{
    template<typename, typename, typename, typename>
    friend class TMapBase;
    
public:
    using ElementType = TPair<KeyType, ValueType>;
    using SizeType = int32;
    
protected:
    using SetType = TSet<ElementType, KeyFuncs, SetAllocator>;
    
public:
    // ========================================================================
    // Constructors and Destructor
    // ========================================================================
    
    /**
     * Default constructor
     */
    TMapBase() = default;
    
    /**
     * Constructor from initializer list
     */
    TMapBase(std::initializer_list<ElementType> InitList)
    {
        Reserve(static_cast<SizeType>(InitList.size()));
        for (const ElementType& Pair : InitList)
        {
            Add(Pair.Key, Pair.Value);
        }
    }
    
    /**
     * Copy constructor
     */
    TMapBase(const TMapBase&) = default;
    
    /**
     * Move constructor
     */
    TMapBase(TMapBase&&) = default;
    
    /**
     * Destructor
     */
    ~TMapBase() = default;
    
    // ========================================================================
    // Assignment Operators
    // ========================================================================
    
    /**
     * Copy assignment
     */
    TMapBase& operator=(const TMapBase&) = default;
    
    /**
     * Move assignment
     */
    TMapBase& operator=(TMapBase&&) = default;
    
    /**
     * Assignment from initializer list
     */
    TMapBase& operator=(std::initializer_list<ElementType> InitList)
    {
        Empty(static_cast<SizeType>(InitList.size()));
        for (const ElementType& Pair : InitList)
        {
            Add(Pair.Key, Pair.Value);
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
        return Pairs.Num();
    }
    
    /**
     * Returns true if empty
     */
    FORCEINLINE bool IsEmpty() const
    {
        return Pairs.IsEmpty();
    }
    
    // ========================================================================
    // Adding Elements
    // ========================================================================
    
    /**
     * Adds a key-value pair
     * @return Reference to the value
     */
    ValueType& Add(const KeyType& InKey, const ValueType& InValue)
    {
        return EmplaceImpl(InKey, InValue);
    }
    
    /**
     * Adds a key-value pair (move value)
     * @return Reference to the value
     */
    ValueType& Add(const KeyType& InKey, ValueType&& InValue)
    {
        return EmplaceImpl(InKey, std::move(InValue));
    }
    
    /**
     * Adds a key with default-constructed value
     * @return Reference to the value
     */
    ValueType& Add(const KeyType& InKey)
    {
        return EmplaceImpl(InKey);
    }
    
    /**
     * Adds a key-value pair (move key)
     * @return Reference to the value
     */
    ValueType& Add(KeyType&& InKey, const ValueType& InValue)
    {
        return EmplaceImpl(std::move(InKey), InValue);
    }
    
    /**
     * Adds a key-value pair (move both)
     * @return Reference to the value
     */
    ValueType& Add(KeyType&& InKey, ValueType&& InValue)
    {
        return EmplaceImpl(std::move(InKey), std::move(InValue));
    }
    
    /**
     * Adds a key with default-constructed value (move key)
     * @return Reference to the value
     */
    ValueType& Add(KeyType&& InKey)
    {
        return EmplaceImpl(std::move(InKey));
    }
    
    /**
     * Constructs a key-value pair in place
     * @return Reference to the value
     */
    template<typename... ArgsType>
    ValueType& Emplace(const KeyType& InKey, ArgsType&&... Args)
    {
        return EmplaceImpl(InKey, std::forward<ArgsType>(Args)...);
    }
    
    /**
     * Constructs a key-value pair in place (move key)
     * @return Reference to the value
     */
    template<typename... ArgsType>
    ValueType& Emplace(KeyType&& InKey, ArgsType&&... Args)
    {
        return EmplaceImpl(std::move(InKey), std::forward<ArgsType>(Args)...);
    }
    
    /**
     * Finds or adds a key-value pair
     * @return Reference to the value (existing or new)
     */
    ValueType& FindOrAdd(const KeyType& InKey)
    {
        ValueType* ExistingValue = Find(InKey);
        if (ExistingValue)
        {
            return *ExistingValue;
        }
        return Add(InKey);
    }
    
    /**
     * Finds or adds a key-value pair (move key)
     * @return Reference to the value (existing or new)
     */
    ValueType& FindOrAdd(KeyType&& InKey)
    {
        ValueType* ExistingValue = Find(InKey);
        if (ExistingValue)
        {
            return *ExistingValue;
        }
        return Add(std::move(InKey));
    }
    
    // ========================================================================
    // Removing Elements
    // ========================================================================
    
    /**
     * Removes a key-value pair by key
     * @return Number of elements removed (0 or 1 for TMap, 0+ for TMultiMap)
     */
    SizeType Remove(const KeyType& InKey)
    {
        // Find the element
        FSetElementId Id = Pairs.FindId(ElementType(InKey));
        if (Id.IsValidId())
        {
            Pairs.RemoveById(Id);
            return 1;
        }
        return 0;
    }
    
    // ========================================================================
    // Finding Elements
    // ========================================================================
    
    /**
     * Finds a value by key
     * @return Pointer to value, or nullptr if not found
     */
    ValueType* Find(const KeyType& InKey)
    {
        ElementType* Pair = Pairs.Find(ElementType(InKey));
        if (Pair)
        {
            return &Pair->Value;
        }
        return nullptr;
    }
    
    /**
     * Finds a value by key (const)
     */
    const ValueType* Find(const KeyType& InKey) const
    {
        return const_cast<TMapBase*>(this)->Find(InKey);
    }
    
    /**
     * Finds a value by key, returns reference (asserts if not found)
     */
    ValueType& FindChecked(const KeyType& InKey)
    {
        ValueType* Value = Find(InKey);
        // Assert that value exists
        return *Value;
    }
    
    /**
     * Finds a value by key, returns reference (const, asserts if not found)
     */
    const ValueType& FindChecked(const KeyType& InKey) const
    {
        return const_cast<TMapBase*>(this)->FindChecked(InKey);
    }
    
    /**
     * Finds a value by key, returns pointer to key if found
     */
    const KeyType* FindKey(const ValueType& InValue) const
    {
        for (const ElementType& Pair : Pairs)
        {
            if (Pair.Value == InValue)
            {
                return &Pair.Key;
            }
        }
        return nullptr;
    }
    
    /**
     * Checks if map contains a key
     */
    bool Contains(const KeyType& InKey) const
    {
        return Find(InKey) != nullptr;
    }
    
    // ========================================================================
    // Bracket Operator
    // ========================================================================
    
    /**
     * Bracket operator - finds or adds a key
     * @return Reference to the value
     */
    ValueType& operator[](const KeyType& InKey)
    {
        return FindOrAdd(InKey);
    }
    
    /**
     * Bracket operator - finds or adds a key (move)
     * @return Reference to the value
     */
    ValueType& operator[](KeyType&& InKey)
    {
        return FindOrAdd(std::move(InKey));
    }
    
    // ========================================================================
    // Memory Management
    // ========================================================================
    
    /**
     * Empties the map
     */
    void Empty(SizeType ExpectedNumElements = 0)
    {
        Pairs.Empty(ExpectedNumElements);
    }
    
    /**
     * Resets the map without deallocating
     */
    void Reset()
    {
        Pairs.Reset();
    }
    
    /**
     * Reserves capacity
     */
    void Reserve(SizeType ExpectedNumElements)
    {
        Pairs.Reserve(ExpectedNumElements);
    }
    
    /**
     * Shrinks the map to fit its contents
     */
    void Shrink()
    {
        Pairs.Shrink();
    }
    
    /**
     * Compacts the map
     */
    void Compact()
    {
        Pairs.Compact();
    }
    
    // ========================================================================
    // Key/Value Arrays
    // ========================================================================
    
    /**
     * Generates an array of all keys
     */
    TArray<KeyType> GetKeys() const
    {
        TArray<KeyType> Result;
        Result.Reserve(Num());
        for (const ElementType& Pair : Pairs)
        {
            Result.Add(Pair.Key);
        }
        return Result;
    }
    
    /**
     * Appends all keys to an array
     */
    void GetKeys(TArray<KeyType>& OutKeys) const
    {
        OutKeys.Reserve(OutKeys.Num() + Num());
        for (const ElementType& Pair : Pairs)
        {
            OutKeys.Add(Pair.Key);
        }
    }
    
    /**
     * Generates an array of all values
     */
    TArray<ValueType> GetValues() const
    {
        TArray<ValueType> Result;
        Result.Reserve(Num());
        for (const ElementType& Pair : Pairs)
        {
            Result.Add(Pair.Value);
        }
        return Result;
    }
    
    // ========================================================================
    // Iteration
    // ========================================================================
    
    /**
     * Iterator for map
     */
    class TIterator
    {
    public:
        TIterator(TMapBase& InMap)
            : Map(InMap)
            , SetIt(InMap.Pairs.CreateIterator())
        {
        }
        
        TIterator& operator++()
        {
            ++SetIt;
            return *this;
        }
        
        FORCEINLINE explicit operator bool() const
        {
            return static_cast<bool>(SetIt);
        }
        
        FORCEINLINE ElementType& operator*() const
        {
            return *SetIt;
        }
        
        FORCEINLINE ElementType* operator->() const
        {
            return &(*SetIt);
        }
        
        FORCEINLINE const KeyType& Key() const
        {
            return (*SetIt).Key;
        }
        
        FORCEINLINE ValueType& Value() const
        {
            return (*SetIt).Value;
        }
        
        void RemoveCurrent()
        {
            SetIt.RemoveCurrent();
        }
        
    private:
        TMapBase& Map;
        typename SetType::TIterator SetIt;
    };
    
    /**
     * Const iterator for map
     */
    class TConstIterator
    {
    public:
        TConstIterator(const TMapBase& InMap)
            : SetIt(InMap.Pairs.CreateConstIterator())
        {
        }
        
        TConstIterator& operator++()
        {
            ++SetIt;
            return *this;
        }
        
        FORCEINLINE explicit operator bool() const
        {
            return static_cast<bool>(SetIt);
        }
        
        FORCEINLINE const ElementType& operator*() const
        {
            return *SetIt;
        }
        
        FORCEINLINE const ElementType* operator->() const
        {
            return &(*SetIt);
        }
        
        FORCEINLINE const KeyType& Key() const
        {
            return (*SetIt).Key;
        }
        
        FORCEINLINE const ValueType& Value() const
        {
            return (*SetIt).Value;
        }
        
    private:
        typename SetType::TConstIterator SetIt;
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
    auto begin() { return Pairs.begin(); }
    auto end() { return Pairs.end(); }
    auto begin() const { return Pairs.begin(); }
    auto end() const { return Pairs.end(); }
    
    // ========================================================================
    // Comparison
    // ========================================================================
    
    /**
     * Equality comparison
     */
    bool operator==(const TMapBase& Other) const
    {
        if (Num() != Other.Num())
        {
            return false;
        }
        
        for (const ElementType& Pair : Pairs)
        {
            const ValueType* OtherValue = Other.Find(Pair.Key);
            if (!OtherValue || !(*OtherValue == Pair.Value))
            {
                return false;
            }
        }
        
        return true;
    }
    
    /**
     * Inequality comparison
     */
    bool operator!=(const TMapBase& Other) const
    {
        return !(*this == Other);
    }
    
protected:
    // ========================================================================
    // Internal Helpers
    // ========================================================================
    
    template<typename KeyArg, typename... ValueArgs>
    ValueType& EmplaceImpl(KeyArg&& InKey, ValueArgs&&... Args)
    {
        // For TMap (no duplicates), check if key exists
        if (!KeyFuncs::bAllowDuplicateKeys)
        {
            ValueType* ExistingValue = Find(InKey);
            if (ExistingValue)
            {
                // Update existing value
                *ExistingValue = ValueType(std::forward<ValueArgs>(Args)...);
                return *ExistingValue;
            }
        }
        
        // Add new pair
        FSetElementId Id = Pairs.Emplace(std::forward<KeyArg>(InKey), std::forward<ValueArgs>(Args)...);
        return Pairs[Id].Value;
    }
    
protected:
    SetType Pairs;
};

// ============================================================================
// TMap
// ============================================================================

/**
 * A map from keys to values (no duplicate keys)
 * 
 * @tparam KeyType The key type
 * @tparam ValueType The value type
 * @tparam Allocator The allocator policy
 * @tparam KeyFuncs Functions for getting keys and hashing
 */
template<typename KeyType, typename ValueType, typename Allocator, typename KeyFuncs>
class TMap : public TMapBase<
    KeyType, 
    ValueType, 
    Allocator, 
    TConditional_T<
        TIsSame<KeyFuncs, void>::Value,
        TDefaultMapKeyFuncs<KeyType, ValueType, false>,
        KeyFuncs
    >
>
{
    using Super = TMapBase<
        KeyType, 
        ValueType, 
        Allocator, 
        TConditional_T<
            TIsSame<KeyFuncs, void>::Value,
            TDefaultMapKeyFuncs<KeyType, ValueType, false>,
            KeyFuncs
        >
    >;
    
public:
    using Super::Super;
    using Super::operator=;
};

// ============================================================================
// TMultiMap
// ============================================================================

/**
 * A map from keys to values (allows duplicate keys)
 * 
 * @tparam KeyType The key type
 * @tparam ValueType The value type
 * @tparam Allocator The allocator policy
 * @tparam KeyFuncs Functions for getting keys and hashing
 */
template<typename KeyType, typename ValueType, typename Allocator, typename KeyFuncs>
class TMultiMap : public TMapBase<
    KeyType, 
    ValueType, 
    Allocator, 
    TConditional_T<
        TIsSame<KeyFuncs, void>::Value,
        TDefaultMapKeyFuncs<KeyType, ValueType, true>,
        KeyFuncs
    >
>
{
    using Super = TMapBase<
        KeyType, 
        ValueType, 
        Allocator, 
        TConditional_T<
            TIsSame<KeyFuncs, void>::Value,
            TDefaultMapKeyFuncs<KeyType, ValueType, true>,
            KeyFuncs
        >
    >;
    
public:
    using Super::Super;
    using Super::operator=;
    
    /**
     * Finds all values for a key
     */
    void MultiFind(const KeyType& InKey, TArray<ValueType>& OutValues) const
    {
        for (const auto& Pair : this->Pairs)
        {
            if (Pair.Key == InKey)
            {
                OutValues.Add(Pair.Value);
            }
        }
    }
    
    /**
     * Removes all values for a key
     * @return Number of elements removed
     */
    int32 RemoveAll(const KeyType& InKey)
    {
        int32 NumRemoved = 0;
        
        for (auto It = this->Pairs.CreateIterator(); It; ++It)
        {
            if ((*It).Key == InKey)
            {
                It.RemoveCurrent();
                ++NumRemoved;
            }
        }
        
        return NumRemoved;
    }
};

} // namespace MonsterEngine
