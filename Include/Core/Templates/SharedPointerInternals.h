// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file SharedPointerInternals.h
 * @brief Internal implementation details for the smart pointer system
 * 
 * This file contains the reference counting infrastructure used by TSharedPtr,
 * TSharedRef, and TWeakPtr. Based on UE5's SharedPointerInternals.h.
 * 
 * Key components:
 * - TReferenceControllerBase: Base class for reference counting
 * - FSharedReferencer: Wrapper for shared reference counting
 * - FWeakReferencer: Wrapper for weak reference counting
 */

#include "Core/CoreTypes.h"
#include "Core/Templates/SharedPointerFwd.h"
#include "Core/HAL/FMemory.h"

#include <atomic>
#include <type_traits>
#include <utility>

namespace MonsterEngine
{

// Forward declarations
template<class ObjectType, ESPMode Mode> class TSharedRef;
template<class ObjectType, ESPMode Mode> class TSharedPtr;
template<class ObjectType, ESPMode Mode> class TWeakPtr;

namespace SharedPointerInternals
{

// Forward declarations
template<ESPMode Mode> class FWeakReferencer;
template<ESPMode Mode> class FSharedReferencer;

/** Dummy structures used internally as template arguments for typecasts */
struct FStaticCastTag {};
struct FConstCastTag {};
struct FNullTag {};

// ============================================================================
// Reference Controller Base
// ============================================================================

/**
 * TReferenceControllerBase - Base class for reference counting
 * 
 * Manages both shared and weak reference counts. The shared count controls
 * object lifetime, while the weak count controls controller lifetime.
 */
template<ESPMode Mode>
class TReferenceControllerBase
{
    // Use atomic or regular int32 based on thread safety mode
    using RefCountType = std::conditional_t<Mode == ESPMode::ThreadSafe, std::atomic<int32>, int32>;

public:
    FORCEINLINE explicit TReferenceControllerBase() = default;

    // Non-copyable
    TReferenceControllerBase(const TReferenceControllerBase&) = delete;
    TReferenceControllerBase& operator=(const TReferenceControllerBase&) = delete;

    virtual ~TReferenceControllerBase() = default;

    /** Destroys the object associated with this reference counter */
    virtual void DestroyObject() = 0;

    /** Returns the shared reference count */
    FORCEINLINE int32 GetSharedReferenceCount() const
    {
        if constexpr (Mode == ESPMode::ThreadSafe)
        {
            return SharedReferenceCount.load(std::memory_order_relaxed);
        }
        else
        {
            return SharedReferenceCount;
        }
    }

    /** Checks if there is exactly one shared reference left */
    FORCEINLINE bool IsUnique() const
    {
        if constexpr (Mode == ESPMode::ThreadSafe)
        {
            return SharedReferenceCount.load(std::memory_order_acquire) == 1;
        }
        else
        {
            return SharedReferenceCount == 1;
        }
    }

    /** Adds a shared reference to this counter */
    FORCEINLINE void AddSharedReference()
    {
        if constexpr (Mode == ESPMode::ThreadSafe)
        {
            SharedReferenceCount.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            ++SharedReferenceCount;
        }
    }

    /**
     * Adds a shared reference only if there is already at least one reference
     * @return True if the shared reference was added successfully
     */
    bool ConditionallyAddSharedReference()
    {
        if constexpr (Mode == ESPMode::ThreadSafe)
        {
            int32 OriginalCount = SharedReferenceCount.load(std::memory_order_relaxed);
            for (;;)
            {
                if (OriginalCount == 0)
                {
                    return false;
                }
                if (SharedReferenceCount.compare_exchange_weak(OriginalCount, OriginalCount + 1, std::memory_order_relaxed))
                {
                    return true;
                }
            }
        }
        else
        {
            if (SharedReferenceCount == 0)
            {
                return false;
            }
            ++SharedReferenceCount;
            return true;
        }
    }

    /** Releases a shared reference to this counter */
    FORCEINLINE void ReleaseSharedReference()
    {
        if constexpr (Mode == ESPMode::ThreadSafe)
        {
            int32 OldSharedCount = SharedReferenceCount.fetch_sub(1, std::memory_order_acq_rel);
            if (OldSharedCount == 1)
            {
                DestroyObject();
                ReleaseWeakReference();
            }
        }
        else
        {
            if (--SharedReferenceCount == 0)
            {
                DestroyObject();
                ReleaseWeakReference();
            }
        }
    }

    /** Adds a weak reference to this counter */
    FORCEINLINE void AddWeakReference()
    {
        if constexpr (Mode == ESPMode::ThreadSafe)
        {
            WeakReferenceCount.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            ++WeakReferenceCount;
        }
    }

