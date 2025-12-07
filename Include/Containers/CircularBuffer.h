// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file CircularBuffer.h
 * @brief Circular buffer (ring buffer) template class
 * 
 * This file defines TCircularBuffer, a fixed-size circular buffer that
 * uses power-of-two sizing for efficient index wrapping.
 */

#include "Core/CoreTypes.h"
#include <cstdint>
#include <type_traits>
#include <utility>

namespace MonsterEngine
{

// ============================================================================
// TCircularBuffer - Fixed-size Circular Buffer
// ============================================================================

/**
 * @brief Template for circular buffers (ring buffers)
 * 
 * The size of the buffer is rounded up to the next power of two to speed up
 * indexing operations using a simple bit mask instead of modulus operator.
 * 
 * @tparam T The element type
 */
template<typename T>
class TCircularBuffer
{
public:
    using ElementType = T;
    using SizeType = uint32_t;

public:
    // ========================================================================
    // Constructors and Destructor
    // ========================================================================

    /**
     * Constructor with capacity
     * 
     * @param InCapacity The number of elements (will be rounded up to power of 2)
     */
    explicit TCircularBuffer(SizeType InCapacity)
        : IndexMask(0)
        , Elements(nullptr)
        , Capacity(0)
    {
        // Round up to next power of two
        SizeType RoundedCapacity = RoundUpToPowerOfTwo(InCapacity);
        
        // Allocate storage
        Elements = static_cast<ElementType*>(
            FMemory::Malloc(RoundedCapacity * sizeof(ElementType), alignof(ElementType))
        );
        
        // Default construct all elements
        for (SizeType i = 0; i < RoundedCapacity; ++i)
        {
            new (Elements + i) ElementType();
        }
        
        Capacity = RoundedCapacity;
        IndexMask = RoundedCapacity - 1;
    }

    /**
     * Constructor with capacity and initial value
     * 
     * @param InCapacity The number of elements (will be rounded up to power of 2)
     * @param InitialValue The initial value for all elements
     */
    TCircularBuffer(SizeType InCapacity, const ElementType& InitialValue)
        : IndexMask(0)
        , Elements(nullptr)
        , Capacity(0)
    {
        SizeType RoundedCapacity = RoundUpToPowerOfTwo(InCapacity);
        
        Elements = static_cast<ElementType*>(
            FMemory::Malloc(RoundedCapacity * sizeof(ElementType), alignof(ElementType))
        );
        
        for (SizeType i = 0; i < RoundedCapacity; ++i)
        {
            new (Elements + i) ElementType(InitialValue);
        }
        
        Capacity = RoundedCapacity;
        IndexMask = RoundedCapacity - 1;
    }

    /** Destructor */
    ~TCircularBuffer()
    {
        if (Elements)
        {
            for (SizeType i = 0; i < Capacity; ++i)
            {
                Elements[i].~ElementType();
            }
            FMemory::Free(Elements);
            Elements = nullptr;
        }
    }

    /** Non-copyable */
    TCircularBuffer(const TCircularBuffer&) = delete;
    TCircularBuffer& operator=(const TCircularBuffer&) = delete;

    /** Move constructor */
    TCircularBuffer(TCircularBuffer&& Other) noexcept
        : IndexMask(Other.IndexMask)
        , Elements(Other.Elements)
        , Capacity(Other.Capacity)
    {
        Other.IndexMask = 0;
        Other.Elements = nullptr;
        Other.Capacity = 0;
    }

    /** Move assignment */
    TCircularBuffer& operator=(TCircularBuffer&& Other) noexcept
    {
        if (this != &Other)
        {
            // Clean up existing
            if (Elements)
            {
                for (SizeType i = 0; i < Capacity; ++i)
                {
                    Elements[i].~ElementType();
                }
                FMemory::Free(Elements);
            }
            
            IndexMask = Other.IndexMask;
            Elements = Other.Elements;
            Capacity = Other.Capacity;
            
            Other.IndexMask = 0;
            Other.Elements = nullptr;
            Other.Capacity = 0;
        }
        return *this;
    }

public:
    // ========================================================================
    // Element Access
    // ========================================================================

    /**
     * Access element at index (wraps around)
     * 
     * @param Index The index (will be wrapped to buffer size)
     */
    ElementType& operator[](SizeType Index)
    {
        return Elements[Index & IndexMask];
    }

    const ElementType& operator[](SizeType Index) const
    {
        return Elements[Index & IndexMask];
    }

public:
    // ========================================================================
    // Capacity
    // ========================================================================

    /**
     * Get the buffer capacity
     * 
     * @return The number of elements the buffer can hold
     */
    SizeType GetCapacity() const
    {
        return Capacity;
    }

