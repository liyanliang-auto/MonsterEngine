// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file BitSet.h
 * @brief Fixed-size and dynamic bit set containers
 * 
 * Provides TBitSet (fixed-size) and TDynamicBitSet (dynamic size)
 * for efficient bit manipulation and storage.
 */

#include "Core/CoreTypes.h"
#include "Core/Templates/TypeTraits.h"
#include "Core/HAL/FMemory.h"
#include <cstring>
#include <algorithm>
#include <climits>

namespace MonsterEngine
{

// ============================================================================
// TBitSet - Fixed-size Bit Set
// ============================================================================

/**
 * @brief Fixed-size bit set with compile-time size
 * 
 * Efficiently stores N bits using minimal storage.
 * All storage is inline (no heap allocation).
 * 
 * @tparam N The number of bits
 */
template<uint32 N>
class TBitSet
{
public:
    using WordType = uint64;
    using SizeType = uint32;

    static constexpr SizeType NumBits = N;
    static constexpr SizeType BitsPerWord = sizeof(WordType) * CHAR_BIT;
    static constexpr SizeType NumWords = (N + BitsPerWord - 1) / BitsPerWord;

private:
    /** Bit reference for operator[] */
    class FBitReference
    {
    public:
        FBitReference(WordType& InWord, WordType InMask)
            : Word(InWord), Mask(InMask)
        {
        }

        FBitReference& operator=(bool Value)
        {
            if (Value)
            {
                Word |= Mask;
            }
            else
            {
                Word &= ~Mask;
            }
            return *this;
        }

        FBitReference& operator=(const FBitReference& Other)
        {
            return *this = static_cast<bool>(Other);
        }

        operator bool() const
        {
            return (Word & Mask) != 0;
        }

        bool operator~() const
        {
            return (Word & Mask) == 0;
        }

        FBitReference& Flip()
        {
            Word ^= Mask;
            return *this;
        }

    private:
        WordType& Word;
        WordType Mask;
    };

public:
    // ========================================================================
    // Constructors
    // ========================================================================

    /** Default constructor - all bits set to 0 */
    TBitSet()
    {
        Reset();
    }

    /** Constructor from unsigned value */
    explicit TBitSet(WordType Value)
    {
        Reset();
        if constexpr (NumWords > 0)
        {
            Words[0] = Value;
        }
    }

    /** Copy constructor */
    TBitSet(const TBitSet& Other)
    {
        std::memcpy(Words, Other.Words, sizeof(Words));
    }

    /** Copy assignment */
    TBitSet& operator=(const TBitSet& Other)
    {
        if (this != &Other)
        {
            std::memcpy(Words, Other.Words, sizeof(Words));
        }
        return *this;
    }

public:
    // ========================================================================
    // Bit Access
    // ========================================================================

    /** Access bit by index (returns reference) */
    FBitReference operator[](SizeType Index)
    {
        return FBitReference(Words[Index / BitsPerWord], WordType(1) << (Index % BitsPerWord));
    }

    /** Access bit by index (returns value) */
    bool operator[](SizeType Index) const
    {
        return Test(Index);
    }

    /** Test if bit is set */
    bool Test(SizeType Index) const
    {
        if (Index >= N) return false;
        return (Words[Index / BitsPerWord] & (WordType(1) << (Index % BitsPerWord))) != 0;
    }

    /** Test if all bits are set */
    bool All() const
    {
        if constexpr (N == 0) return true;

        // Check full words
        for (SizeType i = 0; i < NumWords - 1; ++i)
        {
            if (Words[i] != ~WordType(0))
            {
                return false;
            }
        }

        // Check last word (may have unused bits)
        constexpr SizeType LastWordBits = N % BitsPerWord;
        if constexpr (LastWordBits == 0)
        {
            return Words[NumWords - 1] == ~WordType(0);
        }
        else
        {
            constexpr WordType Mask = (WordType(1) << LastWordBits) - 1;
            return (Words[NumWords - 1] & Mask) == Mask;
        }
    }

    /** Test if any bit is set */
    bool Any() const
    {
        for (SizeType i = 0; i < NumWords; ++i)
        {
            if (Words[i] != 0)
            {
                return true;
            }
        }
        return false;
    }

    /** Test if no bits are set */
    bool None() const
    {
        return !Any();
    }

