// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file BitArray.h
 * @brief Bit array container following UE5 TBitArray patterns
 * 
 * TBitArray is a dynamically sized array of bits with the following features:
 * - Efficient storage (1 bit per element)
 * - Fast iteration over set/unset bits
 * - Used by TSparseArray for tracking allocated elements
 */

#include "Core/CoreTypes.h"
#include "Core/Templates/TypeTraits.h"
#include "ContainerAllocationPolicies.h"
#include "ContainerFwd.h"

#include <cstring>

namespace MonsterEngine
{

// ============================================================================
// Bit Array Constants
// ============================================================================

/** Number of bits per word */
constexpr int32 NumBitsPerDWORD = 32;

/** Mask for getting bit index within a word */
constexpr int32 PerDWORDMask = NumBitsPerDWORD - 1;

/** Shift for converting bit index to word index */
constexpr int32 PerDWORDShift = 5;

// ============================================================================
// FDefaultBitArrayAllocator
// ============================================================================

/**
 * Default allocator for bit arrays
 */
class FDefaultBitArrayAllocator : public FHeapAllocator
{
};

// ============================================================================
// TBitArray
// ============================================================================

/**
 * A dynamically sized bit array
 * 
 * Stores bits efficiently using 32-bit words.
 * Provides O(1) access to individual bits.
 */
template<typename Allocator = FDefaultBitArrayAllocator>
class TBitArray
{
public:
    using SizeType = int32;
    
    // ========================================================================
    // Constructors and Destructor
    // ========================================================================
    
    /**
     * Default constructor - creates empty bit array
     */
    TBitArray()
        : NumBits(0)
        , MaxBits(0)
    {
    }
    
    /**
     * Constructor with initial value for all bits
     * @param bValue Initial value for all bits
     * @param InNumBits Number of bits to allocate
     */
    explicit TBitArray(bool bValue, SizeType InNumBits)
        : NumBits(0)
        , MaxBits(0)
    {
        Init(bValue, InNumBits);
    }
    
    /**
     * Copy constructor
     */
    TBitArray(const TBitArray& Other)
        : NumBits(0)
        , MaxBits(0)
    {
        *this = Other;
    }
    
    /**
     * Move constructor (UE5 pattern)
     */
    TBitArray(TBitArray&& Other) noexcept
        : NumBits(0)
        , MaxBits(0)
    {
        // Use MoveToEmpty to transfer ownership (UE5 pattern)
        AllocatorInstance.MoveToEmpty(Other.AllocatorInstance);
        NumBits = Other.NumBits;
        MaxBits = Other.MaxBits;
        Other.NumBits = 0;
        Other.MaxBits = 0;
    }
    
    /**
     * Destructor
     */
    ~TBitArray() = default;
    
    // ========================================================================
    // Assignment Operators
    // ========================================================================
    
    /**
     * Copy assignment
     */
    TBitArray& operator=(const TBitArray& Other)
    {
        if (this != &Other)
        {
            NumBits = Other.NumBits;
            if (NumBits > MaxBits)
            {
                Realloc(NumBits);
            }
            
            if (NumBits > 0)
            {
                std::memcpy(GetData(), Other.GetData(), GetNumWords() * sizeof(uint32));
            }
        }
        return *this;
    }
    
    /**
     * Move assignment (UE5 pattern)
     */
    TBitArray& operator=(TBitArray&& Other) noexcept
    {
        if (this != &Other)
        {
            // Use MoveToEmpty to transfer ownership (UE5 pattern)
            AllocatorInstance.MoveToEmpty(Other.AllocatorInstance);
            NumBits = Other.NumBits;
            MaxBits = Other.MaxBits;
            Other.NumBits = 0;
            Other.MaxBits = 0;
        }
        return *this;
    }
    
    // ========================================================================
    // Initialization
    // ========================================================================
    
    /**
     * Initializes the bit array with a value
     */
    void Init(bool bValue, SizeType InNumBits)
    {
        NumBits = InNumBits;
        if (NumBits > MaxBits)
        {
            Realloc(NumBits);
        }
        
        if (NumBits > 0)
        {
            std::memset(GetData(), bValue ? 0xFF : 0, GetNumWords() * sizeof(uint32));
        }
    }
    
