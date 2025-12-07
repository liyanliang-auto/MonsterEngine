// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file UniquePtr.h
 * @brief Unique pointer implementation for MonsterEngine
 * 
 * TUniquePtr is a single-ownership smart pointer that automatically deletes
 * its managed object when it goes out of scope. Based on UE5's UniquePtr.h.
 * 
 * Features:
 * - Non-copyable, move-only semantics
 * - Custom deleter support
 * - Array specialization
 * - Zero overhead (same size as raw pointer when using default deleter)
 */

#include "Core/CoreTypes.h"
#include <type_traits>
#include <utility>

namespace MonsterEngine
{

// ============================================================================
// Default Deleters
// ============================================================================

/**
 * TDefaultDelete - Default deleter for single objects
 */
template<typename T>
struct TDefaultDelete
{
    TDefaultDelete() = default;

    template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    TDefaultDelete(TDefaultDelete<U> const&) {}

    FORCEINLINE void operator()(T* Ptr) const
    {
        static_assert(sizeof(T) > 0, "Cannot delete an incomplete type");
        delete Ptr;
    }
};

/**
 * TDefaultDelete - Specialization for arrays
 */
template<typename T>
struct TDefaultDelete<T[]>
{
    TDefaultDelete() = default;

    template<typename U, typename = std::enable_if_t<std::is_convertible_v<U(*)[], T(*)[]>>>
    TDefaultDelete(TDefaultDelete<U[]> const&) {}

    template<typename U, typename = std::enable_if_t<std::is_convertible_v<U(*)[], T(*)[]>>>
    FORCEINLINE void operator()(U* Ptr) const
    {
        static_assert(sizeof(T) > 0, "Cannot delete an incomplete type");
        delete[] Ptr;
    }
};

// ============================================================================
// TUniquePtr - Single Object
// ============================================================================

/**
 * TUniquePtr - Unique ownership smart pointer
 * 
 * Manages a single dynamically allocated object. The object is deleted
 * when the TUniquePtr is destroyed or reset.
 * 
 * @tparam T The type of object being managed
 * @tparam Deleter The deleter type (defaults to TDefaultDelete<T>)
 */
template<typename T, typename Deleter = TDefaultDelete<T>>
class TUniquePtr : private Deleter
{
    template<typename OtherT, typename OtherDeleter>
    friend class TUniquePtr;

public:
    using ElementType = T;
    using DeleterType = Deleter;

    // Non-copyable
    TUniquePtr(TUniquePtr const&) = delete;
    TUniquePtr& operator=(TUniquePtr const&) = delete;

    /** Default constructor - creates an empty unique pointer */
    FORCEINLINE TUniquePtr()
        : Deleter()
        , Ptr(nullptr)
    {
    }

    /** Nullptr constructor */
    FORCEINLINE TUniquePtr(std::nullptr_t)
        : Deleter()
        , Ptr(nullptr)
    {
    }

    /** Pointer constructor - takes ownership of the pointed-to object */
    template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    explicit FORCEINLINE TUniquePtr(U* InPtr)
        : Deleter()
        , Ptr(InPtr)
    {
    }

    /** Pointer constructor with deleter (move) */
    template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    FORCEINLINE TUniquePtr(U* InPtr, Deleter&& InDeleter)
        : Deleter(std::move(InDeleter))
        , Ptr(InPtr)
    {
    }

    /** Pointer constructor with deleter (copy) */
    template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    FORCEINLINE TUniquePtr(U* InPtr, Deleter const& InDeleter)
        : Deleter(InDeleter)
        , Ptr(InPtr)
    {
    }

    /** Move constructor */
    FORCEINLINE TUniquePtr(TUniquePtr&& Other)
        : Deleter(std::move(Other.GetDeleter()))
        , Ptr(Other.Ptr)
    {
        Other.Ptr = nullptr;
    }

    /** Move constructor from compatible type */
    template<typename OtherT, typename OtherDeleter,
             typename = std::enable_if_t<!std::is_array_v<OtherT>>,
             typename = std::enable_if_t<std::is_convertible_v<OtherT*, T*>>>
    FORCEINLINE TUniquePtr(TUniquePtr<OtherT, OtherDeleter>&& Other)
        : Deleter(std::move(Other.GetDeleter()))
        , Ptr(Other.Ptr)
    {
        Other.Ptr = nullptr;
    }

    /** Destructor */
    FORCEINLINE ~TUniquePtr()
    {
        if (Ptr != nullptr)
        {
            GetDeleter()(Ptr);
        }
    }

    /** Move assignment */
    FORCEINLINE TUniquePtr& operator=(TUniquePtr&& Other)
    {
        if (this != &Other)
        {
            T* OldPtr = Ptr;
            Ptr = Other.Ptr;
            Other.Ptr = nullptr;
            if (OldPtr != nullptr)
            {
                GetDeleter()(OldPtr);
            }
            GetDeleter() = std::move(Other.GetDeleter());
        }
        return *this;
    }

