// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file Deque.h
 * @brief Double-ended queue (deque) template class
 * 
 * This file defines TDeque, a dynamically sized sequential container that
 * allows efficient insertion and removal at both ends.
 */

#include "Core/CoreTypes.h"
#include "Core/HAL/FMemory.h"
#include "Containers/Array.h"
#include <initializer_list>
#include <utility>
#include <algorithm>
#include <iterator>

namespace MonsterEngine
{

// ============================================================================
// TDeque - Double-ended Queue
// ============================================================================

/**
 * @brief Sequential double-ended queue (deque) container class
 * 
 * A dynamically sized sequential queue that allows efficient insertion
 * and removal at both the front and back. Uses a circular buffer internally.
 * 
 * @tparam T The element type
 * @tparam Allocator The allocator type (default: FDefaultAllocator)
 */
template<typename T, typename Allocator = FDefaultAllocator>
class TDeque
{
public:
    using ElementType = T;
    using SizeType = typename Allocator::SizeType;
    using AllocatorType = Allocator;

private:
    /** Wrap index around the circular buffer */
    static SizeType WrapAround(SizeType Index, SizeType Range)
    {
        return (Index < Range) ? Index : Index - Range;
    }

public:
    // ========================================================================
    // Iterator
    // ========================================================================

    template<typename ElementT>
    class TIteratorImpl
    {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = ElementT;
        using difference_type = std::ptrdiff_t;
        using pointer = ElementT*;
        using reference = ElementT&;

        TIteratorImpl() = default;

        TIteratorImpl(ElementT* InData, SizeType InRange, SizeType InOffset)
            : Data(InData), Range(InRange), Offset(InOffset)
        {
        }

        reference operator*() const
        {
            return *(Data + WrapAround(Offset, Range));
        }

        pointer operator->() const
        {
            return Data + WrapAround(Offset, Range);
        }

        TIteratorImpl& operator++()
        {
            ++Offset;
            return *this;
        }

        TIteratorImpl operator++(int)
        {
            TIteratorImpl Tmp = *this;
            ++Offset;
            return Tmp;
        }

        TIteratorImpl& operator--()
        {
            --Offset;
            return *this;
        }

        TIteratorImpl operator--(int)
        {
            TIteratorImpl Tmp = *this;
            --Offset;
            return Tmp;
        }

        TIteratorImpl& operator+=(difference_type N)
        {
            Offset += static_cast<SizeType>(N);
            return *this;
        }

        TIteratorImpl& operator-=(difference_type N)
        {
            Offset -= static_cast<SizeType>(N);
            return *this;
        }

        TIteratorImpl operator+(difference_type N) const
        {
            return TIteratorImpl(Data, Range, Offset + static_cast<SizeType>(N));
        }

        TIteratorImpl operator-(difference_type N) const
        {
            return TIteratorImpl(Data, Range, Offset - static_cast<SizeType>(N));
        }

        difference_type operator-(const TIteratorImpl& Other) const
        {
            return static_cast<difference_type>(Offset) - static_cast<difference_type>(Other.Offset);
        }

        bool operator==(const TIteratorImpl& Other) const
        {
            return Data + Offset == Other.Data + Other.Offset;
        }

        bool operator!=(const TIteratorImpl& Other) const
        {
            return !(*this == Other);
        }

        bool operator<(const TIteratorImpl& Other) const
        {
            return Offset < Other.Offset;
        }

        bool operator>(const TIteratorImpl& Other) const
        {
            return Offset > Other.Offset;
        }

        bool operator<=(const TIteratorImpl& Other) const
        {
            return Offset <= Other.Offset;
        }

        bool operator>=(const TIteratorImpl& Other) const
        {
            return Offset >= Other.Offset;
        }

    private:
        ElementT* Data = nullptr;
        SizeType Range = 0;
        SizeType Offset = 0;
    };

    using Iterator = TIteratorImpl<ElementType>;
    using ConstIterator = TIteratorImpl<const ElementType>;

public:
    // ========================================================================
    // Constructors and Destructor
    // ========================================================================

    /** Default constructor */
    TDeque()
        : Data(nullptr)
        , Capacity(0)
        , Head(0)
        , Tail(0)
        , Count(0)
    {
        Reserve(DefaultCapacity);
    }

    /** Constructor with initial capacity */
    explicit TDeque(SizeType InitialCapacity)
        : Data(nullptr)
        , Capacity(0)
        , Head(0)
        , Tail(0)
        , Count(0)
    {
        Reserve(InitialCapacity);
    }

    /** Copy constructor */
    TDeque(const TDeque& Other)
        : Data(nullptr)
        , Capacity(0)
        , Head(0)
        , Tail(0)
        , Count(0)
    {
        Reserve(Other.Count);
        for (SizeType i = 0; i < Other.Count; ++i)
        {
            PushBack(Other[i]);
        }
    }

