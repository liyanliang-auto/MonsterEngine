// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file SharedPointer.h
 * @brief Smart pointer library for MonsterEngine
 * 
 * This is a smart pointer library consisting of shared references (TSharedRef),
 * shared pointers (TSharedPtr), weak pointers (TWeakPtr) and TSharedFromThis.
 * Based on UE5's SharedPointer implementation.
 * 
 * Benefits:
 * - Clean syntax similar to regular C++ pointers
 * - Prevents memory leaks through automatic reference counting
 * - Weak referencing to safely check object lifetime
 * - Optional thread safety (ESPMode::ThreadSafe or ESPMode::NotThreadSafe)
 * - Non-nullable TSharedRef for guaranteed valid references
 * 
 * Usage:
 * - Use TSharedRef when you need a guaranteed non-null reference
 * - Use TSharedPtr when the pointer may be null
 * - Use TWeakPtr to observe without extending lifetime
 * - Prefer MakeShared<T>() for efficient single-allocation construction
 */

#include "Core/Templates/SharedPointerInternals.h"

namespace MonsterEngine
{

// ============================================================================
// TSharedRef - Non-nullable Shared Reference
// ============================================================================

/**
 * TSharedRef is a non-nullable, reference-counted smart pointer.
 * 
 * Unlike TSharedPtr, TSharedRef can never be null. This provides compile-time
 * guarantees that the referenced object is always valid.
 * 
 * @tparam ObjectType The type of object being referenced
 * @tparam Mode Thread safety mode (ThreadSafe or NotThreadSafe)
 */
template<class ObjectType, ESPMode InMode>
class TSharedRef
{
public:
    using ElementType = ObjectType;
    static constexpr ESPMode Mode = InMode;

    // NOTE: TSharedRef has no default constructor - it must always reference a valid object

    /**
     * Constructs a shared reference that owns the specified object.
     * @param InObject Object this shared reference will own. Must not be nullptr.
     */
    template<typename OtherType, 
             typename = std::enable_if_t<std::is_convertible_v<OtherType*, ObjectType*>>>
    FORCEINLINE explicit TSharedRef(OtherType* InObject)
        : Object(InObject)
        , SharedReferenceCount(SharedPointerInternals::NewDefaultReferenceController<Mode>(InObject))
    {
        Init(InObject);
    }

    /**
     * Constructs a shared reference with a custom deleter.
     */
    template<typename OtherType, typename DeleterType,
             typename = std::enable_if_t<std::is_convertible_v<OtherType*, ObjectType*>>>
    FORCEINLINE TSharedRef(OtherType* InObject, DeleterType&& InDeleter)
        : Object(InObject)
        , SharedReferenceCount(SharedPointerInternals::NewCustomReferenceController<Mode>(
              InObject, std::forward<DeleterType>(InDeleter)))
    {
        Init(InObject);
    }

    /**
     * Constructs from MakeShareable proxy.
     */
    template<typename OtherType,
             typename = std::enable_if_t<std::is_convertible_v<OtherType*, ObjectType*>>>
    FORCEINLINE TSharedRef(SharedPointerInternals::TRawPtrProxy<OtherType> const& InRawPtrProxy)
        : Object(InRawPtrProxy.Object)
        , SharedReferenceCount(SharedPointerInternals::NewDefaultReferenceController<Mode>(InRawPtrProxy.Object))
    {
        Init(InRawPtrProxy.Object);
    }

    /**
     * Copy constructor from compatible type.
     */
    template<typename OtherType,
             typename = std::enable_if_t<std::is_convertible_v<OtherType*, ObjectType*>>>
    FORCEINLINE TSharedRef(TSharedRef<OtherType, Mode> const& InSharedRef)
        : Object(InSharedRef.Object)
        , SharedReferenceCount(InSharedRef.SharedReferenceCount)
    {
    }

    /** Copy constructor */
    FORCEINLINE TSharedRef(TSharedRef const& InSharedRef)
        : Object(InSharedRef.Object)
        , SharedReferenceCount(InSharedRef.SharedReferenceCount)
    {
    }

    /** Move constructor - note: doesn't actually move to preserve non-null invariant */
    FORCEINLINE TSharedRef(TSharedRef&& InSharedRef)
        : Object(InSharedRef.Object)
        , SharedReferenceCount(InSharedRef.SharedReferenceCount)
    {
    }

    /** Static cast constructor */
    template<typename OtherType>
    FORCEINLINE TSharedRef(TSharedRef<OtherType, Mode> const& InSharedRef, SharedPointerInternals::FStaticCastTag)
        : Object(static_cast<ObjectType*>(InSharedRef.Object))
        , SharedReferenceCount(InSharedRef.SharedReferenceCount)
    {
    }