    /**
     * Empties the bit array
     */
    void Empty(SizeType ExpectedNumBits = 0)
    {
        NumBits = 0;
        if (ExpectedNumBits != MaxBits)
        {
            Realloc(ExpectedNumBits);
        }
    }
    
    /**
     * Resets the bit array without deallocating
     */
    void Reset()
    {
        NumBits = 0;
    }
    
    // ========================================================================
    // Size and Capacity
    // ========================================================================
    
    /**
     * Returns number of bits
     */
    FORCEINLINE SizeType Num() const
    {
        return NumBits;
    }
    
    /**
     * Returns maximum number of bits without reallocation
     */
    FORCEINLINE SizeType Max() const
    {
        return MaxBits;
    }
    
    /**
     * Returns true if empty
     */
    FORCEINLINE bool IsEmpty() const
    {
        return NumBits == 0;
    }
    
    /**
     * Returns number of 32-bit words used
     */
    FORCEINLINE SizeType GetNumWords() const
    {
        return (NumBits + NumBitsPerDWORD - 1) >> PerDWORDShift;
    }
    
    /**
     * Returns maximum number of words
     */
    FORCEINLINE SizeType GetMaxWords() const
    {
        return (MaxBits + NumBitsPerDWORD - 1) >> PerDWORDShift;
    }
    
    // ========================================================================
    // Bit Access
    // ========================================================================
    
    /**
     * Gets the value of a bit
     */
    FORCEINLINE bool operator[](SizeType Index) const
    {
        return GetData()[Index >> PerDWORDShift] & (1u << (Index & PerDWORDMask));
    }
    
    /**
     * Sets a bit to true
     */
    FORCEINLINE void SetBit(SizeType Index, bool bValue)
    {
        if (bValue)
        {
            GetData()[Index >> PerDWORDShift] |= (1u << (Index & PerDWORDMask));
        }
        else
        {
            GetData()[Index >> PerDWORDShift] &= ~(1u << (Index & PerDWORDMask));
        }
    }
    
    /**
     * Sets a range of bits
     */
    void SetRange(SizeType Index, SizeType Count, bool bValue)
    {
        for (SizeType i = 0; i < Count; ++i)
        {
            SetBit(Index + i, bValue);
        }
    }
    
    // ========================================================================
    // Adding Bits
    // ========================================================================
    
    /**
     * Adds a bit to the end
     * @return Index of the new bit
     */
    SizeType Add(bool bValue)
    {
        const SizeType Index = NumBits;
        
        if (NumBits >= MaxBits)
        {
            // Grow by 25% or at least 4 words
            const SizeType NewMaxBits = std::max(MaxBits + MaxBits / 4, MaxBits + NumBitsPerDWORD * 4);
            Realloc(NewMaxBits);
        }
        
        ++NumBits;
        SetBit(Index, bValue);
        
        return Index;
    }
    
    /**
     * Adds multiple bits to the end
     * @return Index of the first new bit
     */
    SizeType Add(bool bValue, SizeType Count)
    {
        const SizeType Index = NumBits;
        
        const SizeType NewNumBits = NumBits + Count;
        if (NewNumBits > MaxBits)
        {
            Realloc(NewNumBits);
        }
        
        NumBits = NewNumBits;
        SetRange(Index, Count, bValue);
        
        return Index;
    }
    
    // ========================================================================
    // Removing Bits
    // ========================================================================
    
    /**
     * Removes bits at the given index
     */
    void RemoveAt(SizeType Index, SizeType Count = 1)
    {
        // Shift remaining bits
        for (SizeType i = Index; i < NumBits - Count; ++i)
        {
            SetBit(i, (*this)[i + Count]);
        }
        NumBits -= Count;
    }
    
