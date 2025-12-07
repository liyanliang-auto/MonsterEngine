// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file TypeTraits.h
 * @brief Type traits utilities following UE5 patterns
 * 
 * Provides compile-time type introspection and manipulation utilities.
 * These are used extensively by containers for optimization decisions.
 */

#include <type_traits>
#include <cstddef>

namespace MonsterEngine
{

// ============================================================================
// Basic Type Traits
// ============================================================================

/**
 * TIsSigned - Check if a type is a signed integer
 */
template<typename T>
struct TIsSigned
{
    enum { Value = std::is_signed_v<T> };
};

/**
 * TIsUnsigned - Check if a type is an unsigned integer
 */
template<typename T>
struct TIsUnsigned
{
    enum { Value = std::is_unsigned_v<T> };
};

/**
 * TIsFloatingPoint - Check if a type is floating point
 */
template<typename T>
struct TIsFloatingPoint
{
    enum { Value = std::is_floating_point_v<T> };
};

/**
 * TIsIntegral - Check if a type is an integral type
 */
template<typename T>
struct TIsIntegral
{
    enum { Value = std::is_integral_v<T> };
};

/**
 * TIsArithmetic - Check if a type is arithmetic (integral or floating point)
 */
template<typename T>
struct TIsArithmetic
{
    enum { Value = std::is_arithmetic_v<T> };
};

/**
 * TIsPointer - Check if a type is a pointer
 */
template<typename T>
struct TIsPointer
{
    enum { Value = std::is_pointer_v<T> };
};

/**
 * TIsEnum - Check if a type is an enum
 */
template<typename T>
struct TIsEnum
{
    enum { Value = std::is_enum_v<T> };
};

/**
 * TIsClass - Check if a type is a class/struct
 */
template<typename T>
struct TIsClass
{
    enum { Value = std::is_class_v<T> };
};

// ============================================================================
// Construction/Destruction Traits
// ============================================================================

/**
 * TIsTriviallyCopyConstructible - Check if type can be memcpy'd
 */
template<typename T>
struct TIsTriviallyCopyConstructible
{
    enum { Value = std::is_trivially_copy_constructible_v<T> };
};

/**
 * TIsTriviallyMoveConstructible - Check if type can be memmove'd
 */
template<typename T>
struct TIsTriviallyMoveConstructible
{
    enum { Value = std::is_trivially_move_constructible_v<T> };
};

/**
 * TIsTriviallyDestructible - Check if destructor is trivial
 */
template<typename T>
struct TIsTriviallyDestructible
{
    enum { Value = std::is_trivially_destructible_v<T> };
};

/**
 * TIsTriviallyCopyAssignable - Check if copy assignment is trivial
 */
template<typename T>
struct TIsTriviallyCopyAssignable
{
    enum { Value = std::is_trivially_copy_assignable_v<T> };
};

/**
 * TIsTriviallyMoveAssignable - Check if move assignment is trivial
 */
template<typename T>
struct TIsTriviallyMoveAssignable
{
    enum { Value = std::is_trivially_move_assignable_v<T> };
};

/**
 * TIsTrivial - Check if type is trivial (trivially copyable and trivially default constructible)
 */
template<typename T>
struct TIsTrivial
{
    enum { Value = std::is_trivial_v<T> };
};

/**
 * TIsTriviallyCopyable - Check if type can be safely memcpy'd
 * This is the key trait for container optimizations
 */
template<typename T>
struct TIsTriviallyCopyable
{
    enum { Value = std::is_trivially_copyable_v<T> };
};

/**
 * TIsPODType - Check if type is POD (Plain Old Data)
 * POD types can be safely memcpy'd and don't need constructors/destructors
 */
template<typename T>
struct TIsPODType
{
    enum { Value = std::is_standard_layout_v<T> && std::is_trivial_v<T> };
};

/**
 * TIsZeroConstructType - Types that can be zero-initialized with memset
 * Override this for custom types that support zero construction
 */
template<typename T>
struct TIsZeroConstructType
{
    enum { Value = TIsArithmetic<T>::Value || TIsPointer<T>::Value || TIsEnum<T>::Value };
};

// ============================================================================
// Type Modification Traits
// ============================================================================

/**
 * TRemoveConst - Remove const qualifier
 */
template<typename T>
struct TRemoveConst
{
    using Type = std::remove_const_t<T>;
};

template<typename T>
using TRemoveConst_T = typename TRemoveConst<T>::Type;

/**
 * TRemoveVolatile - Remove volatile qualifier
 */
template<typename T>
struct TRemoveVolatile
{
    using Type = std::remove_volatile_t<T>;
};

template<typename T>
using TRemoveVolatile_T = typename TRemoveVolatile<T>::Type;

/**
 * TRemoveCV - Remove const and volatile qualifiers
 */
template<typename T>
struct TRemoveCV
{
    using Type = std::remove_cv_t<T>;
};

template<typename T>
using TRemoveCV_T = typename TRemoveCV<T>::Type;

/**
 * TRemoveReference - Remove reference qualifier
 */
template<typename T>
struct TRemoveReference
{
    using Type = std::remove_reference_t<T>;
};

template<typename T>
using TRemoveReference_T = typename TRemoveReference<T>::Type;

/**
 * TRemovePointer - Remove pointer qualifier
 */
template<typename T>
struct TRemovePointer
{
    using Type = std::remove_pointer_t<T>;
};

template<typename T>
using TRemovePointer_T = typename TRemovePointer<T>::Type;

/**
 * TDecay - Decay type (remove references, cv-qualifiers, array-to-pointer, function-to-pointer)
 */
template<typename T>
struct TDecay
{
    using Type = std::decay_t<T>;
};

template<typename T>
using TDecay_T = typename TDecay<T>::Type;

/**
 * TMakeUnsigned - Convert to unsigned type
 */
template<typename T>
struct TMakeUnsigned
{
    using Type = std::make_unsigned_t<T>;
};

template<typename T>
using TMakeUnsigned_T = typename TMakeUnsigned<T>::Type;

/**
 * TMakeSigned - Convert to signed type
 */
template<typename T>
struct TMakeSigned
{
    using Type = std::make_signed_t<T>;
};

template<typename T>
using TMakeSigned_T = typename TMakeSigned<T>::Type;

// ============================================================================
// Conditional Type Selection
// ============================================================================

/**
 * TEnableIf - SFINAE helper
 */
template<bool Condition, typename T = void>
struct TEnableIf
{
};

template<typename T>
struct TEnableIf<true, T>
{
    using Type = T;
};

template<bool Condition, typename T = void>
using TEnableIf_T = typename TEnableIf<Condition, T>::Type;

/**
 * TConditional - Select type based on condition
 */
template<bool Condition, typename TrueType, typename FalseType>
struct TConditional
{
    using Type = std::conditional_t<Condition, TrueType, FalseType>;
};

template<bool Condition, typename TrueType, typename FalseType>
using TConditional_T = typename TConditional<Condition, TrueType, FalseType>::Type;

/**
 * TIsSame - Check if two types are the same
 */
template<typename A, typename B>
struct TIsSame
{
    enum { Value = std::is_same_v<A, B> };
};

/**
 * TIsDerivedFrom - Check if Derived is derived from Base
 */
template<typename Derived, typename Base>
struct TIsDerivedFrom
{
    enum { Value = std::is_base_of_v<Base, Derived> };
};

/**
 * TIsConvertible - Check if From can be converted to To
 */
template<typename From, typename To>
struct TIsConvertible
{
    enum { Value = std::is_convertible_v<From, To> };
};

// ============================================================================
// Type Identity (for preventing template argument deduction)
// ============================================================================

/**
 * TIdentity - Returns the same type, prevents argument deduction
 */
template<typename T>
struct TIdentity
{
    using Type = T;
};

template<typename T>
using TIdentity_T = typename TIdentity<T>::Type;

// ============================================================================
// Call Traits - Optimal parameter passing
// ============================================================================

/**
 * TCallTraits - Determines optimal way to pass a type as a parameter
 * Small types are passed by value, large types by const reference
 */
template<typename T>
struct TCallTraits
{
private:
    static constexpr bool PassByValue = 
        TIsArithmetic<T>::Value || 
        TIsPointer<T>::Value || 
        TIsEnum<T>::Value ||
        (sizeof(T) <= sizeof(void*) * 2 && TIsTriviallyCopyable<T>::Value);

public:
    using ValueType = T;
    using Reference = T&;
    using ConstReference = const T&;
    using ParamType = TConditional_T<PassByValue, T, const T&>;
    using ConstPointerType = const T*;
};

// Specialization for references
template<typename T>
struct TCallTraits<T&>
{
    using ValueType = T&;
    using Reference = T&;
    using ConstReference = const T&;
    using ParamType = T&;
    using ConstPointerType = const T*;
};

// ============================================================================
// Type Traits for Containers
// ============================================================================

/**
 * TTypeTraits - Combined traits for container element types
 */
template<typename T>
struct TTypeTraits
{
    using ConstPointerType = typename TCallTraits<T>::ConstPointerType;
    using ConstInitType = typename TCallTraits<T>::ParamType;
    