    /** Const cast constructor */
    template<typename OtherType>
    FORCEINLINE TSharedRef(TSharedRef<OtherType, Mode> const& InSharedRef, SharedPointerInternals::FConstCastTag)
        : Object(const_cast<ObjectType*>(InSharedRef.Object))
        , SharedReferenceCount(InSharedRef.SharedReferenceCount)
    {
    }

    /** Aliasing constructor */
    template<typename OtherType>
    FORCEINLINE TSharedRef(TSharedRef<OtherType, Mode> const& OtherSharedRef, ObjectType* InObject)
        : Object(InObject)
        , SharedReferenceCount(OtherSharedRef.SharedReferenceCount)
    {
    }

    /** Conversion from TSharedPtr (must be valid!) */
    template<typename OtherType,
             typename = std::enable_if_t<std::is_convertible_v<OtherType*, ObjectType*>>>
    FORCEINLINE explicit TSharedRef(TSharedPtr<OtherType, Mode> const& InSharedPtr)
        : Object(InSharedPtr.Object)
        , SharedReferenceCount(InSharedPtr.SharedReferenceCount)
    {
    }

    /** Move conversion from TSharedPtr (must be valid!) */
    template<typename OtherType,
             typename = std::enable_if_t<std::is_convertible_v<OtherType*, ObjectType*>>>
    FORCEINLINE explicit TSharedRef(TSharedPtr<OtherType, Mode>&& InSharedPtr)
        : Object(InSharedPtr.Object)
        , SharedReferenceCount(std::move(InSharedPtr.SharedReferenceCount))
    {
        InSharedPtr.Object = nullptr;
    }

    /** Copy assignment */
    FORCEINLINE TSharedRef& operator=(TSharedRef const& InSharedRef)
    {
        TSharedRef Temp = InSharedRef;
        Swap(Temp, *this);
        return *this;
    }

    /** Move assignment */
    FORCEINLINE TSharedRef& operator=(TSharedRef&& InSharedRef)
    {
        // Swap contents using std::swap
        std::swap(Object, InSharedRef.Object);
        std::swap(SharedReferenceCount, InSharedRef.SharedReferenceCount);
        return *this;
    }

    /** Converts to TSharedPtr */
    [[nodiscard]] FORCEINLINE TSharedPtr<ObjectType, Mode> ToSharedPtr() const
    {
        return TSharedPtr<ObjectType, Mode>(*this);
    }

    /** Converts to TWeakPtr */
    [[nodiscard]] FORCEINLINE TWeakPtr<ObjectType, Mode> ToWeakPtr() const
    {
        return TWeakPtr<ObjectType, Mode>(*this);
    }

    /** Returns a C++ reference to the object */
    [[nodiscard]] FORCEINLINE ObjectType& Get() const
    {
        return *Object;
    }

    /** Dereference operator */
    [[nodiscard]] FORCEINLINE ObjectType& operator*() const
    {
        return *Object;
    }

    /** Arrow operator */
    [[nodiscard]] FORCEINLINE ObjectType* operator->() const
    {
        return Object;
    }

    /** Returns the number of shared references */
    [[nodiscard]] FORCEINLINE int32 GetSharedReferenceCount() const
    {
        return SharedReferenceCount.GetSharedReferenceCount();
    }

    /** Returns true if this is the only shared reference */
    [[nodiscard]] FORCEINLINE bool IsUnique() const
    {
        return SharedReferenceCount.IsUnique();
    }

private:
    template<class OtherType>
    void Init(OtherType* InObject)
    {
        SharedPointerInternals::EnableSharedFromThis(this, InObject, InObject);
    }

    [[nodiscard]] FORCEINLINE bool IsValid() const
    {
        return Object != nullptr;
    }

    // Friend declarations
    template<class OtherType, ESPMode OtherMode> friend class TSharedRef;
    template<class OtherType, ESPMode OtherMode> friend class TSharedPtr;
    template<class OtherType, ESPMode OtherMode> friend class TWeakPtr;

    // Internal constructor for MakeShared
    FORCEINLINE explicit TSharedRef(ObjectType* InObject, 
                                     SharedPointerInternals::TReferenceControllerBase<Mode>* InController)
        : Object(InObject)
        , SharedReferenceCount(InController)
    {
        Init(InObject);
    }

    template<typename T, ESPMode M, typename... Args>
    friend TSharedRef<T, M> MakeShared(Args&&... args);