    /** Move assignment from compatible type */
    template<typename OtherT, typename OtherDeleter,
             typename = std::enable_if_t<!std::is_array_v<OtherT>>,
             typename = std::enable_if_t<std::is_convertible_v<OtherT*, T*>>>
    FORCEINLINE TUniquePtr& operator=(TUniquePtr<OtherT, OtherDeleter>&& Other)
    {
        T* OldPtr = Ptr;
        Ptr = Other.Ptr;
        Other.Ptr = nullptr;
        if (OldPtr != nullptr)
        {
            GetDeleter()(OldPtr);
        }
        GetDeleter() = std::move(Other.GetDeleter());
        return *this;
    }

    /** Nullptr assignment */
    FORCEINLINE TUniquePtr& operator=(std::nullptr_t)
    {
        T* OldPtr = Ptr;
        Ptr = nullptr;
        if (OldPtr != nullptr)
        {
            GetDeleter()(OldPtr);
        }
        return *this;
    }

    /** Tests if the TUniquePtr currently owns an object */
    [[nodiscard]] FORCEINLINE bool IsValid() const
    {
        return Ptr != nullptr;
    }

    /** Explicit bool conversion */
    [[nodiscard]] FORCEINLINE explicit operator bool() const
    {
        return IsValid();
    }

    /** Logical not operator */
    [[nodiscard]] FORCEINLINE bool operator!() const
    {
        return !IsValid();
    }

    /** Indirection operator */
    [[nodiscard]] FORCEINLINE T* operator->() const
    {
        return Ptr;
    }

    /** Dereference operator */
    [[nodiscard]] FORCEINLINE T& operator*() const
    {
        return *Ptr;
    }

    /** Returns a pointer to the owned object without relinquishing ownership */
    [[nodiscard]] FORCEINLINE T* Get() const
    {
        return Ptr;
    }

    /** Relinquishes control of the owned object to the caller */
    [[nodiscard]] FORCEINLINE T* Release()
    {
        T* Result = Ptr;
        Ptr = nullptr;
        return Result;
    }

    /** Gives the TUniquePtr a new object to own, destroying any previously-owned object */
    FORCEINLINE void Reset(T* InPtr = nullptr)
    {
        if (Ptr != InPtr)
        {
            T* OldPtr = Ptr;
            Ptr = InPtr;
            if (OldPtr != nullptr)
            {
                GetDeleter()(OldPtr);
            }
        }
    }

    /** Returns a reference to the deleter */
    [[nodiscard]] FORCEINLINE Deleter& GetDeleter()
    {
        return static_cast<Deleter&>(*this);
    }

    /** Returns a const reference to the deleter */
    [[nodiscard]] FORCEINLINE Deleter const& GetDeleter() const
    {
        return static_cast<Deleter const&>(*this);
    }

    // Comparison operators
    template<typename RhsT>
    [[nodiscard]] FORCEINLINE bool operator==(TUniquePtr<RhsT> const& Rhs) const
    {
        return Get() == Rhs.Get();
    }

    template<typename RhsT>
    [[nodiscard]] FORCEINLINE bool operator!=(TUniquePtr<RhsT> const& Rhs) const
    {
        return Get() != Rhs.Get();
    }

    [[nodiscard]] FORCEINLINE bool operator==(std::nullptr_t) const
    {
        return !IsValid();
    }

    [[nodiscard]] FORCEINLINE bool operator!=(std::nullptr_t) const
    {
        return IsValid();
    }

private:
    T* Ptr;
};

// ============================================================================
// TUniquePtr - Array Specialization
// ============================================================================

/**
 * TUniquePtr array specialization
 * 
 * Manages a dynamically allocated array. Uses delete[] for cleanup.
 */
template<typename T, typename Deleter>
class TUniquePtr<T[], Deleter> : private Deleter
{
    template<typename OtherT, typename OtherDeleter>
    friend class TUniquePtr;

public:
    using ElementType = T;
    using DeleterType = Deleter;

    // Non-copyable
    TUniquePtr(TUniquePtr const&) = delete;
    TUniquePtr& operator=(TUniquePtr const&) = delete;

    /** Default constructor */
    FORCEINLINE TUniquePtr()
        : Deleter()
        , Ptr(nullptr)
    {
    }

    /** Nullptr constructor */
    FORCEINLINE TUniquePtr(std::nullptr_t)
        : Deleter()
        , Ptr(nullptr)
    {
    }

    /** Pointer constructor */
    template<typename U, typename = std::enable_if_t<std::is_convertible_v<U(*)[], T(*)[]>>>
    explicit FORCEINLINE TUniquePtr(U* InPtr)
        : Deleter()
        , Ptr(InPtr)
    {
    }

