// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file TypeHash.h
 * @brief Hash function utilities following UE5 patterns
 * 
 * Provides GetTypeHash() overloads for common types and hash combining utilities.
 * All hashable types should provide a GetTypeHash() overload.
 */

#include "Core/CoreTypes.h"
#include "TypeTraits.h"
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace MonsterEngine
{

// ============================================================================
// Hash Utility Functions
// ============================================================================

namespace Private
{
    /**
     * MurmurHash3 finalizer - improves hash distribution
     */
    FORCEINLINE uint32 MurmurFinalize32(uint32 Hash)
    {
        Hash ^= Hash >> 16;
        Hash *= 0x85ebca6b;
        Hash ^= Hash >> 13;
        Hash *= 0xc2b2ae35;
        Hash ^= Hash >> 16;
        return Hash;
    }

    /**
     * MurmurHash3 64-bit finalizer
     */
    FORCEINLINE uint64 MurmurFinalize64(uint64 Hash)
    {
        Hash ^= Hash >> 33;
        Hash *= 0xff51afd7ed558ccdULL;
        Hash ^= Hash >> 33;
        Hash *= 0xc4ceb9fe1a85ec53ULL;
        Hash ^= Hash >> 33;
        return Hash;
    }
} // namespace Private

/**
 * Combines two hash values to get a third.
 * Note - this function is not commutative.
 * 
 * This function cannot change for backward compatibility reasons.
 * You may want to choose HashCombineFast for a better in-memory hash combining function.
 * 
 * @param A First hash value
 * @param C Second hash value
 * @return Combined hash value
 */
FORCEINLINE uint32 HashCombine(uint32 A, uint32 C)
{
    uint32 B = 0x9e3779b9;
    A += B;

    A -= B; A -= C; A ^= (C >> 13);
    B -= C; B -= A; B ^= (A << 8);
    C -= A; C -= B; C ^= (B >> 13);
    A -= B; A -= C; A ^= (C >> 12);
    B -= C; B -= A; B ^= (A << 16);
    C -= A; C -= B; C ^= (B >> 5);
    A -= B; A -= C; A ^= (C >> 3);
    B -= C; B -= A; B ^= (A << 10);
    C -= A; C -= B; C ^= (B >> 15);

    return C;
}

/**
 * Fast hash combine for in-memory use only.
 * WARNING: This function is subject to change and should only be used for creating
 * combined hash values which don't leave the running process.
 * 
 * @param A First hash value
 * @param B Second hash value
 * @return Combined hash value
 */
FORCEINLINE uint32 HashCombineFast(uint32 A, uint32 B)
{
    return HashCombine(A, B);
}

/**
 * Hash a pointer value
 * Ignores the lower 4 bits since they are likely zero anyway.
 * 
 * @param Key Pointer to hash
 * @return Hash value
 */
FORCEINLINE uint32 PointerHash(const void* Key)
{
    const uintptr_t PtrInt = reinterpret_cast<uintptr_t>(Key) >> 4;
    return Private::MurmurFinalize32(static_cast<uint32>(PtrInt));
}

/**
 * Hash a pointer with an additional hash value
 * 
 * @param Key Pointer to hash
 * @param C Additional hash value to combine
 * @return Combined hash value
 */
FORCEINLINE uint32 PointerHash(const void* Key, uint32 C)
{
    return HashCombineFast(PointerHash(Key), C);
}

// ============================================================================
// GetTypeHash Overloads for Scalar Types
// ============================================================================

/**
 * Hash function for scalar types (integers, floats, pointers, enums)
 */
template<typename ScalarType,
    TEnableIf_T<std::is_scalar_v<ScalarType>>* = nullptr>
FORCEINLINE uint32 GetTypeHash(ScalarType Value)
{
    if constexpr (std::is_integral_v<ScalarType>)
    {
        if constexpr (sizeof(ScalarType) <= 4)
        {
            return Private::MurmurFinalize32(static_cast<uint32>(Value));
        }
        else
        {
            // 64-bit integer
            return Private::MurmurFinalize32(static_cast<uint32>(Value ^ (Value >> 32)));
        }
    }
    else if constexpr (std::is_floating_point_v<ScalarType>)
    {
        if constexpr (sizeof(ScalarType) == 4)
        {
            // Float
            uint32 Bits;
            std::memcpy(&Bits, &Value, sizeof(Bits));
            return Private::MurmurFinalize32(Bits);
        }
        else
        {
            // Double
            uint64 Bits;
            std::memcpy(&Bits, &Value, sizeof(Bits));
            return Private::MurmurFinalize32(static_cast<uint32>(Bits ^ (Bits >> 32)));
        }
    }
    else if constexpr (std::is_pointer_v<ScalarType>)
    {
        return PointerHash(Value);
    }
    else if constexpr (std::is_enum_v<ScalarType>)
    {
        return GetTypeHash(static_cast<std::underlying_type_t<ScalarType>>(Value));
    }
    else
    {
        static_assert(sizeof(ScalarType) == 0, "Unsupported scalar type for GetTypeHash");
        return 0;
    }
}

// ============================================================================
// GetTypeHash Overloads for Common Types
// ============================================================================

/**
 * Hash function for C-style strings
 */
FORCEINLINE uint32 GetTypeHash(const char* Str)
{
    if (!Str)
    {
        return 0;
    }
    
    // FNV-1a hash
    uint32 Hash = 2166136261u;
    while (*Str)
    {
        Hash ^= static_cast<uint32>(*Str++);
        Hash *= 16777619u;
    }
    return Hash;
}

/**
 * Hash function for wide C-style strings
 */
FORCEINLINE uint32 GetTypeHash(const wchar_t* Str)
{
    if (!Str)
    {
        return 0;
    }
    
    // FNV-1a hash
    uint32 Hash = 2166136261u;
    while (*Str)
    {
        Hash ^= static_cast<uint32>(*Str++);
        Hash *= 16777619u;
    }
    return Hash;
}

/**
 * Hash function for std::string
 */
FORCEINLINE uint32 GetTypeHash(const std::string& Str)
{
    // FNV-1a hash
    uint32 Hash = 2166136261u;
    for (char C : Str)
    {
        Hash ^= static_cast<uint32>(static_cast<uint8>(C));
        Hash *= 16777619u;
    }
    return Hash;
}

/**
 * Hash function for std::wstring
 */
FORCEINLINE uint32 GetTypeHash(const std::wstring& Str)
{
    // FNV-1a hash
    uint32 Hash = 2166136261u;
    for (wchar_t C : Str)
    {
        Hash ^= static_cast<uint32>(C);
        Hash *= 16777619u;
    }
    return Hash;
}

/**
 * Hash function for std::string_view
 */
FORCEINLINE uint32 GetTypeHash(std::string_view Str)
{
    // FNV-1a hash
    uint32 Hash = 2166136261u;
    for (char C : Str)
    {
        Hash ^= static_cast<uint32>(static_cast<uint8>(C));
        Hash *= 16777619u;
    }
    return Hash;
}

// ============================================================================
// Hash Helpers for Custom Types
// ============================================================================

/**
 * Helper to hash a range of bytes
 */
FORCEINLINE uint32 HashBytes(const void* Data, size_t Size)
{
    const uint8* Bytes = static_cast<const uint8*>(Data);
    
    // FNV-1a hash
    uint32 Hash = 2166136261u;
    for (size_t i = 0; i < Size; ++i)
    {
        Hash ^= Bytes[i];
        Hash *= 16777619u;
    }
    return Hash;
}

/**
 * Helper to hash multiple values together
 */
template<typename T, typename... Rest>
FORCEINLINE uint32 HashValues(const T& First, const Rest&... Others)
{
    uint32 Hash = GetTypeHash(First);
    if constexpr (sizeof...(Others) > 0)
    {
        Hash = HashCombineFast(Hash, HashValues(Others...));
    }
    return Hash;
}

// ============================================================================
// Bulk Serialization Traits
// ============================================================================

/**
 * TCanBulkSerialize - Trait to determine if a type can be serialized in bulk
 * Types that can be bulk serialized are memcpy'd directly without per-element serialization
 */
template<typename T>
struct TCanBulkSerialize
{
    enum { Value = false };
};

// Specializations for types that can be bulk serialized
template<> struct TCanBulkSerialize<int8>   { enum { Value = true }; };
template<> struct TCanBulkSerialize<uint8>  { enum { Value = true }; };
template<> struct TCanBulkSerialize<int16>  { enum { Value = true }; };
template<> struct TCanBulkSerialize<uint16> { enum { Value = true }; };
template<> struct TCanBulkSerialize<int32>  { enum { Value = true }; };
template<> struct TCanBulkSerialize<uint32> { enum { Value = true }; };
template<> struct TCanBulkSerialize<int64>  { enum { Value = true }; };
template<> struct TCanBulkSerialize<uint64> { enum { Value = true }; };
template<> struct TCanBulkSerialize<float>  { enum { Value = true }; };
template<> struct TCanBulkSerialize<double> { enum { Value = true }; };

} // namespace MonsterEngine