    ObjectType* Object;
    SharedPointerInternals::FSharedReferencer<Mode> SharedReferenceCount;
};

// ============================================================================
// TSharedPtr - Nullable Shared Pointer
// ============================================================================

/**
 * TSharedPtr is a nullable, reference-counted smart pointer.
 * 
 * Similar to std::shared_ptr, but with optional thread safety and
 * integration with TSharedRef and TWeakPtr.
 * 
 * @tparam ObjectType The type of object being pointed to
 * @tparam Mode Thread safety mode (ThreadSafe or NotThreadSafe)
 */
template<class ObjectType, ESPMode InMode>
class TSharedPtr
{
public:
    using ElementType = ObjectType;
    static constexpr ESPMode Mode = InMode;

    /** Constructs an empty shared pointer */
    FORCEINLINE TSharedPtr(SharedPointerInternals::FNullTag* = nullptr)
        : Object(nullptr)
        , SharedReferenceCount()
    {
    }

    /** Constructs a shared pointer that owns the specified object */
    template<typename OtherType,
             typename = std::enable_if_t<std::is_convertible_v<OtherType*, ObjectType*>>>
    FORCEINLINE explicit TSharedPtr(OtherType* InObject)
        : Object(InObject)
        , SharedReferenceCount(SharedPointerInternals::NewDefaultReferenceController<Mode>(InObject))
    {
        SharedPointerInternals::EnableSharedFromThis(this, InObject, InObject);
    }

    /** Constructs with custom deleter */
    template<typename OtherType, typename DeleterType,
             typename = std::enable_if_t<std::is_convertible_v<OtherType*, ObjectType*>>>
    FORCEINLINE TSharedPtr(OtherType* InObject, DeleterType&& InDeleter)
        : Object(InObject)
        , SharedReferenceCount(SharedPointerInternals::NewCustomReferenceController<Mode>(
              InObject, std::forward<DeleterType>(InDeleter)))
    {
        SharedPointerInternals::EnableSharedFromThis(this, InObject, InObject);
    }

    /** Constructs from MakeShareable proxy */
    template<typename OtherType,
             typename = std::enable_if_t<std::is_convertible_v<OtherType*, ObjectType*>>>
    FORCEINLINE TSharedPtr(SharedPointerInternals::TRawPtrProxy<OtherType> const& InRawPtrProxy)
        : Object(InRawPtrProxy.Object)
        , SharedReferenceCount(SharedPointerInternals::NewDefaultReferenceController<Mode>(InRawPtrProxy.Object))
    {
        SharedPointerInternals::EnableSharedFromThis(this, InRawPtrProxy.Object, InRawPtrProxy.Object);
    }

    /** Constructs from MakeShareable proxy with custom deleter */
    template<typename OtherType, typename DeleterType,
             typename = std::enable_if_t<std::is_convertible_v<OtherType*, ObjectType*>>>
    FORCEINLINE TSharedPtr(SharedPointerInternals::TRawPtrProxyWithDeleter<OtherType, DeleterType>&& InRawPtrProxy)
        : Object(InRawPtrProxy.Object)
        , SharedReferenceCount(SharedPointerInternals::NewCustomReferenceController<Mode>(
              InRawPtrProxy.Object, std::move(InRawPtrProxy.Deleter)))
    {
        SharedPointerInternals::EnableSharedFromThis(this, InRawPtrProxy.Object, InRawPtrProxy.Object);
    }

    /** Copy constructor from compatible type */
    template<typename OtherType,
             typename = std::enable_if_t<std::is_convertible_v<OtherType*, ObjectType*>>>
    FORCEINLINE TSharedPtr(TSharedPtr<OtherType, Mode> const& InSharedPtr)
        : Object(InSharedPtr.Object)
        , SharedReferenceCount(InSharedPtr.SharedReferenceCount)
    {
    }

    /** Copy constructor */
    FORCEINLINE TSharedPtr(TSharedPtr const& InSharedPtr)
        : Object(InSharedPtr.Object)
        , SharedReferenceCount(InSharedPtr.SharedReferenceCount)
    {
    }

    /** Move constructor */
    FORCEINLINE TSharedPtr(TSharedPtr&& InSharedPtr)
        : Object(InSharedPtr.Object)
        , SharedReferenceCount(std::move(InSharedPtr.SharedReferenceCount))
    {
        InSharedPtr.Object = nullptr;
    }

    /** Implicit conversion from TSharedRef */
    template<typename OtherType,
             typename = std::enable_if_t<std::is_convertible_v<OtherType*, ObjectType*>>>
    FORCEINLINE TSharedPtr(TSharedRef<OtherType, Mode> const& InSharedRef)
        : Object(InSharedRef.Object)
        , SharedReferenceCount(InSharedRef.SharedReferenceCount)
    {
    }

    /** Static cast constructor */
    template<typename OtherType>
    FORCEINLINE TSharedPtr(TSharedPtr<OtherType, Mode> const& InSharedPtr, SharedPointerInternals::FStaticCastTag)
        : Object(static_cast<ObjectType*>(InSharedPtr.Object))
        , SharedReferenceCount(InSharedPtr.SharedReferenceCount)
    {
    }

