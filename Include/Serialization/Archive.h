// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file Archive.h
 * @brief Serialization archive interface following UE5 FArchive patterns
 * 
 * FArchive provides a unified interface for serialization with:
 * - Bidirectional serialization (loading and saving)
 * - Support for various data types
 * - Extensible for custom types
 * - Version support for backward compatibility
 */

#include "Core/CoreTypes.h"
#include "Core/Templates/TypeTraits.h"
#include "Containers/ContainerFwd.h"

#include <cstring>
#include <string>

namespace MonsterEngine
{

// Forward declarations
class FString;
class FName;

// ============================================================================
// Archive Flags
// ============================================================================

/**
 * Archive state flags
 */
enum class EArchiveFlags : uint32
{
    None = 0,
    Loading = 1 << 0,       // Archive is loading (reading)
    Saving = 1 << 1,        // Archive is saving (writing)
    Persistent = 1 << 2,    // Archive is persistent (file, network)
    Error = 1 << 3,         // An error occurred
    AtEnd = 1 << 4,         // At end of archive
};

FORCEINLINE EArchiveFlags operator|(EArchiveFlags A, EArchiveFlags B)
{
    return static_cast<EArchiveFlags>(static_cast<uint32>(A) | static_cast<uint32>(B));
}

FORCEINLINE EArchiveFlags operator&(EArchiveFlags A, EArchiveFlags B)
{
    return static_cast<EArchiveFlags>(static_cast<uint32>(A) & static_cast<uint32>(B));
}

FORCEINLINE bool operator!(EArchiveFlags A)
{
    return static_cast<uint32>(A) == 0;
}

// ============================================================================
// FArchive
// ============================================================================

/**
 * Base class for serialization archives
 * 
 * FArchive provides a unified interface for reading and writing data.
 * The same code can be used for both loading and saving by checking IsLoading().
 * 
 * Usage:
 *   Ar << MyInt;           // Serialize int
 *   Ar << MyString;        // Serialize string
 *   Ar.Serialize(Data, Size); // Serialize raw bytes
 */
class FArchive
{
public:
    // ========================================================================
    // Constructors and Destructor
    // ========================================================================
    
    FArchive()
        : Flags(EArchiveFlags::None)
        , ArchiveVersion(0)
        , Position(0)
    {
    }
    
    virtual ~FArchive() = default;
    
    // ========================================================================
    // State Queries
    // ========================================================================
    
    /**
     * Returns true if archive is loading (reading)
     */
    FORCEINLINE bool IsLoading() const
    {
        return !!(Flags & EArchiveFlags::Loading);
    }
    
    /**
     * Returns true if archive is saving (writing)
     */
    FORCEINLINE bool IsSaving() const
    {
        return !!(Flags & EArchiveFlags::Saving);
    }
    
    /**
     * Returns true if archive is persistent
     */
    FORCEINLINE bool IsPersistent() const
    {
        return !!(Flags & EArchiveFlags::Persistent);
    }
    
    /**
     * Returns true if an error occurred
     */
    FORCEINLINE bool IsError() const
    {
        return !!(Flags & EArchiveFlags::Error);
    }
    
    /**
     * Returns true if at end of archive
     */
    FORCEINLINE bool AtEnd() const
    {
        return !!(Flags & EArchiveFlags::AtEnd);
    }
    
    /**
     * Sets error flag
     */
    void SetError()
    {
        Flags = Flags | EArchiveFlags::Error;
    }
    
    /**
     * Returns archive version
     */
    FORCEINLINE int32 GetArchiveVersion() const
    {
        return ArchiveVersion;
    }
    
    /**
     * Sets archive version
     */
    void SetArchiveVersion(int32 Version)
    {
        ArchiveVersion = Version;
    }
    
    // ========================================================================
    // Position
    // ========================================================================
    
    /**
     * Returns current position in archive
     */
    virtual int64 Tell() const
    {
        return Position;
    }
    
    /**
     * Returns total size of archive
     */
    virtual int64 TotalSize()
    {
        return 0;
    }
    
    /**
     * Seeks to position
     */
    virtual void Seek(int64 InPos)
    {
        Position = InPos;
    }
    
    // ========================================================================
    // Raw Serialization
    // ========================================================================
    
