// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file Queue.h
 * @brief Lock-free queue template class
 * 
 * This file defines TQueue, an unbounded non-intrusive queue using a lock-free
 * linked list. Supports both MPSC (Multiple-Producer Single-Consumer) and
 * SPSC (Single-Producer Single-Consumer) modes.
 */

#include "Core/CoreTypes.h"
#include <atomic>
#include <utility>
#include <type_traits>

namespace MonsterEngine
{

// ============================================================================
// Queue Mode Enum
// ============================================================================

/**
 * Enumerates concurrent queue modes
 */
enum class EQueueMode
{
    /** Multiple-producers, single-consumer queue */
    Mpsc,
    /** Single-producer, single-consumer queue */
    Spsc
};

// ============================================================================
// TQueue - Lock-free Queue
// ============================================================================

/**
 * @brief Template for lock-free queues
 * 
 * This template implements an unbounded non-intrusive queue using a lock-free
 * linked list that stores copies of the queued items.
 * 
 * The queue is thread-safe in both modes:
 * - SPSC: Single-producer, single-consumer (default, fastest)
 * - MPSC: Multiple-producers, single-consumer
 * 
 * @tparam T The type of items stored in the queue
 * @tparam Mode The queue mode (SPSC by default)
 */
template<typename T, EQueueMode Mode = EQueueMode::Spsc>
class TQueue
{
public:
    using ElementType = T;

private:
    /** Internal node structure */
    struct TNode
    {
        /** The stored item */
        ElementType Item;
        
        /** Pointer to next node */
        TNode* NextNode;
        
        /** Default constructor */
        TNode()
            : Item()
            , NextNode(nullptr)
        {
        }
        
        /** Constructor with item */
        explicit TNode(const ElementType& InItem)
            : Item(InItem)
            , NextNode(nullptr)
        {
        }
        
        /** Move constructor */
        explicit TNode(ElementType&& InItem)
            : Item(std::move(InItem))
            , NextNode(nullptr)
        {
        }
    };

public:
    // ========================================================================
    // Constructors and Destructor
    // ========================================================================

    /** Default constructor */
    TQueue()
    {
        // Create dummy node
        Head = Tail = new TNode();
    }

    /** Destructor */
    ~TQueue()
    {
        // Delete all nodes
        while (Tail != nullptr)
        {
            TNode* Node = Tail;
            Tail = Tail->NextNode;
            delete Node;
        }
    }

    /** Non-copyable */
    TQueue(const TQueue&) = delete;
    TQueue& operator=(const TQueue&) = delete;

    /** Move constructor */
    TQueue(TQueue&& Other) noexcept
        : Head(Other.Head)
        , Tail(Other.Tail)
    {
        Other.Head = Other.Tail = new TNode();
    }

    /** Move assignment */
    TQueue& operator=(TQueue&& Other) noexcept
    {
        if (this != &Other)
        {
            // Clean up existing nodes
            while (Tail != nullptr)
            {
                TNode* Node = Tail;
                Tail = Tail->NextNode;
                delete Node;
            }
            
            Head = Other.Head;
            Tail = Other.Tail;
            Other.Head = Other.Tail = new TNode();
        }
        return *this;
    }

public:
    // ========================================================================
    // Queue Operations
    // ========================================================================

    /**
     * Adds an item to the head of the queue (copy)
     * 
     * @param Item The item to add
     * @return true if the item was added, false otherwise
     * @note To be called only from producer thread(s)
     */
    bool Enqueue(const ElementType& Item)
    {
        TNode* NewNode = new TNode(Item);
        if (NewNode == nullptr)
        {
            return false;
        }
        
        EnqueueNode(NewNode);
        return true;
    }

