#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace MonsterRender {
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
    
    // Smart pointers (following UE5 style)
    template<typename T>
    using TSharedPtr = std::shared_ptr<T>;
    
    template<typename T>
    using TUniquePtr = std::unique_ptr<T>;
    
    template<typename T>
    using TWeakPtr = std::weak_ptr<T>;
    
    // Helper functions for smart pointers
    template<typename T, typename... Args>
    constexpr TSharedPtr<T> MakeShared(Args&&... args) {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }
    
    template<typename T, typename... Args>
    constexpr TUniquePtr<T> MakeUnique(Args&&... args) {
        return std::make_unique<T>(std::forward<Args>(args)...);
    }
    
    // Array and container types
    template<typename T>
    using TArray = std::vector<T>;
    
    template<typename T>
    using TSpan = std::span<T>;
    
    template<typename Key, typename Value>
    using TMap = std::unordered_map<Key, Value>;
    
    // Optional type
    template<typename T>
    using TOptional = std::optional<T>;
    
    // Function type
    template<typename T>
    using TFunction = std::function<T>;
}