    /**
     * Serializes raw bytes
     * Override in derived classes for actual I/O
     */
    virtual void Serialize(void* Data, int64 Num)
    {
        // Base implementation does nothing
        // Derived classes should implement actual I/O
    }
    
    // ========================================================================
    // Operator << for Basic Types
    // ========================================================================
    
    /**
     * Serializes bool
     */
    FArchive& operator<<(bool& Value)
    {
        uint8 ByteValue = Value ? 1 : 0;
        Serialize(&ByteValue, sizeof(ByteValue));
        if (IsLoading())
        {
            Value = ByteValue != 0;
        }
        return *this;
    }
    
    /**
     * Serializes int8
     */
    FArchive& operator<<(int8& Value)
    {
        Serialize(&Value, sizeof(Value));
        return *this;
    }
    
    /**
     * Serializes uint8
     */
    FArchive& operator<<(uint8& Value)
    {
        Serialize(&Value, sizeof(Value));
        return *this;
    }
    
    /**
     * Serializes int16
     */
    FArchive& operator<<(int16& Value)
    {
        Serialize(&Value, sizeof(Value));
        return *this;
    }
    
    /**
     * Serializes uint16
     */
    FArchive& operator<<(uint16& Value)
    {
        Serialize(&Value, sizeof(Value));
        return *this;
    }
    
    /**
     * Serializes int32
     */
    FArchive& operator<<(int32& Value)
    {
        Serialize(&Value, sizeof(Value));
        return *this;
    }
    
    /**
     * Serializes uint32
     */
    FArchive& operator<<(uint32& Value)
    {
        Serialize(&Value, sizeof(Value));
        return *this;
    }
    
    /**
     * Serializes int64
     */
    FArchive& operator<<(int64& Value)
    {
        Serialize(&Value, sizeof(Value));
        return *this;
    }
    
    /**
     * Serializes uint64
     */
    FArchive& operator<<(uint64& Value)
    {
        Serialize(&Value, sizeof(Value));
        return *this;
    }
    
    /**
     * Serializes float
     */
    FArchive& operator<<(float& Value)
    {
        Serialize(&Value, sizeof(Value));
        return *this;
    }
    
    /**
     * Serializes double
     */
    FArchive& operator<<(double& Value)
    {
        Serialize(&Value, sizeof(Value));
        return *this;
    }
    
    // ========================================================================
    // Operator << for Strings
    // ========================================================================
    
    /**
     * Serializes std::string
     */
    FArchive& operator<<(std::string& Value)
    {
        int32 Length = static_cast<int32>(Value.length());
        *this << Length;
        
        if (IsLoading())
        {
            Value.resize(Length);
        }
        
        if (Length > 0)
        {
            Serialize(Value.data(), Length);
        }
        
        return *this;
    }
    
    /**
     * Serializes std::wstring
     */
    FArchive& operator<<(std::wstring& Value)
    {
        int32 Length = static_cast<int32>(Value.length());
        *this << Length;
        
        if (IsLoading())
        {
            Value.resize(Length);
        }
        
        if (Length > 0)
        {
            Serialize(Value.data(), Length * sizeof(wchar_t));
        }
        
        return *this;
    }
    
protected:
    EArchiveFlags Flags;
    int32 ArchiveVersion;
    int64 Position;
};

// ============================================================================
// FMemoryArchive
// ============================================================================

/**
 * Archive that reads/writes to memory buffer
 */
class FMemoryArchive : public FArchive
{
public:
    FMemoryArchive() = default;
    virtual ~FMemoryArchive() = default;
};

// ============================================================================
// FMemoryWriter
// ============================================================================

/**
 * Archive that writes to a memory buffer
 */
class FMemoryWriter : public FMemoryArchive
{
public:
    FMemoryWriter(TArray<uint8>& InBytes)
        : Bytes(InBytes)
    {
        Flags = EArchiveFlags::Saving | EArchiveFlags::Persistent;
    }
    
    virtual void Serialize(void* Data, int64 Num) override
    {
        if (Num > 0)
        {
            int64 WritePos = Tell();
            
            // Grow buffer if needed
            if (WritePos + Num > static_cast<int64>(Bytes.Num()))
            {
                Bytes.SetNumUninitialized(static_cast<int32>(WritePos + Num));
            }
            
            std::memcpy(Bytes.GetData() + WritePos, Data, static_cast<size_t>(Num));
            Position = WritePos + Num;
        }
    }
    