    // Can this type be relocated with memcpy?
    // Override this for types that have self-referential pointers
    enum { IsBytewiseComparable = TIsPODType<T>::Value };
};

/**
 * TContainerTraits - Traits for container optimization decisions
 */
template<typename T>
struct TContainerTraits
{
    // Can elements be moved with memmove?
    enum { MoveWillEmptyContainer = false };
};

// ============================================================================
// Alignment Helpers
// ============================================================================

/**
 * TAlignOf - Get alignment of a type
 */
template<typename T>
struct TAlignOf
{
    enum { Value = alignof(T) };
};

/**
 * TAlignedStorage - Storage with specified size and alignment
 */
template<size_t Size, size_t Alignment>
struct TAlignedStorage
{
    using Type = std::aligned_storage_t<Size, Alignment>;
};

template<size_t Size, size_t Alignment>
using TAlignedStorage_T = typename TAlignedStorage<Size, Alignment>::Type;

/**
 * TAlignedBytes - Byte array with specified alignment
 */
template<int32_t Size, uint32_t Alignment = alignof(std::max_align_t)>
struct TAlignedBytes
{
    alignas(Alignment) uint8_t Pad[Size];
};

// ============================================================================
// Utility Macros
// ============================================================================

/**
 * Helper macro for SFINAE with requires-like syntax
 */
#define TEMPLATE_REQUIRES(...) typename = TEnableIf_T<(__VA_ARGS__)>

} // namespace MonsterEngine