    /** Const cast constructor */
    template<typename OtherType>
    FORCEINLINE TSharedPtr(TSharedPtr<OtherType, Mode> const& InSharedPtr, SharedPointerInternals::FConstCastTag)
        : Object(const_cast<ObjectType*>(InSharedPtr.Object))
        , SharedReferenceCount(InSharedPtr.SharedReferenceCount)
    {
    }

    /** Aliasing constructor */
    template<typename OtherType>
    FORCEINLINE TSharedPtr(TSharedPtr<OtherType, Mode> const& OtherSharedPtr, ObjectType* InObject)
        : Object(InObject)
        , SharedReferenceCount(OtherSharedPtr.SharedReferenceCount)
    {
    }

    /** Assignment to nullptr */
    FORCEINLINE TSharedPtr& operator=(SharedPointerInternals::FNullTag*)
    {
        Reset();
        return *this;
    }

    /** Copy assignment */
    FORCEINLINE TSharedPtr& operator=(TSharedPtr const& InSharedPtr)
    {
        TSharedPtr Temp = InSharedPtr;
        Swap(Temp, *this);
        return *this;
    }

    /** Move assignment */
    FORCEINLINE TSharedPtr& operator=(TSharedPtr&& InSharedPtr)
    {
        if (this != &InSharedPtr)
        {
            Object = InSharedPtr.Object;
            InSharedPtr.Object = nullptr;
            SharedReferenceCount = std::move(InSharedPtr.SharedReferenceCount);
        }
        return *this;
    }

    /** Converts to TSharedRef. Pointer must be valid! */
    [[nodiscard]] FORCEINLINE TSharedRef<ObjectType, Mode> ToSharedRef() const&
    {
        return TSharedRef<ObjectType, Mode>(*this);
    }

    [[nodiscard]] FORCEINLINE TSharedRef<ObjectType, Mode> ToSharedRef() &&
    {
        return TSharedRef<ObjectType, Mode>(std::move(*this));
    }

    /** Converts to TWeakPtr */
    [[nodiscard]] FORCEINLINE TWeakPtr<ObjectType, Mode> ToWeakPtr() const
    {
        return TWeakPtr<ObjectType, Mode>(*this);
    }

    /** Returns the object pointer, or nullptr */
    [[nodiscard]] FORCEINLINE ObjectType* Get() const
    {
        return Object;
    }

    /** Checks if the pointer is valid */
    [[nodiscard]] FORCEINLINE explicit operator bool() const
    {
        return Object != nullptr;
    }

    /** Checks if the pointer is valid */
    [[nodiscard]] FORCEINLINE bool IsValid() const
    {
        return Object != nullptr;
    }

    /** Dereference operator */
    [[nodiscard]] FORCEINLINE ObjectType& operator*() const
    {
        return *Object;
    }

    /** Arrow operator */
    [[nodiscard]] FORCEINLINE ObjectType* operator->() const
    {
        return Object;
    }

    /** Resets the shared pointer to null */
    FORCEINLINE void Reset()
    {
        *this = TSharedPtr<ObjectType, Mode>();
    }

    /** Returns the number of shared references */
    [[nodiscard]] FORCEINLINE int32 GetSharedReferenceCount() const
    {
        return SharedReferenceCount.GetSharedReferenceCount();
    }

    /** Returns true if this is the only shared reference */
    [[nodiscard]] FORCEINLINE bool IsUnique() const
    {
        return SharedReferenceCount.IsUnique();
    }

private:
    /** Private constructor from weak pointer (used by Pin()) */
    template<typename OtherType,
             typename = std::enable_if_t<std::is_convertible_v<OtherType*, ObjectType*>>>
    FORCEINLINE explicit TSharedPtr(TWeakPtr<OtherType, Mode> const& InWeakPtr)
        : Object(nullptr)
        , SharedReferenceCount(InWeakPtr.WeakReferenceCount)
    {
        if (SharedReferenceCount.IsValid())
        {
            Object = InWeakPtr.Object;
        }
    }

    template<typename OtherType,
             typename = std::enable_if_t<std::is_convertible_v<OtherType*, ObjectType*>>>
    FORCEINLINE explicit TSharedPtr(TWeakPtr<OtherType, Mode>&& InWeakPtr)
        : Object(nullptr)
        , SharedReferenceCount(std::move(InWeakPtr.WeakReferenceCount))
    {
        if (SharedReferenceCount.IsValid())
        {
            Object = InWeakPtr.Object;
            InWeakPtr.Object = nullptr;
        }
    }

