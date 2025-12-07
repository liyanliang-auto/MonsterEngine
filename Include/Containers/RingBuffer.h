// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file RingBuffer.h
 * @brief Ring buffer (circular buffer) with FIFO semantics
 * 
 * TRingBuffer provides a fixed-capacity circular buffer with:
 * - O(1) push/pop operations
 * - Automatic overwrite when full (optional)
 * - Thread-safe version available
 */

#include "Core/CoreTypes.h"
#include "Core/HAL/FMemory.h"
#include <atomic>
#include <utility>
#include <type_traits>

namespace MonsterEngine
{

// ============================================================================
// ERingBufferMode
// ============================================================================

/**
 * Ring buffer overflow behavior
 */
enum class ERingBufferMode
{
    /** Reject new items when full */
    Bounded,
    /** Overwrite oldest items when full */
    Overwrite
};

// ============================================================================
// TRingBuffer - Ring Buffer with FIFO Semantics
// ============================================================================

/**
 * @brief Fixed-capacity ring buffer
 * 
 * A circular buffer that maintains FIFO order. When full, behavior
 * depends on the mode: either reject new items or overwrite oldest.
 * 
 * @tparam T The element type
 * @tparam Mode The overflow behavior (default: Bounded)
 */
template<typename T, ERingBufferMode Mode = ERingBufferMode::Bounded>
class TRingBuffer
{
public:
    using ElementType = T;
    using SizeType = uint32;

public:
    // ========================================================================
    // Constructors and Destructor
    // ========================================================================

    /** Constructor with capacity */
    explicit TRingBuffer(SizeType InCapacity)
        : Capacity(InCapacity)
        , Head(0)
        , Tail(0)
        , Count(0)
    {
        Data = static_cast<T*>(FMemory::Malloc(Capacity * sizeof(T), alignof(T)));
    }

    /** Destructor */
    ~TRingBuffer()
    {
        Clear();
        if (Data)
        {
            FMemory::Free(Data);
            Data = nullptr;
        }
    }

    /** Non-copyable */
    TRingBuffer(const TRingBuffer&) = delete;
    TRingBuffer& operator=(const TRingBuffer&) = delete;

    /** Move constructor */
    TRingBuffer(TRingBuffer&& Other) noexcept
        : Data(Other.Data)
        , Capacity(Other.Capacity)
        , Head(Other.Head)
        , Tail(Other.Tail)
        , Count(Other.Count)
    {
        Other.Data = nullptr;
        Other.Capacity = 0;
        Other.Head = 0;
        Other.Tail = 0;
        Other.Count = 0;
    }

    /** Move assignment */
    TRingBuffer& operator=(TRingBuffer&& Other) noexcept
    {
        if (this != &Other)
        {
            Clear();
            if (Data)
            {
                FMemory::Free(Data);
            }

            Data = Other.Data;
            Capacity = Other.Capacity;
            Head = Other.Head;
            Tail = Other.Tail;
            Count = Other.Count;

            Other.Data = nullptr;
            Other.Capacity = 0;
            Other.Head = 0;
            Other.Tail = 0;
            Other.Count = 0;
        }
        return *this;
    }

public:
    // ========================================================================
    // Capacity
    // ========================================================================

    /** Check if buffer is empty */
    bool IsEmpty() const { return Count == 0; }

    /** Check if buffer is full */
    bool IsFull() const { return Count == Capacity; }

    /** Get number of elements */
    SizeType Num() const { return Count; }

    /** Get maximum capacity */
    SizeType Max() const { return Capacity; }

    /** Get available space */
    SizeType Available() const { return Capacity - Count; }

public:
    // ========================================================================
    // Element Access
    // ========================================================================

    /** Access front element (oldest) */
    T& Front()
    {
        return Data[Head];
    }

    const T& Front() const
    {
        return Data[Head];
    }

    /** Access back element (newest) */
    T& Back()
    {
        SizeType BackIndex = (Tail == 0) ? Capacity - 1 : Tail - 1;
        return Data[BackIndex];
    }

