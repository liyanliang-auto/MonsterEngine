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
// Reference Controller Memory Pool
// ============================================================================

/**
 * TReferenceControllerPool - Memory pool for reference controllers
 * 
 * Provides efficient allocation/deallocation of reference controllers
 * by reusing freed memory blocks. Thread-safe for ThreadSafe mode.
 * 
 * Based on UE5's object pool patterns for high-frequency allocations.
 */
template<typename ControllerType, ESPMode Mode>
class TReferenceControllerPool
{
    // Use atomic or regular pointer based on thread safety mode
    using NodePtrType = std::conditional_t<Mode == ESPMode::ThreadSafe, 
                                            std::atomic<void*>, void*>;

    struct FreeNode
    {
        FreeNode* Next;
    };

public:
    static TReferenceControllerPool& Get()
    {
        static TReferenceControllerPool Instance;
        return Instance;
    }

    /** Allocate memory for a controller */
    void* Allocate()
    {
        if constexpr (Mode == ESPMode::ThreadSafe)
        {
            // Lock-free pop from free list
            void* Head = FreeList.load(std::memory_order_acquire);
            while (Head != nullptr)
            {
                FreeNode* Node = static_cast<FreeNode*>(Head);
                if (FreeList.compare_exchange_weak(Head, Node->Next,
                    std::memory_order_release, std::memory_order_relaxed))
                {
                    PooledCount.fetch_sub(1, std::memory_order_relaxed);
                    return Head;
                }
            }
        }
        else
        {
            if (FreeList != nullptr)
            {
                void* Result = FreeList;
                FreeList = static_cast<FreeNode*>(FreeList)->Next;
                --PooledCount;
                return Result;
            }
        }

        // No pooled memory available, allocate new
        TotalAllocated.fetch_add(1, std::memory_order_relaxed);
        return MonsterRender::FMemory::Malloc(sizeof(ControllerType), alignof(ControllerType));
    }

    /** Return memory to the pool */
    void Free(void* Ptr)
    {
        if (Ptr == nullptr)
        {
            return;
        }

        // Check if we should pool or actually free
        int32 CurrentPooled;
        if constexpr (Mode == ESPMode::ThreadSafe)
        {
            CurrentPooled = PooledCount.load(std::memory_order_relaxed);
        }
        else
        {
            CurrentPooled = PooledCount;
        }

        if (CurrentPooled >= MaxPoolSize)
        {
            // Pool is full, actually free the memory
            MonsterRender::FMemory::Free(Ptr);
            TotalAllocated.fetch_sub(1, std::memory_order_relaxed);
            return;
        }

        // Add to free list
        FreeNode* Node = static_cast<FreeNode*>(Ptr);
        
        if constexpr (Mode == ESPMode::ThreadSafe)
        {
            // Lock-free push to free list
            void* Head = FreeList.load(std::memory_order_relaxed);
            do
            {
                Node->Next = static_cast<FreeNode*>(Head);
            } while (!FreeList.compare_exchange_weak(Head, Node,
                std::memory_order_release, std::memory_order_relaxed));
            
            PooledCount.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            Node->Next = static_cast<FreeNode*>(FreeList);
            FreeList = Node;
            ++PooledCount;
        }
    }

    /** Get statistics */
    int32 GetPooledCount() const
    {
        if constexpr (Mode == ESPMode::ThreadSafe)
        {
            return PooledCount.load(std::memory_order_relaxed);
        }
        else
        {
            return PooledCount;
        }
    }

    int32 GetTotalAllocated() const
    {
        return TotalAllocated.load(std::memory_order_relaxed);
    }

    /** Set maximum pool size */
    void SetMaxPoolSize(int32 NewMaxSize)
    {
        MaxPoolSize = NewMaxSize;
    }