    /**
     * Removes bits at the given index by swapping with end bits
     */
    void RemoveAtSwap(SizeType Index, SizeType Count = 1)
    {
        // Copy bits from end to fill gap
        for (SizeType i = 0; i < Count && Index + i < NumBits - Count; ++i)
        {
            SetBit(Index + i, (*this)[NumBits - Count + i]);
        }
        NumBits -= Count;
    }
    
    // ========================================================================
    // Finding Bits
    // ========================================================================
    
    /**
     * Finds the first set bit
     * @return Index of first set bit, or INDEX_NONE_VALUE if none
     */
    SizeType FindFirstSetBit() const
    {
        const uint32* Data = GetData();
        const SizeType NumWords = GetNumWords();
        
        for (SizeType WordIndex = 0; WordIndex < NumWords; ++WordIndex)
        {
            if (Data[WordIndex] != 0)
            {
                // Find first set bit in this word
                uint32 Word = Data[WordIndex];
                for (SizeType BitIndex = 0; BitIndex < NumBitsPerDWORD; ++BitIndex)
                {
                    if (Word & (1u << BitIndex))
                    {
                        const SizeType Index = WordIndex * NumBitsPerDWORD + BitIndex;
                        return Index < NumBits ? Index : INDEX_NONE_VALUE;
                    }
                }
            }
        }
        
        return INDEX_NONE_VALUE;
    }
    
    /**
     * Finds the first unset bit
     * @return Index of first unset bit, or INDEX_NONE_VALUE if none
     */
    SizeType FindFirstZeroBit() const
    {
        const uint32* Data = GetData();
        const SizeType NumWords = GetNumWords();
        
        for (SizeType WordIndex = 0; WordIndex < NumWords; ++WordIndex)
        {
            if (Data[WordIndex] != 0xFFFFFFFF)
            {
                // Find first unset bit in this word
                uint32 Word = Data[WordIndex];
                for (SizeType BitIndex = 0; BitIndex < NumBitsPerDWORD; ++BitIndex)
                {
                    if (!(Word & (1u << BitIndex)))
                    {
                        const SizeType Index = WordIndex * NumBitsPerDWORD + BitIndex;
                        return Index < NumBits ? Index : INDEX_NONE_VALUE;
                    }
                }
            }
        }
        
        return INDEX_NONE_VALUE;
    }
    
    /**
     * Counts the number of set bits
     */
    SizeType CountSetBits() const
    {
        SizeType Count = 0;
        const uint32* Data = GetData();
        const SizeType NumWords = GetNumWords();
        
        for (SizeType WordIndex = 0; WordIndex < NumWords; ++WordIndex)
        {
            // Population count (number of set bits)
            uint32 Word = Data[WordIndex];
            while (Word)
            {
                Count += Word & 1;
                Word >>= 1;
            }
        }
        
        return Count;
    }
    
    // ========================================================================
    // Data Access
    // ========================================================================
    
    /**
     * Returns pointer to raw data
     */
    FORCEINLINE uint32* GetData()
    {
        return static_cast<uint32*>(AllocatorInstance.GetAllocation());
    }
    
    /**
     * Returns const pointer to raw data
     */
    FORCEINLINE const uint32* GetData() const
    {
        return static_cast<const uint32*>(AllocatorInstance.GetAllocation());
    }
    
private:
    // ========================================================================
    // Internal Helpers
    // ========================================================================
    
    void Realloc(SizeType NewMaxBits)
    {
        const SizeType OldMaxWords = GetMaxWords();
        MaxBits = NewMaxBits;
        const SizeType NewMaxWords = GetMaxWords();
        
        if (NewMaxWords != OldMaxWords)
        {
            AllocatorInstance.ResizeAllocation(OldMaxWords, NewMaxWords, sizeof(uint32), alignof(uint32));
            
            // Zero new words
            if (NewMaxWords > OldMaxWords)
            {
                std::memset(GetData() + OldMaxWords, 0, (NewMaxWords - OldMaxWords) * sizeof(uint32));
            }
        }
    }
    
private:
    SizeType NumBits;
    SizeType MaxBits;
    typename Allocator::ForAnyElementType AllocatorInstance;
};

} // namespace MonsterEngine