    /** Count number of set bits */
    SizeType Count() const
    {
        SizeType Result = 0;
        for (SizeType i = 0; i < NumWords; ++i)
        {
            Result += PopCount(Words[i]);
        }
        return Result;
    }

public:
    // ========================================================================
    // Bit Manipulation
    // ========================================================================

    /** Set bit at index */
    TBitSet& Set(SizeType Index, bool Value = true)
    {
        if (Index < N)
        {
            if (Value)
            {
                Words[Index / BitsPerWord] |= (WordType(1) << (Index % BitsPerWord));
            }
            else
            {
                Words[Index / BitsPerWord] &= ~(WordType(1) << (Index % BitsPerWord));
            }
        }
        return *this;
    }

    /** Set all bits to 1 */
    TBitSet& Set()
    {
        std::memset(Words, 0xFF, sizeof(Words));
        // Clear unused bits in last word
        if constexpr (N % BitsPerWord != 0)
        {
            constexpr WordType Mask = (WordType(1) << (N % BitsPerWord)) - 1;
            Words[NumWords - 1] &= Mask;
        }
        return *this;
    }

    /** Reset bit at index to 0 */
    TBitSet& Reset(SizeType Index)
    {
        return Set(Index, false);
    }

    /** Reset all bits to 0 */
    TBitSet& Reset()
    {
        std::memset(Words, 0, sizeof(Words));
        return *this;
    }

    /** Flip bit at index */
    TBitSet& Flip(SizeType Index)
    {
        if (Index < N)
        {
            Words[Index / BitsPerWord] ^= (WordType(1) << (Index % BitsPerWord));
        }
        return *this;
    }

    /** Flip all bits */
    TBitSet& Flip()
    {
        for (SizeType i = 0; i < NumWords; ++i)
        {
            Words[i] = ~Words[i];
        }
        // Clear unused bits in last word
        if constexpr (N % BitsPerWord != 0)
        {
            constexpr WordType Mask = (WordType(1) << (N % BitsPerWord)) - 1;
            Words[NumWords - 1] &= Mask;
        }
        return *this;
    }

public:
    // ========================================================================
    // Bitwise Operators
    // ========================================================================

    TBitSet operator~() const
    {
        TBitSet Result(*this);
        Result.Flip();
        return Result;
    }

    TBitSet& operator&=(const TBitSet& Other)
    {
        for (SizeType i = 0; i < NumWords; ++i)
        {
            Words[i] &= Other.Words[i];
        }
        return *this;
    }

    TBitSet& operator|=(const TBitSet& Other)
    {
        for (SizeType i = 0; i < NumWords; ++i)
        {
            Words[i] |= Other.Words[i];
        }
        return *this;
    }

    TBitSet& operator^=(const TBitSet& Other)
    {
        for (SizeType i = 0; i < NumWords; ++i)
        {
            Words[i] ^= Other.Words[i];
        }
        return *this;
    }

    TBitSet operator&(const TBitSet& Other) const
    {
        TBitSet Result(*this);
        return Result &= Other;
    }

    TBitSet operator|(const TBitSet& Other) const
    {
        TBitSet Result(*this);
        return Result |= Other;
    }

    TBitSet operator^(const TBitSet& Other) const
    {
        TBitSet Result(*this);
        return Result ^= Other;
    }

public:
    // ========================================================================
    // Shift Operators
    // ========================================================================

    TBitSet& operator<<=(SizeType Shift)
    {
        if (Shift >= N)
        {
            Reset();
            return *this;
        }

        const SizeType WordShift = Shift / BitsPerWord;
        const SizeType BitShift = Shift % BitsPerWord;

        if (BitShift == 0)
        {
            for (SizeType i = NumWords - 1; i >= WordShift; --i)
            {
                Words[i] = Words[i - WordShift];
            }
        }
        else
        {
            for (SizeType i = NumWords - 1; i > WordShift; --i)
            {
                Words[i] = (Words[i - WordShift] << BitShift) |
                           (Words[i - WordShift - 1] >> (BitsPerWord - BitShift));
            }
            Words[WordShift] = Words[0] << BitShift;
        }

        for (SizeType i = 0; i < WordShift; ++i)
        {
            Words[i] = 0;
        }

        // Clear unused bits
        if constexpr (N % BitsPerWord != 0)
        {
            constexpr WordType Mask = (WordType(1) << (N % BitsPerWord)) - 1;
            Words[NumWords - 1] &= Mask;
        }

        return *this;
    }

