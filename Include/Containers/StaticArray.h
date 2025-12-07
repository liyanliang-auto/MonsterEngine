// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file StaticArray.h
 * @brief Fixed-size array template following UE5 TStaticArray patterns
 * 
 * TStaticArray provides a fixed-size array with:
 * - Compile-time size
 * - No heap allocation
 * - STL-compatible interface
 * - Bounds checking in debug builds
 */

#include "Core/CoreTypes.h"
#include "Core/Templates/TypeTraits.h"
#include <array>
#include <algorithm>
#include <initializer_list>
#include <stdexcept>

namespace MonsterEngine
{

// ============================================================================
// TStaticArray - Fixed-size Array
// ============================================================================

/**
 * @brief Fixed-size array with compile-time size
 * 
 * Unlike TArray, TStaticArray has a fixed size determined at compile time.
 * All storage is inline (no heap allocation).
 * 
 * @tparam T The element type
 * @tparam N The number of elements
 */
template<typename T, uint32 N>
class TStaticArray
{
public:
    using ElementType = T;
    using SizeType = uint32;
    using Iterator = T*;
    using ConstIterator = const T*;

    static constexpr SizeType ArraySize = N;

public:
    // ========================================================================
    // Constructors
    // ========================================================================

    /** Default constructor - default initializes all elements */
    TStaticArray()
    {
        for (SizeType i = 0; i < N; ++i)
        {
            new (&Elements[i]) T();
        }
    }

    /** Fill constructor - initializes all elements with given value */
    explicit TStaticArray(const T& Value)
    {
        for (SizeType i = 0; i < N; ++i)
        {
            new (&Elements[i]) T(Value);
        }
    }

    /** Initializer list constructor */
    TStaticArray(std::initializer_list<T> InitList)
    {
        SizeType i = 0;
        for (const T& Item : InitList)
        {
            if (i >= N) break;
            new (&Elements[i++]) T(Item);
        }
        // Default construct remaining elements
        for (; i < N; ++i)
        {
            new (&Elements[i]) T();
        }
    }

    /** Copy constructor */
    TStaticArray(const TStaticArray& Other)
    {
        for (SizeType i = 0; i < N; ++i)
        {
            new (&Elements[i]) T(Other.Elements[i]);
        }
    }

    /** Move constructor */
    TStaticArray(TStaticArray&& Other) noexcept
    {
        for (SizeType i = 0; i < N; ++i)
        {
            new (&Elements[i]) T(std::move(Other.Elements[i]));
        }
    }

    /** Destructor */
    ~TStaticArray()
    {
        for (SizeType i = 0; i < N; ++i)
        {
            Elements[i].~T();
        }
    }

    /** Copy assignment */
    TStaticArray& operator=(const TStaticArray& Other)
    {
        if (this != &Other)
        {
            for (SizeType i = 0; i < N; ++i)
            {
                Elements[i] = Other.Elements[i];
            }
        }
        return *this;
    }

    /** Move assignment */
    TStaticArray& operator=(TStaticArray&& Other) noexcept
    {
        if (this != &Other)
        {
            for (SizeType i = 0; i < N; ++i)
            {
                Elements[i] = std::move(Other.Elements[i]);
            }
        }
        return *this;
    }

public:
    // ========================================================================
    // Element Access
    // ========================================================================

    /** Access element by index (with bounds checking in debug) */
    FORCEINLINE T& operator[](SizeType Index)
    {
#if defined(_DEBUG) || defined(DEBUG)
        if (Index >= N)
        {
            throw std::out_of_range("TStaticArray index out of range");
        }
#endif
        return Elements[Index];
    }

    FORCEINLINE const T& operator[](SizeType Index) const
    {
#if defined(_DEBUG) || defined(DEBUG)
        if (Index >= N)
        {
            throw std::out_of_range("TStaticArray index out of range");
        }
#endif
        return Elements[Index];
    }

    /** Access element with bounds checking */
    T& At(SizeType Index)
    {
        if (Index >= N)
        {
            throw std::out_of_range("TStaticArray::At index out of range");
        }
        return Elements[Index];
    }

