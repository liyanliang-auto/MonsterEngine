#include "Core/Logging/LogMacros.h"

/**
 * LogCategories.cpp
 * 
 * Defines the built-in log categories for MonsterEngine.
 * Add new categories here or in your own source files.
 */

namespace MonsterRender {

// ============================================================================
// Built-in Log Categories
// ============================================================================

// Default temporary log category (equivalent to UE5's LogTemp)
DEFINE_LOG_CATEGORY(LogTemp)

} // namespace MonsterRender

// ============================================================================
// Additional Engine Log Categories
// ============================================================================

// These are defined outside the namespace for easier access

// Core engine categories
DEFINE_LOG_CATEGORY(LogCore)
DEFINE_LOG_CATEGORY(LogInit)
DEFINE_LOG_CATEGORY(LogExit)
DEFINE_LOG_CATEGORY(LogMemory)

// Rendering categories
DEFINE_LOG_CATEGORY(LogRenderer)
DEFINE_LOG_CATEGORY(LogRHI)
DEFINE_LOG_CATEGORY(LogVulkan)
DEFINE_LOG_CATEGORY(LogShaders)
DEFINE_LOG_CATEGORY(LogTextures)

// Platform categories
DEFINE_LOG_CATEGORY(LogPlatform)
DEFINE_LOG_CATEGORY(LogWindow)
DEFINE_LOG_CATEGORY(LogInput)