    const T& Back() const
    {
        SizeType BackIndex = (Tail == 0) ? Capacity - 1 : Tail - 1;
        return Data[BackIndex];
    }

    /** Access element by index (0 = oldest) */
    T& operator[](SizeType Index)
    {
        return Data[(Head + Index) % Capacity];
    }

    const T& operator[](SizeType Index) const
    {
        return Data[(Head + Index) % Capacity];
    }

public:
    // ========================================================================
    // Modifiers
    // ========================================================================

    /**
     * Push element to back (copy)
     * @return true if pushed, false if full (Bounded mode only)
     */
    bool Push(const T& Item)
    {
        return Emplace(Item);
    }

    /**
     * Push element to back (move)
     * @return true if pushed, false if full (Bounded mode only)
     */
    bool Push(T&& Item)
    {
        return Emplace(std::move(Item));
    }

    /**
     * Construct element at back
     * @return true if pushed, false if full (Bounded mode only)
     */
    template<typename... Args>
    bool Emplace(Args&&... InArgs)
    {
        if constexpr (Mode == ERingBufferMode::Bounded)
        {
            if (IsFull())
            {
                return false;
            }
        }
        else // Overwrite mode
        {
            if (IsFull())
            {
                // Destroy oldest element
                Data[Head].~T();
                Head = (Head + 1) % Capacity;
                --Count;
            }
        }

        // Construct new element at tail
        new (&Data[Tail]) T(std::forward<Args>(InArgs)...);
        Tail = (Tail + 1) % Capacity;
        ++Count;

        return true;
    }

    /**
     * Pop element from front
     * @return true if popped, false if empty
     */
    bool Pop()
    {
        if (IsEmpty())
        {
            return false;
        }

        Data[Head].~T();
        Head = (Head + 1) % Capacity;
        --Count;

        return true;
    }

    /**
     * Pop element from front and return it
     * @param OutItem Will hold the popped item
     * @return true if popped, false if empty
     */
    bool Pop(T& OutItem)
    {
        if (IsEmpty())
        {
            return false;
        }

        OutItem = std::move(Data[Head]);
        Data[Head].~T();
        Head = (Head + 1) % Capacity;
        --Count;

        return true;
    }

    /**
     * Peek at front element without removing
     * @param OutItem Will hold the peeked item
     * @return true if peeked, false if empty
     */
    bool Peek(T& OutItem) const
    {
        if (IsEmpty())
        {
            return false;
        }

        OutItem = Data[Head];
        return true;
    }

    /** Clear all elements */
    void Clear()
    {
        while (!IsEmpty())
        {
            Pop();
        }
        Head = 0;
        Tail = 0;
    }

public:
    // ========================================================================
    // Iterator Support
    // ========================================================================

    class FIterator
    {
    public:
        FIterator(TRingBuffer* InBuffer, SizeType InIndex)
            : Buffer(InBuffer), Index(InIndex)
        {
        }

        T& operator*() { return (*Buffer)[Index]; }
        const T& operator*() const { return (*Buffer)[Index]; }

        FIterator& operator++()
        {
            ++Index;
            return *this;
        }

        bool operator==(const FIterator& Other) const
        {
            return Buffer == Other.Buffer && Index == Other.Index;
        }

        bool operator!=(const FIterator& Other) const
        {
            return !(*this == Other);
        }

    private:
        TRingBuffer* Buffer;
        SizeType Index;
    };

    FIterator begin() { return FIterator(this, 0); }
    FIterator end() { return FIterator(this, Count); }

private:
    /** Pointer to data storage */
    T* Data;

    /** Maximum capacity */
    SizeType Capacity;

    /** Index of first element */
    SizeType Head;

    /** Index after last element */
    SizeType Tail;