    virtual int64 TotalSize() override
    {
        return static_cast<int64>(Bytes.Num());
    }
    
private:
    TArray<uint8>& Bytes;
};

// ============================================================================
// FMemoryReader
// ============================================================================

/**
 * Archive that reads from a memory buffer
 */
class FMemoryReader : public FMemoryArchive
{
public:
    FMemoryReader(const TArray<uint8>& InBytes)
        : Bytes(InBytes)
    {
        Flags = EArchiveFlags::Loading | EArchiveFlags::Persistent;
    }
    
    FMemoryReader(const uint8* InData, int64 InSize)
        : DataPtr(InData)
        , DataSize(InSize)
    {
        Flags = EArchiveFlags::Loading | EArchiveFlags::Persistent;
    }
    
    virtual void Serialize(void* Data, int64 Num) override
    {
        if (Num > 0)
        {
            int64 ReadPos = Tell();
            const uint8* Source = DataPtr ? DataPtr : Bytes.GetData();
            int64 SourceSize = DataPtr ? DataSize : static_cast<int64>(Bytes.Num());
            
            if (ReadPos + Num <= SourceSize)
            {
                std::memcpy(Data, Source + ReadPos, static_cast<size_t>(Num));
                Position = ReadPos + Num;
            }
            else
            {
                SetError();
                Flags = Flags | EArchiveFlags::AtEnd;
            }
        }
    }
    
    virtual int64 TotalSize() override
    {
        return DataPtr ? DataSize : static_cast<int64>(Bytes.Num());
    }
    
private:
    const TArray<uint8>& Bytes = EmptyBytes;
    const uint8* DataPtr = nullptr;
    int64 DataSize = 0;
    