    const T& At(SizeType Index) const
    {
        if (Index >= N)
        {
            throw std::out_of_range("TStaticArray::At index out of range");
        }
        return Elements[Index];
    }

    /** Get first element */
    FORCEINLINE T& First() { return Elements[0]; }
    FORCEINLINE const T& First() const { return Elements[0]; }

    /** Get last element */
    FORCEINLINE T& Last() { return Elements[N - 1]; }
    FORCEINLINE const T& Last() const { return Elements[N - 1]; }

    /** Get raw pointer to data */
    FORCEINLINE T* GetData() { return Elements; }
    FORCEINLINE const T* GetData() const { return Elements; }

public:
    // ========================================================================
    // Capacity
    // ========================================================================

    /** Get number of elements (always N) */
    static constexpr SizeType Num() { return N; }

    /** Get maximum size (always N) */
    static constexpr SizeType Max() { return N; }

    /** Check if empty (always false for N > 0) */
    static constexpr bool IsEmpty() { return N == 0; }

    /** Get size in bytes */
    static constexpr size_t GetAllocatedSize() { return sizeof(T) * N; }

public:
    // ========================================================================
    // Modifiers
    // ========================================================================

    /** Fill all elements with value */
    void Fill(const T& Value)
    {
        for (SizeType i = 0; i < N; ++i)
        {
            Elements[i] = Value;
        }
    }

    /** Swap with another array */
    void Swap(TStaticArray& Other)
    {
        for (SizeType i = 0; i < N; ++i)
        {
            std::swap(Elements[i], Other.Elements[i]);
        }
    }

public:
    // ========================================================================
    // Iterators
    // ========================================================================

    FORCEINLINE Iterator begin() { return Elements; }
    FORCEINLINE ConstIterator begin() const { return Elements; }
    FORCEINLINE Iterator end() { return Elements + N; }
    FORCEINLINE ConstIterator end() const { return Elements + N; }

    FORCEINLINE ConstIterator cbegin() const { return Elements; }
    FORCEINLINE ConstIterator cend() const { return Elements + N; }

public:
    // ========================================================================
    // Comparison
    // ========================================================================

    bool operator==(const TStaticArray& Other) const
    {
        for (SizeType i = 0; i < N; ++i)
        {
            if (!(Elements[i] == Other.Elements[i]))
            {
                return false;
            }
        }
        return true;
    }

    bool operator!=(const TStaticArray& Other) const
    {
        return !(*this == Other);
    }

private:
    /** Storage for elements */
    alignas(T) T Elements[N];
};

// ============================================================================
// TStaticArray<T, 0> Specialization
// ============================================================================

template<typename T>
class TStaticArray<T, 0>
{
public:
    using ElementType = T;
    using SizeType = uint32;
    using Iterator = T*;
    using ConstIterator = const T*;

    static constexpr SizeType ArraySize = 0;

    TStaticArray() = default;
    explicit TStaticArray(const T&) {}
    TStaticArray(std::initializer_list<T>) {}

    static constexpr SizeType Num() { return 0; }
    static constexpr SizeType Max() { return 0; }
    static constexpr bool IsEmpty() { return true; }
    static constexpr size_t GetAllocatedSize() { return 0; }

    T* GetData() { return nullptr; }
    const T* GetData() const { return nullptr; }

    Iterator begin() { return nullptr; }
    ConstIterator begin() const { return nullptr; }
    Iterator end() { return nullptr; }
    ConstIterator end() const { return nullptr; }

    bool operator==(const TStaticArray&) const { return true; }
    bool operator!=(const TStaticArray&) const { return false; }
};

// ============================================================================
// MakeStaticArray Helper
// ============================================================================

/**
 * Helper to create TStaticArray with deduced size
 */
template<typename T, typename... Args>
TStaticArray<T, sizeof...(Args)> MakeStaticArray(Args&&... InArgs)
{
    return TStaticArray<T, sizeof...(Args)>{ std::forward<Args>(InArgs)... };
}

} // namespace MonsterEngine