    /** Releases a weak reference to this counter */
    void ReleaseWeakReference()
    {
        if constexpr (Mode == ESPMode::ThreadSafe)
        {
            int32 OldWeakCount = WeakReferenceCount.fetch_sub(1, std::memory_order_acq_rel);
            if (OldWeakCount == 1)
            {
                delete this;
            }
        }
        else
        {
            if (--WeakReferenceCount == 0)
            {
                delete this;
            }
        }
    }

private:
    // Shared reference count starts at 1 (created with first shared reference)
    RefCountType SharedReferenceCount{1};
    
    // Weak reference count starts at 1 (represents the shared references)
    RefCountType WeakReferenceCount{1};
};

// ============================================================================
// Deleter Holder (Empty Base Optimization)
// ============================================================================

/**
 * TDeleterHolder - Stores a custom deleter with empty base optimization
 */
template<typename DeleterType, bool bIsZeroSize = std::is_empty_v<DeleterType>>
struct TDeleterHolder : private DeleterType
{
    explicit TDeleterHolder(DeleterType&& InDeleter)
        : DeleterType(std::move(InDeleter))
    {
    }

    template<typename ObjectType>
    void InvokeDeleter(ObjectType* Object)
    {
        (*static_cast<DeleterType*>(this))(Object);
    }
};

template<typename DeleterType>
struct TDeleterHolder<DeleterType, false>
{
    explicit TDeleterHolder(DeleterType&& InDeleter)
        : Deleter(std::move(InDeleter))
    {
    }

    template<typename ObjectType>
    void InvokeDeleter(ObjectType* Object)
    {
        Deleter(Object);
    }

private:
    DeleterType Deleter;
};

// ============================================================================
// Reference Controller Implementations
// ============================================================================

/**
 * TReferenceControllerWithDeleter - Reference controller with custom deleter
 */
template<typename ObjectType, typename DeleterType, ESPMode Mode>
class TReferenceControllerWithDeleter : private TDeleterHolder<DeleterType>, public TReferenceControllerBase<Mode>
{
public:
    explicit TReferenceControllerWithDeleter(ObjectType* InObject, DeleterType&& InDeleter)
        : TDeleterHolder<DeleterType>(std::move(InDeleter))
        , Object(InObject)
    {
    }

    virtual void DestroyObject() override
    {
        this->InvokeDeleter(Object);
    }

    // Non-copyable
    TReferenceControllerWithDeleter(const TReferenceControllerWithDeleter&) = delete;
    TReferenceControllerWithDeleter& operator=(const TReferenceControllerWithDeleter&) = delete;

private:
    ObjectType* Object;
};

/**
 * TIntrusiveReferenceController - Reference controller with inline object storage
 * 
 * Used by MakeShared to allocate object and controller in a single allocation.
 */
template<typename ObjectType, ESPMode Mode>
class TIntrusiveReferenceController : public TReferenceControllerBase<Mode>
{
public:
    template<typename... ArgTypes>
    explicit TIntrusiveReferenceController(ArgTypes&&... Args)
    {
        // Construct object in-place
        new (GetObjectPtr()) ObjectType(std::forward<ArgTypes>(Args)...);
    }

    ObjectType* GetObjectPtr() const
    {
        return reinterpret_cast<ObjectType*>(const_cast<uint8*>(ObjectStorage));
    }

    virtual void DestroyObject() override
    {
        GetObjectPtr()->~ObjectType();
    }

    // Non-copyable
    TIntrusiveReferenceController(const TIntrusiveReferenceController&) = delete;
    TIntrusiveReferenceController& operator=(const TIntrusiveReferenceController&) = delete;

private:
    alignas(ObjectType) uint8 ObjectStorage[sizeof(ObjectType)];
};

// ============================================================================
// Default Deleter
// ============================================================================

/** Default deleter that uses delete operator */
template<typename Type>
struct DefaultDeleter
{
    FORCEINLINE void operator()(Type* Object) const
    {
        delete Object;
    }
};

// ============================================================================
// Factory Functions
// ============================================================================

/** Creates a reference controller with default deleter */
template<ESPMode Mode, typename ObjectType>
inline TReferenceControllerBase<Mode>* NewDefaultReferenceController(ObjectType* Object)
{
    return new TReferenceControllerWithDeleter<ObjectType, DefaultDeleter<ObjectType>, Mode>(
        Object, DefaultDeleter<ObjectType>());
}

/** Creates a reference controller with custom deleter */
template<ESPMode Mode, typename ObjectType, typename DeleterType>
inline TReferenceControllerBase<Mode>* NewCustomReferenceController(ObjectType* Object, DeleterType&& Deleter)
{
    return new TReferenceControllerWithDeleter<ObjectType, std::remove_reference_t<DeleterType>, Mode>(
        Object, std::forward<DeleterType>(Deleter));
}

/** Creates an intrusive reference controller (for MakeShared) */
template<ESPMode Mode, typename ObjectType, typename... ArgTypes>
inline TIntrusiveReferenceController<ObjectType, Mode>* NewIntrusiveReferenceController(ArgTypes&&... Args)
{
    return new TIntrusiveReferenceController<ObjectType, Mode>(std::forward<ArgTypes>(Args)...);
}

// ============================================================================
// Raw Pointer Proxy (for MakeShareable)
// ============================================================================

/** Proxy structure for implicitly converting raw pointers to shared pointers */
template<class ObjectType>
struct TRawPtrProxy
{
    ObjectType* Object;