    /**
     * Get the index mask for wrapping
     * 
     * @return The bit mask used for index wrapping
     */
    SizeType GetIndexMask() const
    {
        return IndexMask;
    }

public:
    // ========================================================================
    // Index Operations
    // ========================================================================

    /**
     * Calculate the next index (wraps around)
     * 
     * @param CurrentIndex The current index
     * @return The next index
     */
    SizeType GetNextIndex(SizeType CurrentIndex) const
    {
        return (CurrentIndex + 1) & IndexMask;
    }

    /**
     * Calculate the previous index (wraps around)
     * 
     * @param CurrentIndex The current index
     * @return The previous index
     */
    SizeType GetPreviousIndex(SizeType CurrentIndex) const
    {
        return (CurrentIndex - 1) & IndexMask;
    }

    /**
     * Wrap an index to valid range
     * 
     * @param Index The index to wrap
     * @return The wrapped index
     */
    SizeType WrapIndex(SizeType Index) const
    {
        return Index & IndexMask;
    }

private:
    /** Round up to next power of two */
    static SizeType RoundUpToPowerOfTwo(SizeType Value)
    {
        if (Value == 0)
        {
            return 1;
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
    /** Bit mask for index wrapping (Capacity - 1) */
    SizeType IndexMask;
    
    /** Pointer to element storage */
    ElementType* Elements;
    
    /** Buffer capacity (always power of 2) */
    SizeType Capacity;
};

// ============================================================================
// TCircularQueue - Circular Queue with Head/Tail Tracking
// ============================================================================

/**
 * @brief A circular queue with explicit head and tail tracking
 * 
 * Unlike TCircularBuffer which is just indexed storage, TCircularQueue
 * maintains head and tail pointers for FIFO queue operations.
 * 
 * @tparam T The element type
 */
template<typename T>
class TCircularQueue
{
public:
    using ElementType = T;
    using SizeType = uint32_t;

public:
    /**
     * Constructor with capacity
     * 
     * @param InCapacity The maximum number of elements
     */
    explicit TCircularQueue(SizeType InCapacity)
        : Buffer(InCapacity + 1)  // +1 to distinguish full from empty
        , Head(0)
        , Tail(0)
    {
    }

    /** Check if queue is empty */
    bool IsEmpty() const
    {
        return Head == Tail;
    }

    /** Check if queue is full */
    bool IsFull() const
    {
        return Buffer.GetNextIndex(Tail) == Head;
    }

    /** Get number of elements */
    SizeType Num() const
    {
        if (Tail >= Head)
        {
            return Tail - Head;
        }
        return Buffer.GetCapacity() - Head + Tail;
    }

    /** Get maximum capacity */
    SizeType Max() const
    {
        return Buffer.GetCapacity() - 1;  // -1 because one slot is always empty
    }

    /**
     * Add element to queue (copy)
     * 
     * @param Item The item to add
     * @return true if added, false if queue is full
     */
    bool Enqueue(const ElementType& Item)
    {
        if (IsFull())
        {
            return false;
        }
        
        Buffer[Tail] = Item;
        Tail = Buffer.GetNextIndex(Tail);
        return true;
    }

    /**
     * Add element to queue (move)
     * 
     * @param Item The item to add
     * @return true if added, false if queue is full
     */
    bool Enqueue(ElementType&& Item)
    {
        if (IsFull())
        {
            return false;
        }
        
        Buffer[Tail] = std::move(Item);
        Tail = Buffer.GetNextIndex(Tail);
        return true;
    }

    /**
     * Remove and return element from queue
     * 
     * @param OutItem Will hold the removed item
     * @return true if removed, false if queue is empty
     */
    bool Dequeue(ElementType& OutItem)
    {
        if (IsEmpty())
        {
            return false;
        }
        
        OutItem = std::move(Buffer[Head]);
        Head = Buffer.GetNextIndex(Head);
        return true;
    }

    /**
     * Peek at front element without removing
     * 
     * @param OutItem Will hold the peeked item
     * @return true if peeked, false if queue is empty
     */
    bool Peek(ElementType& OutItem) const
    {
        if (IsEmpty())
        {
            return false;
        }
        
        OutItem = Buffer[Head];
        return true;
    }

    /** Clear all elements */
    void Empty()
    {
        Head = 0;
        Tail = 0;
    }

private:
    /** The underlying circular buffer */
    TCircularBuffer<ElementType> Buffer;
    
    /** Index of first element */
    SizeType Head;
    
    /** Index after last element */
    SizeType Tail;
};

} // namespace MonsterEngine