    // Friend declarations
    template<class OtherType, ESPMode OtherMode> friend class TSharedPtr;
    template<class OtherType, ESPMode OtherMode> friend class TSharedRef;
    template<class OtherType, ESPMode OtherMode> friend class TWeakPtr;
    template<class OtherType, ESPMode OtherMode> friend class TSharedFromThis;

    ObjectType* Object;
    SharedPointerInternals::FSharedReferencer<Mode> SharedReferenceCount;
};

// ============================================================================
// TWeakPtr - Weak Pointer
// ============================================================================

/**
 * TWeakPtr is a non-owning reference to a shared object.
 * 
 * Weak pointers don't prevent the object from being destroyed. Use Pin()
 * to get a TSharedPtr if you need to access the object.
 * 
 * @tparam ObjectType The type of object being observed
 * @tparam Mode Thread safety mode (ThreadSafe or NotThreadSafe)
 */
template<class ObjectType, ESPMode InMode>
class TWeakPtr
{
public:
    using ElementType = ObjectType;
    static constexpr ESPMode Mode = InMode;

    /** Constructs an empty weak pointer */
    FORCEINLINE TWeakPtr(SharedPointerInternals::FNullTag* = nullptr)
        : Object(nullptr)
        , WeakReferenceCount()
    {
    }

    /** Constructs from a shared reference */
    template<typename OtherType,
             typename = std::enable_if_t<std::is_convertible_v<OtherType*, ObjectType*>>>
    FORCEINLINE TWeakPtr(TSharedRef<OtherType, Mode> const& InSharedRef)
        : Object(InSharedRef.Object)
        , WeakReferenceCount(InSharedRef.SharedReferenceCount)
    {
    }

    /** Constructs from a shared pointer */
    template<typename OtherType,
             typename = std::enable_if_t<std::is_convertible_v<OtherType*, ObjectType*>>>
    FORCEINLINE TWeakPtr(TSharedPtr<OtherType, Mode> const& InSharedPtr)
        : Object(InSharedPtr.Object)
        , WeakReferenceCount(InSharedPtr.SharedReferenceCount)
    {
    }

    /** Copy constructor from compatible type */
    template<typename OtherType,
             typename = std::enable_if_t<std::is_convertible_v<OtherType*, ObjectType*>>>
    FORCEINLINE TWeakPtr(TWeakPtr<OtherType, Mode> const& InWeakPtr)
        : Object(InWeakPtr.Object)
        , WeakReferenceCount(InWeakPtr.WeakReferenceCount)
    {
    }

    /** Copy constructor */
    FORCEINLINE TWeakPtr(TWeakPtr const& InWeakPtr)
        : Object(InWeakPtr.Object)
        , WeakReferenceCount(InWeakPtr.WeakReferenceCount)
    {
    }

    /** Move constructor */
    FORCEINLINE TWeakPtr(TWeakPtr&& InWeakPtr)
        : Object(InWeakPtr.Object)
        , WeakReferenceCount(std::move(InWeakPtr.WeakReferenceCount))
    {
        InWeakPtr.Object = nullptr;
    }

    /** Static cast constructor */
    template<typename OtherType>
    FORCEINLINE TWeakPtr(TWeakPtr<OtherType, Mode> const& InWeakPtr, SharedPointerInternals::FStaticCastTag)
        : Object(static_cast<ObjectType*>(InWeakPtr.Object))
        , WeakReferenceCount(InWeakPtr.WeakReferenceCount)
    {
    }

    /** Const cast constructor */
    template<typename OtherType>
    FORCEINLINE TWeakPtr(TWeakPtr<OtherType, Mode> const& InWeakPtr, SharedPointerInternals::FConstCastTag)
        : Object(const_cast<ObjectType*>(InWeakPtr.Object))
        , WeakReferenceCount(InWeakPtr.WeakReferenceCount)
    {
    }

    /** Assignment to nullptr */
    FORCEINLINE TWeakPtr& operator=(SharedPointerInternals::FNullTag*)
    {
        Reset();
        return *this;
    }

    /** Copy assignment */
    FORCEINLINE TWeakPtr& operator=(TWeakPtr const& InWeakPtr)
    {
        TWeakPtr Temp = InWeakPtr;
        Swap(Temp, *this);
        return *this;
    }

    /** Move assignment */
    FORCEINLINE TWeakPtr& operator=(TWeakPtr&& InWeakPtr)
    {
        if (this != &InWeakPtr)
        {
            Object = InWeakPtr.Object;
            InWeakPtr.Object = nullptr;
            WeakReferenceCount = std::move(InWeakPtr.WeakReferenceCount);
        }
        return *this;
    }

