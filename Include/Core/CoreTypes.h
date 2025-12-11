#pragma once

// Prevent Windows min/max macros from interfering with std::min/max and std::numeric_limits
#if defined(_WIN32) || defined(_WIN64)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
#endif

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <optional>
#include <functional>
#include <cstddef>

// Platform-specific macros
#if defined(_WIN32) || defined(_WIN64)
    #define PLATFORM_WINDOWS 1
    #define FORCEINLINE __forceinline
#elif defined(__linux__)
    #define PLATFORM_LINUX 1
    #define FORCEINLINE inline __attribute__((always_inline))
#else
    #define FORCEINLINE inline
#endif

namespace MonsterRender {
    // Size type (similar to UE5's SIZE_T)
    using SIZE_T = std::size_t;
    // Basic integer types
    using uint8  = std::uint8_t;
    using uint16 = std::uint16_t;
    using uint32 = std::uint32_t;
    using uint64 = std::uint64_t;
    
    using int8   = std::int8_t;
    using int16  = std::int16_t;
    using int32  = std::int32_t;
    using int64  = std::int64_t;
    
    // Floating point types
    using float32 = float;
    using float64 = double;
    
    // String types
    using String = std::string;
    using StringView = std::string_view;
    
    // Span type
    template<typename T>
    using TSpan = std::span<T>;
    
    // Note: TArray is now defined in MonsterEngine::TArray (Containers/Array.h)
    // Note: TMap is now defined in MonsterEngine::TMap (Containers/Map.h)
    // Use MonsterEngine containers for new code
    
    // Note: Smart pointers (TSharedPtr, TUniquePtr, TWeakPtr) are now defined in MonsterEngine namespace
    // Include "Core/Templates/SharedPointer.h" and "Core/Templates/UniquePtr.h" for smart pointer support
    
    // Optional type
    template<typename T>
    using TOptional = std::optional<T>;
    
    // Function type
    template<typename T>
    using TFunction = std::function<T>;
}

// MonsterEngine namespace - mirror types for container system compatibility
namespace MonsterEngine {
    // Size type
    using SIZE_T = std::size_t;
    
    // Basic integer types
    using uint8  = std::uint8_t;
    using uint16 = std::uint16_t;
    using uint32 = std::uint32_t;
    using uint64 = std::uint64_t;
    
    using int8   = std::int8_t;
    using int16  = std::int16_t;
    using int32  = std::int32_t;
    using int64  = std::int64_t;
    
    // Floating point types
    using float32 = float;
    using float64 = double;
    
    // String types
    using String = std::string;
    using StringView = std::string_view;
    
    // Span type (C++20 std::span wrapper)
    template<typename T>
    using TSpan = std::span<T>;
    
    // Optional type
    template<typename T>
    using TOptional = std::optional<T>;
    
    // Function type
    template<typename T>
    using TFunction = std::function<T>;
}




