#pragma once

/**
 * MonsterEngine Precompiled Header (PCH)
 * 
 * This file contains commonly used headers that rarely change.
 * Including this as a precompiled header significantly speeds up compilation.
 * 
 * Reference: UE5 Engine/Source/Runtime/Core/Public/CoreMinimal.h
 */

// ============================================================================
// Platform Detection
// ============================================================================
#if defined(_WIN32)
    #define PLATFORM_WINDOWS 1
#else
    #define PLATFORM_WINDOWS 0
#endif

#if defined(__linux__)
    #define PLATFORM_LINUX 1
#else
    #define PLATFORM_LINUX 0
#endif

// ============================================================================
// C++ Standard Library Headers (rarely change, expensive to parse)
// ============================================================================
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <map>
#include <set>
#include <optional>
#include <span>
#include <functional>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <type_traits>
#include <utility>
#include <array>
#include <limits>

// ============================================================================
// Core Engine Types (stable, rarely change)
// ============================================================================
#include "Core/CoreTypes.h"
#include "Core/Templates/SharedPointer.h"
#include "Core/Templates/UniquePtr.h"
#include "Core/Assert.h"
#include "Core/Memory.h"

// ============================================================================
// Container Types (stable)
// ============================================================================
#include "Containers/String.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/Set.h"

// ============================================================================
// Logging (used everywhere)
// ============================================================================
#include "Core/Log.h"