    /** Assignment from shared reference */
    template<typename OtherType,
             typename = std::enable_if_t<std::is_convertible_v<OtherType*, ObjectType*>>>
    FORCEINLINE TWeakPtr& operator=(TSharedRef<OtherType, Mode> const& InSharedRef)
    {
        Object = InSharedRef.Object;
        WeakReferenceCount = InSharedRef.SharedReferenceCount;
        return *this;
    }

    /** Assignment from shared pointer */
    template<typename OtherType,
             typename = std::enable_if_t<std::is_convertible_v<OtherType*, ObjectType*>>>
    FORCEINLINE TWeakPtr& operator=(TSharedPtr<OtherType, Mode> const& InSharedPtr)
    {
        Object = InSharedPtr.Object;
        WeakReferenceCount = InSharedPtr.SharedReferenceCount;
        return *this;
    }

    /**
     * Converts to a shared pointer.
     * Returns an invalid shared pointer if the object has been destroyed.
     */
    [[nodiscard]] FORCEINLINE TSharedPtr<ObjectType, Mode> Pin() const&
    {
        return TSharedPtr<ObjectType, Mode>(*this);
    }

    [[nodiscard]] FORCEINLINE TSharedPtr<ObjectType, Mode> Pin() &&
    {
        return TSharedPtr<ObjectType, Mode>(std::move(*this));
    }

    /** Checks if the weak pointer is still valid */
    [[nodiscard]] FORCEINLINE bool IsValid() const
    {
        return Object != nullptr && WeakReferenceCount.IsValid();
    }

    /** Resets the weak pointer */
    FORCEINLINE void Reset()
    {
        *this = TWeakPtr<ObjectType, Mode>();
    }

    /** Returns true if this points to the same object */
    [[nodiscard]] FORCEINLINE bool HasSameObject(const void* InOtherPtr) const
    {
        return Pin().Get() == InOtherPtr;
    }

private:
    // Friend declarations
    template<class OtherType, ESPMode OtherMode> friend class TWeakPtr;
    template<class OtherType, ESPMode OtherMode> friend class TSharedPtr;

    ObjectType* Object;
    SharedPointerInternals::FWeakReferencer<Mode> WeakReferenceCount;
};

// ============================================================================
// TSharedFromThis - Mixin for getting shared pointer from 'this'
// ============================================================================

/**
 * TSharedFromThis allows an object to get a shared pointer to itself.
 * 
 * Derive from this class to enable AsShared() and AsWeak() methods.
 * 
 * @tparam ObjectType The derived class type
 * @tparam Mode Thread safety mode (ThreadSafe or NotThreadSafe)
 */
template<class ObjectType, ESPMode Mode>
class TSharedFromThis
{
public:
    /**
     * Returns a shared reference to this object.
     * Only valid after the object has been assigned to a shared pointer.
     */
    [[nodiscard]] TSharedRef<ObjectType, Mode> AsShared()
    {
        TSharedPtr<ObjectType, Mode> SharedThis(WeakThis.Pin());
        return std::move(SharedThis).ToSharedRef();
    }

    [[nodiscard]] TSharedRef<ObjectType const, Mode> AsShared() const
    {
        TSharedPtr<ObjectType const, Mode> SharedThis(WeakThis);
        return std::move(SharedThis).ToSharedRef();
    }

    /**
     * Returns a weak pointer to this object.
     */
    [[nodiscard]] TWeakPtr<ObjectType, Mode> AsWeak()
    {
        return WeakThis;
    }

    [[nodiscard]] TWeakPtr<ObjectType const, Mode> AsWeak() const
    {
        return WeakThis;
    }

    /**
     * Checks if this object is managed by a shared pointer.
     */
    [[nodiscard]] FORCEINLINE bool DoesSharedInstanceExist() const
    {
        return WeakThis.IsValid();
    }

protected:
    TSharedFromThis() = default;
    TSharedFromThis(TSharedFromThis const&) {}
    TSharedFromThis& operator=(TSharedFromThis const&) { return *this; }
    ~TSharedFromThis() = default;

    template<class OtherType>
    [[nodiscard]] static TSharedRef<OtherType, Mode> SharedThis(OtherType* ThisPtr)
    {
        return StaticCastSharedRef<OtherType>(ThisPtr->AsShared());
    }

public:
    // Internal use only
    template<class SharedPtrType, class OtherType>
    FORCEINLINE void UpdateWeakReferenceInternal(TSharedPtr<SharedPtrType, Mode> const* InSharedPtr, OtherType* InObject) const
    {
        if (!WeakThis.IsValid())
        {
            WeakThis = TSharedPtr<ObjectType, Mode>(*InSharedPtr, const_cast<ObjectType*>(static_cast<ObjectType const*>(InObject)));
        }
    }