    /** Pointer constructor with deleter */
    template<typename U, typename = std::enable_if_t<std::is_convertible_v<U(*)[], T(*)[]>>>
    FORCEINLINE TUniquePtr(U* InPtr, Deleter&& InDeleter)
        : Deleter(std::move(InDeleter))
        , Ptr(InPtr)
    {
    }

    /** Move constructor */
    FORCEINLINE TUniquePtr(TUniquePtr&& Other)
        : Deleter(std::move(Other.GetDeleter()))
        , Ptr(Other.Ptr)
    {
        Other.Ptr = nullptr;
    }

    /** Destructor */
    FORCEINLINE ~TUniquePtr()
    {
        if (Ptr != nullptr)
        {
            GetDeleter()(Ptr);
        }
    }

    /** Move assignment */
    FORCEINLINE TUniquePtr& operator=(TUniquePtr&& Other)
    {
        if (this != &Other)
        {
            T* OldPtr = Ptr;
            Ptr = Other.Ptr;
            Other.Ptr = nullptr;
            if (OldPtr != nullptr)
            {
                GetDeleter()(OldPtr);
            }
            GetDeleter() = std::move(Other.GetDeleter());
        }
        return *this;
    }

    /** Nullptr assignment */
    FORCEINLINE TUniquePtr& operator=(std::nullptr_t)
    {
        T* OldPtr = Ptr;
        Ptr = nullptr;
        if (OldPtr != nullptr)
        {
            GetDeleter()(OldPtr);
        }
        return *this;
    }

    /** Tests if valid */
    [[nodiscard]] FORCEINLINE bool IsValid() const
    {
        return Ptr != nullptr;
    }

    /** Explicit bool conversion */
    [[nodiscard]] FORCEINLINE explicit operator bool() const
    {
        return IsValid();
    }

    /** Array subscript operator */
    [[nodiscard]] FORCEINLINE T& operator[](SIZE_T Index) const
    {
        return Ptr[Index];
    }

    /** Returns a pointer to the owned array */
    [[nodiscard]] FORCEINLINE T* Get() const
    {
        return Ptr;
    }

    /** Relinquishes control of the owned array */
    [[nodiscard]] FORCEINLINE T* Release()
    {
        T* Result = Ptr;
        Ptr = nullptr;
        return Result;
    }

    /** Resets with a new array */
    FORCEINLINE void Reset(T* InPtr = nullptr)
    {
        if (Ptr != InPtr)
        {
            T* OldPtr = Ptr;
            Ptr = InPtr;
            if (OldPtr != nullptr)
            {
                GetDeleter()(OldPtr);
            }
        }
    }

    /** Returns a reference to the deleter */
    [[nodiscard]] FORCEINLINE Deleter& GetDeleter()
    {
        return static_cast<Deleter&>(*this);
    }

    [[nodiscard]] FORCEINLINE Deleter const& GetDeleter() const
    {
        return static_cast<Deleter const&>(*this);
    }

    // Comparison operators
    [[nodiscard]] FORCEINLINE bool operator==(std::nullptr_t) const
    {
        return !IsValid();
    }

    [[nodiscard]] FORCEINLINE bool operator!=(std::nullptr_t) const
    {
        return IsValid();
    }

private:
    T* Ptr;
};

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * MakeUnique - Creates a TUniquePtr with a new object
 * 
 * This is the preferred way to create unique pointers.
 */
template<typename T, typename... ArgTypes>
[[nodiscard]] FORCEINLINE std::enable_if_t<!std::is_array_v<T>, TUniquePtr<T>> 
MakeUnique(ArgTypes&&... Args)
{
    return TUniquePtr<T>(new T(std::forward<ArgTypes>(Args)...));
}

/**
 * MakeUnique for arrays - Creates a TUniquePtr with a new array
 */
template<typename T>
[[nodiscard]] FORCEINLINE std::enable_if_t<std::is_array_v<T> && std::extent_v<T> == 0, TUniquePtr<T>>
MakeUnique(SIZE_T Size)
{
    using ElementType = std::remove_extent_t<T>;
    return TUniquePtr<T>(new ElementType[Size]());
}

// Prevent MakeUnique for arrays with known bounds
template<typename T, typename... ArgTypes>
std::enable_if_t<std::extent_v<T> != 0> MakeUnique(ArgTypes&&...) = delete;

// ============================================================================
// Hash Function
// ============================================================================

template<typename T, typename Deleter>
[[nodiscard]] uint32 GetTypeHash(TUniquePtr<T, Deleter> const& InPtr)
{
    return GetTypeHash(InPtr.Get());
}

} // namespace MonsterEngine
