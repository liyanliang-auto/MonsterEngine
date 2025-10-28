#pragma once

// Platform detection
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

// C++ Standard Library
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <optional>
#include <span>
#include <functional>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cassert>
#include <iostream>

// Core types and definitions
#include "Core/CoreTypes.h"
#include "Core/Log.h"
#include "Core/Assert.h"
#include "Core/Memory.h"

// Engine namespace
namespace MonsterRender {
    // Forward declarations
    class Engine;
    class Renderer;
    
    namespace RHI {
        class IRHIDevice;
        class IRHICommandList;
        class IRHIResource;
        class IRHIBuffer;
        class IRHITexture;
        class IRHIPipelineState;
    }
}