    /**
     * Adds an item to the head of the queue (move)
     * 
     * @param Item The item to add
     * @return true if the item was added, false otherwise
     * @note To be called only from producer thread(s)
     */
    bool Enqueue(ElementType&& Item)
    {
        TNode* NewNode = new TNode(std::move(Item));
        if (NewNode == nullptr)
        {
            return false;
        }
        
        EnqueueNode(NewNode);
        return true;
    }

    /**
     * Removes and returns the item from the tail of the queue
     * 
     * @param OutItem Will hold the returned value
     * @return true if a value was returned, false if the queue was empty
     * @note To be called only from consumer thread
     */
    bool Dequeue(ElementType& OutItem)
    {
        TNode* Popped = Tail->NextNode;
        
        if (Popped == nullptr)
        {
            return false;
        }
        
        // Memory barrier to ensure we see the item
        std::atomic_thread_fence(std::memory_order_acquire);
        
        OutItem = std::move(Popped->Item);
        
        TNode* OldTail = Tail;
        Tail = Popped;
        Tail->Item = ElementType();  // Clear the item
        delete OldTail;
        
        return true;
    }

    /**
     * Peeks at the item at the tail of the queue without removing it
     * 
     * @param OutItem Will hold the peeked value
     * @return true if a value was peeked, false if the queue was empty
     * @note To be called only from consumer thread
     */
    bool Peek(ElementType& OutItem) const
    {
        TNode* Popped = Tail->NextNode;
        
        if (Popped == nullptr)
        {
            return false;
        }
        
        std::atomic_thread_fence(std::memory_order_acquire);
        OutItem = Popped->Item;
        
        return true;
    }

    /**
     * Removes the item at the tail of the queue without returning it
     * 
     * @return true if an item was removed, false if the queue was empty
     * @note To be called only from consumer thread
     */
    bool Pop()
    {
        TNode* Popped = Tail->NextNode;
        
        if (Popped == nullptr)
        {
            return false;
        }
        
        TNode* OldTail = Tail;
        Tail = Popped;
        Tail->Item = ElementType();
        delete OldTail;
        
        return true;
    }

    /**
     * Empty the queue, discarding all items
     * 
     * @note To be called only from consumer thread
     */
    void Empty()
    {
        while (Pop())
        {
            // Keep popping until empty
        }
    }

    /**
     * Check if the queue is empty
     * 
     * @return true if the queue is empty
     * @note This is a snapshot and may change immediately after returning
     */
    bool IsEmpty() const
    {
        return Tail->NextNode == nullptr;
    }

private:
    /** Internal enqueue helper */
    void EnqueueNode(TNode* NewNode)
    {
        TNode* OldHead;
        
        if constexpr (Mode == EQueueMode::Mpsc)
        {
            // Multiple producers - use atomic exchange
            OldHead = reinterpret_cast<TNode*>(
                std::atomic_exchange(
                    reinterpret_cast<std::atomic<TNode*>*>(&Head),
                    NewNode
                )
            );
            
            // Memory barrier before linking
            std::atomic_thread_fence(std::memory_order_release);
            
            // Atomically set the next pointer
            std::atomic_store(
                reinterpret_cast<std::atomic<TNode*>*>(&OldHead->NextNode),
                NewNode
            );
        }
        else
        {
            // Single producer - no atomics needed for head update
            OldHead = Head;
            Head = NewNode;
            
            // Memory barrier before linking
            std::atomic_thread_fence(std::memory_order_release);
            
            OldHead->NextNode = NewNode;
        }
    }

private:
    /** Pointer to the head (newest item) */
    TNode* Head;
    
    /** Pointer to the tail (oldest item, dummy node) */
    TNode* Tail;
};

// ============================================================================
// Type Aliases
// ============================================================================

/** Single-producer single-consumer queue */
template<typename T>
using TSpscQueue = TQueue<T, EQueueMode::Spsc>;

/** Multiple-producer single-consumer queue */
template<typename T>
using TMpscQueue = TQueue<T, EQueueMode::Mpsc>;

} // namespace MonsterEngine