    FORCEINLINE TRawPtrProxy(std::nullptr_t)
        : Object(nullptr)
    {
    }

    explicit FORCEINLINE TRawPtrProxy(ObjectType* InObject)
        : Object(InObject)
    {
    }
};

/** Proxy structure with custom deleter */
template<class ObjectType, typename DeleterType>
struct TRawPtrProxyWithDeleter
{
    ObjectType* Object;
    DeleterType Deleter;

    FORCEINLINE TRawPtrProxyWithDeleter(ObjectType* InObject, DeleterType&& InDeleter)
        : Object(InObject)
        , Deleter(std::move(InDeleter))
    {
    }
};

// ============================================================================
// FSharedReferencer - Shared Reference Counter Wrapper
// ============================================================================

/**
 * FSharedReferencer - Wrapper around a pointer to a reference controller
 * 
 * Used by TSharedRef and TSharedPtr to manage the reference count.
 */
template<ESPMode Mode>
class FSharedReferencer
{
public:
    /** Constructor for an empty shared referencer */
    FORCEINLINE FSharedReferencer()
        : ReferenceController(nullptr)
    {
    }

    /** Constructor that counts a single reference */
    explicit FORCEINLINE FSharedReferencer(TReferenceControllerBase<Mode>* InReferenceController)
        : ReferenceController(InReferenceController)
    {
    }

    /** Copy constructor - adds a reference */
    FORCEINLINE FSharedReferencer(const FSharedReferencer& InSharedReference)
        : ReferenceController(InSharedReference.ReferenceController)
    {
        if (ReferenceController != nullptr)
        {
            ReferenceController->AddSharedReference();
        }
    }

    /** Move constructor - no new references */
    FORCEINLINE FSharedReferencer(FSharedReferencer&& InSharedReference)
        : ReferenceController(InSharedReference.ReferenceController)
    {
        InSharedReference.ReferenceController = nullptr;
    }

    /** Constructor from weak referencer - may fail if object expired */
    FSharedReferencer(const FWeakReferencer<Mode>& InWeakReference)
        : ReferenceController(InWeakReference.ReferenceController)
    {
        if (ReferenceController != nullptr)
        {
            if (!ReferenceController->ConditionallyAddSharedReference())
            {
                ReferenceController = nullptr;
            }
        }
    }

    /** Move constructor from weak referencer */
    FSharedReferencer(FWeakReferencer<Mode>&& InWeakReference)
        : ReferenceController(InWeakReference.ReferenceController)
    {
        if (ReferenceController != nullptr)
        {
            if (!ReferenceController->ConditionallyAddSharedReference())
            {
                ReferenceController = nullptr;
            }
            InWeakReference.ReferenceController->ReleaseWeakReference();
            InWeakReference.ReferenceController = nullptr;
        }
    }

    /** Destructor - releases shared reference */
    FORCEINLINE ~FSharedReferencer()
    {
        if (ReferenceController != nullptr)
        {
            ReferenceController->ReleaseSharedReference();
        }
    }

    /** Copy assignment */
    FSharedReferencer& operator=(const FSharedReferencer& InSharedReference)
    {
        auto NewReferenceController = InSharedReference.ReferenceController;
        if (NewReferenceController != ReferenceController)
        {
            if (NewReferenceController != nullptr)
            {
                NewReferenceController->AddSharedReference();
            }
            if (ReferenceController != nullptr)
            {
                ReferenceController->ReleaseSharedReference();
            }
            ReferenceController = NewReferenceController;
        }
        return *this;
    }

    /** Move assignment */
    FSharedReferencer& operator=(FSharedReferencer&& InSharedReference)
    {
        auto NewReferenceController = InSharedReference.ReferenceController;
        auto OldReferenceController = ReferenceController;
        if (NewReferenceController != OldReferenceController)
        {
            InSharedReference.ReferenceController = nullptr;
            ReferenceController = NewReferenceController;
            if (OldReferenceController != nullptr)
            {
                OldReferenceController->ReleaseSharedReference();
            }
        }
        return *this;
    }