    template<class SharedRefType, class OtherType>
    FORCEINLINE void UpdateWeakReferenceInternal(TSharedRef<SharedRefType, Mode> const* InSharedRef, OtherType* InObject) const
    {
        if (!WeakThis.IsValid())
        {
            WeakThis = TSharedRef<ObjectType, Mode>(*InSharedRef, const_cast<ObjectType*>(static_cast<ObjectType const*>(InObject)));
        }
    }

private:
    mutable TWeakPtr<ObjectType, Mode> WeakThis;
};

// ============================================================================
// EnableSharedFromThis Implementation
// ============================================================================

namespace SharedPointerInternals
{

template<class SharedPtrType, class ObjectType, class OtherType, ESPMode Mode>
FORCEINLINE void EnableSharedFromThis(TSharedPtr<SharedPtrType, Mode> const* InSharedPtr,
                                       ObjectType const* InObject,
                                       TSharedFromThis<OtherType, Mode> const* InShareable)
{
    if (InShareable != nullptr)
    {
        InShareable->UpdateWeakReferenceInternal(InSharedPtr, const_cast<ObjectType*>(InObject));
    }
}

template<class SharedRefType, class ObjectType, class OtherType, ESPMode Mode>
FORCEINLINE void EnableSharedFromThis(TSharedRef<SharedRefType, Mode> const* InSharedRef,
                                       ObjectType const* InObject,
                                       TSharedFromThis<OtherType, Mode> const* InShareable)
{
    if (InShareable != nullptr)
    {
        InShareable->UpdateWeakReferenceInternal(InSharedRef, const_cast<ObjectType*>(InObject));
    }
}

} // namespace SharedPointerInternals

// ============================================================================
// Comparison Operators
// ============================================================================

// TSharedRef comparisons
template<class A, class B, ESPMode Mode>
[[nodiscard]] FORCEINLINE bool operator==(TSharedRef<A, Mode> const& Lhs, TSharedRef<B, Mode> const& Rhs)
{
    return &Lhs.Get() == &Rhs.Get();
}

template<class A, class B, ESPMode Mode>
[[nodiscard]] FORCEINLINE bool operator!=(TSharedRef<A, Mode> const& Lhs, TSharedRef<B, Mode> const& Rhs)
{
    return &Lhs.Get() != &Rhs.Get();
}

// TSharedPtr comparisons
template<class A, class B, ESPMode Mode>
[[nodiscard]] FORCEINLINE bool operator==(TSharedPtr<A, Mode> const& Lhs, TSharedPtr<B, Mode> const& Rhs)
{
    return Lhs.Get() == Rhs.Get();
}

template<class A, class B, ESPMode Mode>
[[nodiscard]] FORCEINLINE bool operator!=(TSharedPtr<A, Mode> const& Lhs, TSharedPtr<B, Mode> const& Rhs)
{
    return Lhs.Get() != Rhs.Get();
}

template<class A, ESPMode Mode>
[[nodiscard]] FORCEINLINE bool operator==(TSharedPtr<A, Mode> const& Lhs, std::nullptr_t)
{
    return !Lhs.IsValid();
}

template<class A, ESPMode Mode>
[[nodiscard]] FORCEINLINE bool operator==(std::nullptr_t, TSharedPtr<A, Mode> const& Rhs)
{
    return !Rhs.IsValid();
}

template<class A, ESPMode Mode>
[[nodiscard]] FORCEINLINE bool operator!=(TSharedPtr<A, Mode> const& Lhs, std::nullptr_t)
{
    return Lhs.IsValid();
}

template<class A, ESPMode Mode>
[[nodiscard]] FORCEINLINE bool operator!=(std::nullptr_t, TSharedPtr<A, Mode> const& Rhs)
{
    return Rhs.IsValid();
}

// TWeakPtr comparisons
template<class A, class B, ESPMode Mode>
[[nodiscard]] FORCEINLINE bool operator==(TWeakPtr<A, Mode> const& Lhs, TWeakPtr<B, Mode> const& Rhs)
{
    return Lhs.Pin().Get() == Rhs.Pin().Get();
}

template<class A, class B, ESPMode Mode>
[[nodiscard]] FORCEINLINE bool operator!=(TWeakPtr<A, Mode> const& Lhs, TWeakPtr<B, Mode> const& Rhs)
{
    return Lhs.Pin().Get() != Rhs.Pin().Get();
}

template<class A, ESPMode Mode>
[[nodiscard]] FORCEINLINE bool operator==(TWeakPtr<A, Mode> const& Lhs, std::nullptr_t)
{
    return !Lhs.IsValid();
}

template<class A, ESPMode Mode>
[[nodiscard]] FORCEINLINE bool operator!=(TWeakPtr<A, Mode> const& Lhs, std::nullptr_t)
{
    return Lhs.IsValid();
}

// ============================================================================
// Cast Functions
// ============================================================================

/** Static cast for TSharedRef */
template<class CastToType, class CastFromType, ESPMode Mode>
[[nodiscard]] FORCEINLINE TSharedRef<CastToType, Mode> StaticCastSharedRef(TSharedRef<CastFromType, Mode> const& InSharedRef)
{
    return TSharedRef<CastToType, Mode>(InSharedRef, SharedPointerInternals::FStaticCastTag());
}

/** Static cast for TSharedPtr */
template<class CastToType, class CastFromType, ESPMode Mode>
[[nodiscard]] FORCEINLINE TSharedPtr<CastToType, Mode> StaticCastSharedPtr(TSharedPtr<CastFromType, Mode> const& InSharedPtr)
{
    return TSharedPtr<CastToType, Mode>(InSharedPtr, SharedPointerInternals::FStaticCastTag());
}

/** Static cast for TWeakPtr */
template<class CastToType, class CastFromType, ESPMode Mode>
[[nodiscard]] FORCEINLINE TWeakPtr<CastToType, Mode> StaticCastWeakPtr(TWeakPtr<CastFromType, Mode> const& InWeakPtr)
{
    return TWeakPtr<CastToType, Mode>(InWeakPtr, SharedPointerInternals::FStaticCastTag());
}

/** Const cast for TSharedRef */
template<class CastToType, class CastFromType, ESPMode Mode>
[[nodiscard]] FORCEINLINE TSharedRef<CastToType, Mode> ConstCastSharedRef(TSharedRef<CastFromType, Mode> const& InSharedRef)
{
    return TSharedRef<CastToType, Mode>(InSharedRef, SharedPointerInternals::FConstCastTag());
}

/** Const cast for TSharedPtr */
template<class CastToType, class CastFromType, ESPMode Mode>
[[nodiscard]] FORCEINLINE TSharedPtr<CastToType, Mode> ConstCastSharedPtr(TSharedPtr<CastFromType, Mode> const& InSharedPtr)
{
    return TSharedPtr<CastToType, Mode>(InSharedPtr, SharedPointerInternals::FConstCastTag());
}

/** Const cast for TWeakPtr */
template<class CastToType, class CastFromType, ESPMode Mode>
[[nodiscard]] FORCEINLINE TWeakPtr<CastToType, Mode> ConstCastWeakPtr(TWeakPtr<CastFromType, Mode> const& InWeakPtr)
{
    return TWeakPtr<CastToType, Mode>(InWeakPtr, SharedPointerInternals::FConstCastTag());
}

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * MakeShareable - Wraps a raw pointer for implicit conversion to shared pointer
 */
template<class ObjectType>
[[nodiscard]] FORCEINLINE SharedPointerInternals::TRawPtrProxy<ObjectType> MakeShareable(ObjectType* InObject)
{
    return SharedPointerInternals::TRawPtrProxy<ObjectType>(InObject);
}

/**
 * MakeShareable with custom deleter
 */
template<class ObjectType, class DeleterType>
[[nodiscard]] FORCEINLINE SharedPointerInternals::TRawPtrProxyWithDeleter<ObjectType, DeleterType> 
MakeShareable(ObjectType* InObject, DeleterType&& InDeleter)
{
    return SharedPointerInternals::TRawPtrProxyWithDeleter<ObjectType, DeleterType>(
        InObject, std::forward<DeleterType>(InDeleter));
}

/**
 * MakeShared - Creates a shared reference with single allocation
 * 
 * This is the preferred way to create shared objects as it allocates
 * the object and reference controller in a single memory block.
 */
template<typename ObjectType, ESPMode Mode = ESPMode::ThreadSafe, typename... ArgTypes>
[[nodiscard]] FORCEINLINE TSharedRef<ObjectType, Mode> MakeShared(ArgTypes&&... Args)
{
    auto* Controller = SharedPointerInternals::NewIntrusiveReferenceController<Mode, ObjectType>(
        std::forward<ArgTypes>(Args)...);
    return TSharedRef<ObjectType, Mode>(
        Controller->GetObjectPtr(), 
        static_cast<SharedPointerInternals::TReferenceControllerBase<Mode>*>(Controller));
}

// ============================================================================
// Hash Functions
// ============================================================================

template<typename ObjectType, ESPMode Mode>
[[nodiscard]] uint32 GetTypeHash(TSharedRef<ObjectType, Mode> const& InSharedRef)
{
    return GetTypeHash(&InSharedRef.Get());
}

template<typename ObjectType, ESPMode Mode>
[[nodiscard]] uint32 GetTypeHash(TSharedPtr<ObjectType, Mode> const& InSharedPtr)
{
    return GetTypeHash(InSharedPtr.Get());
}

} // namespace MonsterEngine