    static inline const TArray<uint8> EmptyBytes;
};

// ============================================================================
// Serialization Helpers
// ============================================================================

/**
 * Helper to serialize TArray
 */
template<typename ElementType, typename Allocator>
FArchive& operator<<(FArchive& Ar, TArray<ElementType, Allocator>& Array)
{
    int32 Num = Array.Num();
    Ar << Num;
    
    if (Ar.IsLoading())
    {
        Array.SetNumUninitialized(Num);
    }
    
    // Check if we can bulk serialize
    if constexpr (TCanBulkSerialize<ElementType>::Value)
    {
        if (Num > 0)
        {
            Ar.Serialize(Array.GetData(), Num * sizeof(ElementType));
        }
    }
    else
    {
        for (int32 i = 0; i < Num; ++i)
        {
            Ar << Array[i];
        }
    }
    
    return Ar;
}

/**
 * Helper to serialize TMap
 */
template<typename KeyType, typename ValueType, typename Allocator, typename KeyFuncs>
FArchive& operator<<(FArchive& Ar, TMap<KeyType, ValueType, Allocator, KeyFuncs>& Map)
{
    int32 Num = Map.Num();
    Ar << Num;
    
    if (Ar.IsLoading())
    {
        Map.Empty(Num);
        for (int32 i = 0; i < Num; ++i)
        {
            KeyType Key;
            ValueType Value;
            Ar << Key;
            Ar << Value;
            Map.Add(std::move(Key), std::move(Value));
        }
    }
    else
    {
        for (auto& Pair : Map)
        {
            KeyType Key = Pair.Key;
            ValueType Value = Pair.Value;
            Ar << Key;
            Ar << Value;
        }
    }
    
    return Ar;
}

/**
 * Helper to serialize TSet
 */
template<typename ElementType, typename KeyFuncs, typename Allocator>
FArchive& operator<<(FArchive& Ar, TSet<ElementType, KeyFuncs, Allocator>& Set)
{
    int32 Num = Set.Num();
    Ar << Num;
    
    if (Ar.IsLoading())
    {
        Set.Empty(Num);
        for (int32 i = 0; i < Num; ++i)
        {
            ElementType Element;
            Ar << Element;
            Set.Add(std::move(Element));
        }
    }
    else
    {
        for (const ElementType& Element : Set)
        {
            ElementType Copy = Element;
            Ar << Copy;
        }
    }
    
    return Ar;
}

/**
 * Helper to serialize TMap
 * Serializes key-value pairs sequentially
 */
template<typename KeyType, typename ValueType, typename SetAllocator, typename KeyFuncs>
FArchive& operator<<(FArchive& Ar, TMap<KeyType, ValueType, SetAllocator, KeyFuncs>& Map)
{
    int32 Num = Map.Num();
    Ar << Num;
    
    if (Ar.IsLoading())
    {
        Map.Empty(Num);
        for (int32 i = 0; i < Num; ++i)
        {
            KeyType Key;
            ValueType Value;
            Ar << Key;
            Ar << Value;
            Map.Add(std::move(Key), std::move(Value));
        }
    }
    else
    {
        for (auto& Pair : Map)
        {
            KeyType Key = Pair.Key;
            ValueType Value = Pair.Value;
            Ar << Key;
            Ar << Value;
        }
    }
    
    return Ar;
}

/**
 * Helper to serialize TMultiMap
 * Serializes key-value pairs sequentially (allows duplicate keys)
 */
template<typename KeyType, typename ValueType, typename SetAllocator, typename KeyFuncs>
FArchive& operator<<(FArchive& Ar, TMultiMap<KeyType, ValueType, SetAllocator, KeyFuncs>& Map)
{
    int32 Num = Map.Num();
    Ar << Num;
    
    if (Ar.IsLoading())
    {
        Map.Empty(Num);
        for (int32 i = 0; i < Num; ++i)
        {
            KeyType Key;
            ValueType Value;
            Ar << Key;
            Ar << Value;
            Map.Add(std::move(Key), std::move(Value));
        }
    }
    else
    {
        for (auto& Pair : Map)
        {
            KeyType Key = Pair.Key;
            ValueType Value = Pair.Value;
            Ar << Key;
            Ar << Value;
        }
    }
    
    return Ar;
}

/**
 * Helper to serialize TStaticArray
 */
template<typename ElementType, uint32 N>
FArchive& operator<<(FArchive& Ar, TStaticArray<ElementType, N>& Array)
{
    // Check if we can bulk serialize
    if constexpr (TCanBulkSerialize<ElementType>::Value)
    {
        Ar.Serialize(Array.GetData(), N * sizeof(ElementType));
    }
    else
    {
        for (uint32 i = 0; i < N; ++i)
        {
            Ar << Array[i];
        }
    }
    
    return Ar;
}

/**
 * Helper to serialize TBitSet
 */
template<uint32 N>
FArchive& operator<<(FArchive& Ar, TBitSet<N>& BitSet)
{
    // Serialize the underlying words directly
    constexpr uint32 NumWords = (N + 63) / 64;
    for (uint32 i = 0; i < NumWords; ++i)
    {
        // Access internal storage through bit operations
        // This is a simplified approach - full implementation would need friend access
        uint64 Word = 0;
        if (Ar.IsSaving())
        {
            for (uint32 j = 0; j < 64 && (i * 64 + j) < N; ++j)
            {
                if (BitSet.Test(i * 64 + j))
                {
                    Word |= (uint64(1) << j);
                }
            }
        }
        Ar << Word;
        if (Ar.IsLoading())
        {
            for (uint32 j = 0; j < 64 && (i * 64 + j) < N; ++j)
            {
                BitSet.Set(i * 64 + j, (Word & (uint64(1) << j)) != 0);
            }
        }
    }
    
    return Ar;
}

} // namespace MonsterEngine

// ============================================================================
// Forward declarations for new types serialization
// ============================================================================

namespace MonsterEngine
{

// Forward declare color types
struct FLinearColor;
struct FColor;

/**
 * Serialize FLinearColor
 */
inline FArchive& operator<<(FArchive& Ar, FLinearColor& Color)
{
    Ar << reinterpret_cast<float&>(Color);  // R
    Ar << *(reinterpret_cast<float*>(&Color) + 1);  // G
    Ar << *(reinterpret_cast<float*>(&Color) + 2);  // B
    Ar << *(reinterpret_cast<float*>(&Color) + 3);  // A
    return Ar;
}

/**
 * Serialize FColor
 */
inline FArchive& operator<<(FArchive& Ar, FColor& Color)
{
    Ar << reinterpret_cast<uint32&>(Color);
    return Ar;
}

} // namespace MonsterEngine