    /** Clear the pool (free all pooled memory) */
    void Clear()
    {
        if constexpr (Mode == ESPMode::ThreadSafe)
        {
            void* Head = FreeList.exchange(nullptr, std::memory_order_acquire);
            while (Head != nullptr)
            {
                FreeNode* Node = static_cast<FreeNode*>(Head);
                Head = Node->Next;
                MonsterRender::FMemory::Free(Node);
                TotalAllocated.fetch_sub(1, std::memory_order_relaxed);
            }
            PooledCount.store(0, std::memory_order_relaxed);
        }
        else
        {
            while (FreeList != nullptr)
            {
                FreeNode* Node = static_cast<FreeNode*>(FreeList);
                FreeList = Node->Next;
                MonsterRender::FMemory::Free(Node);
                TotalAllocated.fetch_sub(1, std::memory_order_relaxed);
            }
            PooledCount = 0;
        }
    }

private:
    TReferenceControllerPool()
        : MaxPoolSize(1024)  // Default max pool size
    {
        if constexpr (Mode == ESPMode::ThreadSafe)
        {
            FreeList.store(nullptr, std::memory_order_relaxed);
            PooledCount.store(0, std::memory_order_relaxed);
        }
        else
        {
            FreeList = nullptr;
            PooledCount = 0;
        }
        TotalAllocated.store(0, std::memory_order_relaxed);
    }

    ~TReferenceControllerPool()
    {
        Clear();
    }

    // Non-copyable
    TReferenceControllerPool(const TReferenceControllerPool&) = delete;
    TReferenceControllerPool& operator=(const TReferenceControllerPool&) = delete;

    NodePtrType FreeList;
    std::conditional_t<Mode == ESPMode::ThreadSafe, std::atomic<int32>, int32> PooledCount;
    std::atomic<int32> TotalAllocated;
    int32 MaxPoolSize;
};

// ============================================================================
// Pooled Reference Controller
// ============================================================================

/**
 * TPooledReferenceController - Reference controller that uses memory pool
 * 
 * Overrides new/delete to use the memory pool for efficient allocation.
 */
template<typename ObjectType, typename DeleterType, ESPMode Mode>
class TPooledReferenceController : private TDeleterHolder<DeleterType>, public TReferenceControllerBase<Mode>
{
    using PoolType = TReferenceControllerPool<TPooledReferenceController, Mode>;

public:
    explicit TPooledReferenceController(ObjectType* InObject, DeleterType&& InDeleter)
        : TDeleterHolder<DeleterType>(std::move(InDeleter))
        , Object(InObject)
    {
    }

    virtual void DestroyObject() override
    {
        this->InvokeDeleter(Object);
    }

    // Custom new/delete using pool
    static void* operator new(size_t Size)
    {
        return PoolType::Get().Allocate();
    }

    static void operator delete(void* Ptr)
    {
        PoolType::Get().Free(Ptr);
    }

    // Non-copyable
    TPooledReferenceController(const TPooledReferenceController&) = delete;
    TPooledReferenceController& operator=(const TPooledReferenceController&) = delete;

private:
    ObjectType* Object;
};

/**
 * TPooledIntrusiveReferenceController - Intrusive controller with pool support
 */
template<typename ObjectType, ESPMode Mode>
class TPooledIntrusiveReferenceController : public TReferenceControllerBase<Mode>
{
    using PoolType = TReferenceControllerPool<TPooledIntrusiveReferenceController, Mode>;

public:
    template<typename... ArgTypes>
    explicit TPooledIntrusiveReferenceController(ArgTypes&&... Args)
    {
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

    // Custom new/delete using pool
    static void* operator new(size_t Size)
    {
        return PoolType::Get().Allocate();
    }

    static void operator delete(void* Ptr)
    {
        PoolType::Get().Free(Ptr);
    }

    // Non-copyable
    TPooledIntrusiveReferenceController(const TPooledIntrusiveReferenceController&) = delete;
    TPooledIntrusiveReferenceController& operator=(const TPooledIntrusiveReferenceController&) = delete;

private:
    alignas(ObjectType) uint8 ObjectStorage[sizeof(ObjectType)];
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

/** Creates a pooled reference controller with default deleter (for high-frequency allocations) */
template<ESPMode Mode, typename ObjectType>
inline TReferenceControllerBase<Mode>* NewPooledReferenceController(ObjectType* Object)
{
    return new TPooledReferenceController<ObjectType, DefaultDeleter<ObjectType>, Mode>(
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

/** Creates a pooled intrusive reference controller (for MakeSharedPooled) */
template<ESPMode Mode, typename ObjectType, typename... ArgTypes>
inline TPooledIntrusiveReferenceController<ObjectType, Mode>* NewPooledIntrusiveReferenceController(ArgTypes&&... Args)
{
    return new TPooledIntrusiveReferenceController<ObjectType, Mode>(std::forward<ArgTypes>(Args)...);
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