    /** Number of elements */
    SizeType Count;
};

// ============================================================================
// Type Aliases
// ============================================================================

/** Bounded ring buffer (rejects when full) */
template<typename T>
using TBoundedRingBuffer = TRingBuffer<T, ERingBufferMode::Bounded>;

/** Overwriting ring buffer (overwrites oldest when full) */
template<typename T>
using TOverwriteRingBuffer = TRingBuffer<T, ERingBufferMode::Overwrite>;

// ============================================================================
// TLockFreeRingBuffer - Thread-safe SPSC Ring Buffer
// ============================================================================

/**
 * @brief Lock-free single-producer single-consumer ring buffer
 * 
 * Thread-safe for one producer and one consumer thread.
 * Uses atomic operations for synchronization.
 * 
 * @tparam T The element type (must be trivially copyable)
 */
template<typename T>
class TLockFreeRingBuffer
{
    static_assert(std::is_trivially_copyable_v<T>, 
        "TLockFreeRingBuffer requires trivially copyable types");

public:
    using ElementType = T;
    using SizeType = uint32;

public:
    /** Constructor with capacity (rounded up to power of 2) */
    explicit TLockFreeRingBuffer(SizeType InCapacity)
        : Capacity(RoundUpToPowerOfTwo(InCapacity))
        , Mask(Capacity - 1)
        , Head(0)
        , Tail(0)
    {
        Data = static_cast<T*>(FMemory::Malloc(Capacity * sizeof(T), alignof(T)));
    }

    /** Destructor */
    ~TLockFreeRingBuffer()
    {
        if (Data)
        {
            FMemory::Free(Data);
            Data = nullptr;
        }
    }

    /** Non-copyable, non-movable */
    TLockFreeRingBuffer(const TLockFreeRingBuffer&) = delete;
    TLockFreeRingBuffer& operator=(const TLockFreeRingBuffer&) = delete;
    TLockFreeRingBuffer(TLockFreeRingBuffer&&) = delete;
    TLockFreeRingBuffer& operator=(TLockFreeRingBuffer&&) = delete;

public:
    /** Check if empty (approximate) */
    bool IsEmpty() const
    {
        return Head.load(std::memory_order_acquire) == 
               Tail.load(std::memory_order_acquire);
    }

    /** Check if full (approximate) */
    bool IsFull() const
    {
        SizeType NextTail = (Tail.load(std::memory_order_acquire) + 1) & Mask;
        return NextTail == Head.load(std::memory_order_acquire);
    }

    /** Get approximate count */
    SizeType Num() const
    {
        SizeType H = Head.load(std::memory_order_acquire);
        SizeType T = Tail.load(std::memory_order_acquire);
        return (T >= H) ? (T - H) : (Capacity - H + T);
    }

    /** Get capacity */
    SizeType Max() const { return Capacity - 1; }  // One slot always empty

    /**
     * Push element (producer thread only)
     * @return true if pushed, false if full
     */
    bool Push(const T& Item)
    {
        SizeType CurrentTail = Tail.load(std::memory_order_relaxed);
        SizeType NextTail = (CurrentTail + 1) & Mask;

        if (NextTail == Head.load(std::memory_order_acquire))
        {
            return false;  // Full
        }

        Data[CurrentTail] = Item;
        Tail.store(NextTail, std::memory_order_release);
        return true;
    }

    /**
     * Pop element (consumer thread only)
     * @param OutItem Will hold the popped item
     * @return true if popped, false if empty
     */
    bool Pop(T& OutItem)
    {
        SizeType CurrentHead = Head.load(std::memory_order_relaxed);

        if (CurrentHead == Tail.load(std::memory_order_acquire))
        {
            return false;  // Empty
        }

        OutItem = Data[CurrentHead];
        Head.store((CurrentHead + 1) & Mask, std::memory_order_release);
        return true;
    }

private:
    /** Round up to next power of two */
    static SizeType RoundUpToPowerOfTwo(SizeType Value)
    {
        if (Value == 0) return 1;
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
    /** Pointer to data storage */
    T* Data;

    /** Capacity (power of 2) */
    SizeType Capacity;

    /** Mask for index wrapping */
    SizeType Mask;

    /** Head index (consumer) */
    alignas(64) std::atomic<SizeType> Head;

    /** Tail index (producer) */
    alignas(64) std::atomic<SizeType> Tail;
};

} // namespace MonsterEngine