    TBitSet& operator>>=(SizeType Shift)
    {
        if (Shift >= N)
        {
            Reset();
            return *this;
        }

        const SizeType WordShift = Shift / BitsPerWord;
        const SizeType BitShift = Shift % BitsPerWord;

        if (BitShift == 0)
        {
            for (SizeType i = 0; i < NumWords - WordShift; ++i)
            {
                Words[i] = Words[i + WordShift];
            }
        }
        else
        {
            for (SizeType i = 0; i < NumWords - WordShift - 1; ++i)
            {
                Words[i] = (Words[i + WordShift] >> BitShift) |
                           (Words[i + WordShift + 1] << (BitsPerWord - BitShift));
            }
            Words[NumWords - WordShift - 1] = Words[NumWords - 1] >> BitShift;
        }

        for (SizeType i = NumWords - WordShift; i < NumWords; ++i)
        {
            Words[i] = 0;
        }

        return *this;
    }

    TBitSet operator<<(SizeType Shift) const
    {
        TBitSet Result(*this);
        return Result <<= Shift;
    }

    TBitSet operator>>(SizeType Shift) const
    {
        TBitSet Result(*this);
        return Result >>= Shift;
    }

public:
    // ========================================================================
    // Comparison
    // ========================================================================

    bool operator==(const TBitSet& Other) const
    {
        return std::memcmp(Words, Other.Words, sizeof(Words)) == 0;
    }

    bool operator!=(const TBitSet& Other) const
    {
        return !(*this == Other);
    }

public:
    // ========================================================================
    // Utility
    // ========================================================================

    /** Get number of bits */
    static constexpr SizeType Size() { return N; }

    /** Find first set bit, returns N if none found */
    SizeType FindFirstSet() const
    {
        for (SizeType i = 0; i < NumWords; ++i)
        {
            if (Words[i] != 0)
            {
                return i * BitsPerWord + CountTrailingZeros(Words[i]);
            }
        }
        return N;
    }

    /** Find last set bit, returns N if none found */
    SizeType FindLastSet() const
    {
        for (SizeType i = NumWords; i > 0; --i)
        {
            if (Words[i - 1] != 0)
            {
                return (i - 1) * BitsPerWord + (BitsPerWord - 1 - CountLeadingZeros(Words[i - 1]));
            }
        }
        return N;
    }

private:
    /** Count trailing zeros */
    static SizeType CountTrailingZeros(WordType Value)
    {
        if (Value == 0) return BitsPerWord;
#if defined(_MSC_VER)
        unsigned long Index;
        _BitScanForward64(&Index, Value);
        return static_cast<SizeType>(Index);
#else
        return static_cast<SizeType>(__builtin_ctzll(Value));
#endif
    }

    /** Count leading zeros */
    static SizeType CountLeadingZeros(WordType Value)
    {
        if (Value == 0) return BitsPerWord;
#if defined(_MSC_VER)
        unsigned long Index;
        _BitScanReverse64(&Index, Value);
        return static_cast<SizeType>(BitsPerWord - 1 - Index);
#else
        return static_cast<SizeType>(__builtin_clzll(Value));
#endif
    }

    /** Population count */
    static SizeType PopCount(WordType Value)
    {
#if defined(_MSC_VER)
        return static_cast<SizeType>(__popcnt64(Value));
#else
        return static_cast<SizeType>(__builtin_popcountll(Value));
#endif
    }

private:
    /** Storage for bits */
    WordType Words[NumWords > 0 ? NumWords : 1];
};

// ============================================================================
// TBitSet<0> Specialization
// ============================================================================

template<>
class TBitSet<0>
{
public:
    using SizeType = uint32;

    TBitSet() = default;
    explicit TBitSet(uint64) {}

    bool Test(SizeType) const { return false; }
    bool All() const { return true; }
    bool Any() const { return false; }
    bool None() const { return true; }
    SizeType Count() const { return 0; }

    TBitSet& Set(SizeType, bool = true) { return *this; }
    TBitSet& Set() { return *this; }
    TBitSet& Reset(SizeType) { return *this; }
    TBitSet& Reset() { return *this; }
    TBitSet& Flip(SizeType) { return *this; }
    TBitSet& Flip() { return *this; }

    static constexpr SizeType Size() { return 0; }
    SizeType FindFirstSet() const { return 0; }
    SizeType FindLastSet() const { return 0; }

    bool operator==(const TBitSet&) const { return true; }
    bool operator!=(const TBitSet&) const { return false; }
};

} // namespace MonsterEngine