    /** Move constructor */
    TDeque(TDeque&& Other) noexcept
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

    /** Initializer list constructor */
    TDeque(std::initializer_list<ElementType> InitList)
        : Data(nullptr)
        , Capacity(0)
        , Head(0)
        , Tail(0)
        , Count(0)
    {
        Reserve(static_cast<SizeType>(InitList.size()));
        for (const auto& Item : InitList)
        {
            PushBack(Item);
        }
    }

    /** Destructor */
    ~TDeque()
    {
        Clear();
        if (Data)
        {
            MonsterRender::FMemory::Free(Data);
            Data = nullptr;
        }
    }

    /** Copy assignment */
    TDeque& operator=(const TDeque& Other)
    {
        if (this != &Other)
        {
            Clear();
            Reserve(Other.Count);
            for (SizeType i = 0; i < Other.Count; ++i)
            {
                PushBack(Other[i]);
            }
        }
        return *this;
    }

    /** Move assignment */
    TDeque& operator=(TDeque&& Other) noexcept
    {
        if (this != &Other)
        {
            Clear();
            if (Data)
            {
                MonsterRender::FMemory::Free(Data);
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

    /** Initializer list assignment */
    TDeque& operator=(std::initializer_list<ElementType> InitList)
    {
        Clear();
        Reserve(static_cast<SizeType>(InitList.size()));
        for (const auto& Item : InitList)
        {
            PushBack(Item);
        }
        return *this;
    }

public:
    // ========================================================================
    // Element Access
    // ========================================================================

    /** Access element by index */
    ElementType& operator[](SizeType Index)
    {
        return Data[WrapAround(Head + Index, Capacity)];
    }

    const ElementType& operator[](SizeType Index) const
    {
        return Data[WrapAround(Head + Index, Capacity)];
    }

    /** Access first element */
    ElementType& First()
    {
        return Data[Head];
    }

    const ElementType& First() const
    {
        return Data[Head];
    }

    /** Access last element */
    ElementType& Last()
    {
        return Data[WrapAround(Tail + Capacity - 1, Capacity)];
    }

    const ElementType& Last() const
    {
        return Data[WrapAround(Tail + Capacity - 1, Capacity)];
    }

public:
    // ========================================================================
    // Capacity
    // ========================================================================

    /** Check if empty */
    bool IsEmpty() const
    {
        return Count == 0;
    }

    /** Get number of elements */
    SizeType Num() const
    {
        return Count;
    }

    /** Get maximum capacity */
    SizeType Max() const
    {
        return Capacity;
    }

    /** Reserve capacity */
    void Reserve(SizeType NewCapacity)
    {
        if (NewCapacity <= Capacity)
        {
            return;
        }
        
        // Allocate new buffer
        ElementType* NewData = static_cast<ElementType*>(
            MonsterRender::FMemory::Malloc(NewCapacity * sizeof(ElementType), alignof(ElementType))
        );
        
        // Copy existing elements
        if (Data && Count > 0)
        {
            for (SizeType i = 0; i < Count; ++i)
            {
                new (NewData + i) ElementType(std::move((*this)[i]));
                (*this)[i].~ElementType();
            }
        }
        
        // Free old buffer
        if (Data)
        {
            MonsterRender::FMemory::Free(Data);
        }
        
        Data = NewData;
        Capacity = NewCapacity;
        Head = 0;
        Tail = Count;
    }

    /** Shrink to fit */
    void Shrink()
    {
        if (Count == 0)
        {
            if (Data)
            {
                MonsterRender::FMemory::Free(Data);
                Data = nullptr;
            }
            Capacity = 0;
            Head = 0;
            Tail = 0;
            return;
        }
        
        if (Count < Capacity)
        {
            ElementType* NewData = static_cast<ElementType*>(
                MonsterRender::FMemory::Malloc(Count * sizeof(ElementType), alignof(ElementType))
            );
            
            for (SizeType i = 0; i < Count; ++i)
            {
                new (NewData + i) ElementType(std::move((*this)[i]));
                (*this)[i].~ElementType();
            }
            
            MonsterRender::FMemory::Free(Data);
            Data = NewData;
            Capacity = Count;
            Head = 0;
            Tail = Count;
        }
    }

public:
    // ========================================================================
    // Modifiers
    // ========================================================================

    /** Add element to back (copy) */
    void PushBack(const ElementType& Item)
    {
        EmplaceBack(Item);
    }

    /** Add element to back (move) */
    void PushBack(ElementType&& Item)
    {
        EmplaceBack(std::move(Item));
    }

    /** Construct element at back */
    template<typename... Args>
    ElementType& EmplaceBack(Args&&... InArgs)
    {
        EnsureCapacity(Count + 1);
        
        ElementType* Ptr = Data + Tail;
        new (Ptr) ElementType(std::forward<Args>(InArgs)...);
        
        Tail = WrapAround(Tail + 1, Capacity);
        ++Count;
        
        return *Ptr;
    }

    /** Add element to front (copy) */
    void PushFront(const ElementType& Item)
    {
        EmplaceFront(Item);
    }

    /** Add element to front (move) */
    void PushFront(ElementType&& Item)
    {
        EmplaceFront(std::move(Item));
    }

    /** Construct element at front */
    template<typename... Args>
    ElementType& EmplaceFront(Args&&... InArgs)
    {
        EnsureCapacity(Count + 1);
        
        Head = (Head == 0) ? Capacity - 1 : Head - 1;
        
        ElementType* Ptr = Data + Head;
        new (Ptr) ElementType(std::forward<Args>(InArgs)...);
        
        ++Count;
        
        return *Ptr;
    }

    /** Remove element from back */
    void PopBack()
    {
        if (Count > 0)
        {
            Tail = (Tail == 0) ? Capacity - 1 : Tail - 1;
            Data[Tail].~ElementType();
            --Count;
        }
    }

    /** Remove element from front */
    void PopFront()
    {
        if (Count > 0)
        {
            Data[Head].~ElementType();
            Head = WrapAround(Head + 1, Capacity);
            --Count;
        }
    }

    /** Remove and return element from back */
    ElementType PopBackValue()
    {
        Tail = (Tail == 0) ? Capacity - 1 : Tail - 1;
        ElementType Result = std::move(Data[Tail]);
        Data[Tail].~ElementType();
        --Count;
        return Result;
    }

    /** Remove and return element from front */
    ElementType PopFrontValue()
    {
        ElementType Result = std::move(Data[Head]);
        Data[Head].~ElementType();
        Head = WrapAround(Head + 1, Capacity);
        --Count;
        return Result;
    }

    /** Clear all elements */
    void Clear()
    {
        while (Count > 0)
        {
            PopBack();
        }
        Head = 0;
        Tail = 0;
    }

    /** Reset (clear and free memory) */
    void Reset()
    {
        Clear();
        if (Data)
        {
            MonsterRender::FMemory::Free(Data);
            Data = nullptr;
        }
        Capacity = 0;
    }

    // ========================================================================
    // STL Compatibility Methods
    // ========================================================================
    
    /** STL-compatible clear() - same as Clear() */
    void clear() { Clear(); }
    
    /** STL-compatible size() - same as Num() */
    SizeType size() const { return Count; }
    
    /** STL-compatible empty() - same as IsEmpty() */
    bool empty() const { return Count == 0; }
    
    /** STL-compatible push_back() - same as PushBack() */
    template<typename ArgType>
    void push_back(ArgType&& Item) { PushBack(std::forward<ArgType>(Item)); }
    
    /** STL-compatible push_front() - same as PushFront() */
    template<typename ArgType>
    void push_front(ArgType&& Item) { PushFront(std::forward<ArgType>(Item)); }
    
    /** STL-compatible pop_back() - same as PopBack() but returns void */
    void pop_back() { PopBack(); }
    
    /** STL-compatible pop_front() - same as PopFront() but returns void */
    void pop_front() { PopFront(); }
    
    /** STL-compatible front() - same as First() */
    ElementType& front() { return First(); }
    const ElementType& front() const { return First(); }
    
    /** STL-compatible back() - same as Last() */
    ElementType& back() { return Last(); }
    const ElementType& back() const { return Last(); }

public:
    // ========================================================================
    // Iterators
    // ========================================================================

    Iterator begin()
    {
        return Iterator(Data, Capacity, Head);
    }

    Iterator end()
    {
        return Iterator(Data, Capacity, Head + Count);
    }

    ConstIterator begin() const
    {
        return ConstIterator(Data, Capacity, Head);
    }

    ConstIterator end() const
    {
        return ConstIterator(Data, Capacity, Head + Count);
    }

private:
    /** Ensure capacity for N elements */
    void EnsureCapacity(SizeType RequiredCapacity)
    {
        if (RequiredCapacity > Capacity)
        {
            // Grow by 1.5x or to required capacity, whichever is larger
            SizeType NewCapacity = Capacity + Capacity / 2;
            if (NewCapacity < RequiredCapacity)
            {
                NewCapacity = RequiredCapacity;
            }
            if (NewCapacity < DefaultCapacity)
            {
                NewCapacity = DefaultCapacity;
            }
            Reserve(NewCapacity);
        }
    }

private:
    /** Default initial capacity */
    static constexpr SizeType DefaultCapacity = 8;

    /** Pointer to data */
    ElementType* Data;
    
    /** Current capacity */
    SizeType Capacity;
    
    /** Index of first element */
    SizeType Head;
    
    /** Index after last element */
    SizeType Tail;
    
    /** Number of elements */
    SizeType Count;
};

} // namespace MonsterEngine