    /** Tests if this shared counter contains a valid reference */
    FORCEINLINE bool IsValid() const
    {
        return ReferenceController != nullptr;
    }

    /** Returns the number of shared references */
    FORCEINLINE int32 GetSharedReferenceCount() const
    {
        return ReferenceController != nullptr ? ReferenceController->GetSharedReferenceCount() : 0;
    }

    /** Returns true if this is the only shared reference */
    FORCEINLINE bool IsUnique() const
    {
        return ReferenceController != nullptr && ReferenceController->IsUnique();
    }

private:
    template<ESPMode OtherMode> friend class FWeakReferencer;

    TReferenceControllerBase<Mode>* ReferenceController;
};

// ============================================================================
// FWeakReferencer - Weak Reference Counter Wrapper
// ============================================================================

/**
 * FWeakReferencer - Wrapper for weak reference counting
 * 
 * Used by TWeakPtr to track object lifetime without preventing destruction.
 */
template<ESPMode Mode>
class FWeakReferencer
{
public:
    /** Default constructor */
    FORCEINLINE FWeakReferencer()
        : ReferenceController(nullptr)
    {
    }

    /** Copy constructor from another weak referencer */
    FORCEINLINE FWeakReferencer(const FWeakReferencer& InWeakReference)
        : ReferenceController(InWeakReference.ReferenceController)
    {
        if (ReferenceController != nullptr)
        {
            ReferenceController->AddWeakReference();
        }
    }

    /** Move constructor */
    FORCEINLINE FWeakReferencer(FWeakReferencer&& InWeakReference)
        : ReferenceController(InWeakReference.ReferenceController)
    {
        InWeakReference.ReferenceController = nullptr;
    }

    /** Constructor from shared referencer */
    FORCEINLINE FWeakReferencer(const FSharedReferencer<Mode>& InSharedReference)
        : ReferenceController(InSharedReference.ReferenceController)
    {
        if (ReferenceController != nullptr)
        {
            ReferenceController->AddWeakReference();
        }
    }

    /** Destructor */
    FORCEINLINE ~FWeakReferencer()
    {
        if (ReferenceController != nullptr)
        {
            ReferenceController->ReleaseWeakReference();
        }
    }

    /** Copy assignment from weak referencer */
    FWeakReferencer& operator=(const FWeakReferencer& InWeakReference)
    {
        AssignReferenceController(InWeakReference.ReferenceController);
        return *this;
    }

    /** Move assignment */
    FWeakReferencer& operator=(FWeakReferencer&& InWeakReference)
    {
        auto OldReferenceController = ReferenceController;
        ReferenceController = InWeakReference.ReferenceController;
        InWeakReference.ReferenceController = nullptr;
        if (OldReferenceController != nullptr)
        {
            OldReferenceController->ReleaseWeakReference();
        }
        return *this;
    }

    /** Assignment from shared referencer */
    FWeakReferencer& operator=(const FSharedReferencer<Mode>& InSharedReference)
    {
        AssignReferenceController(InSharedReference.ReferenceController);
        return *this;
    }

    /** Tests if the weak reference is still valid */
    FORCEINLINE bool IsValid() const
    {
        return ReferenceController != nullptr && ReferenceController->GetSharedReferenceCount() > 0;
    }

private:
    template<ESPMode OtherMode> friend class FSharedReferencer;

    void AssignReferenceController(TReferenceControllerBase<Mode>* NewReferenceController)
    {
        if (NewReferenceController != ReferenceController)
        {
            if (NewReferenceController != nullptr)
            {
                NewReferenceController->AddWeakReference();
            }
            if (ReferenceController != nullptr)
            {
                ReferenceController->ReleaseWeakReference();
            }
            ReferenceController = NewReferenceController;
        }
    }

    TReferenceControllerBase<Mode>* ReferenceController;
};

// ============================================================================
// EnableSharedFromThis Helper Functions
// ============================================================================

/** Helper to enable TSharedFromThis functionality */
template<class SharedPtrType, class ObjectType, class OtherType, ESPMode Mode>
FORCEINLINE void EnableSharedFromThis(TSharedPtr<SharedPtrType, Mode> const* InSharedPtr, 
                                       ObjectType const* InObject, 
                                       TSharedFromThis<OtherType, Mode> const* InShareable);

template<class SharedRefType, class ObjectType, class OtherType, ESPMode Mode>
FORCEINLINE void EnableSharedFromThis(TSharedRef<SharedRefType, Mode> const* InSharedRef,
                                       ObjectType const* InObject,
                                       TSharedFromThis<OtherType, Mode> const* InShareable);

/** Catch-all for types that don't inherit from TSharedFromThis */
FORCEINLINE void EnableSharedFromThis(...) {}

} // namespace SharedPointerInternals

} // namespace MonsterEngine
