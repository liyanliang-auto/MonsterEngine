#pragma once

/**
 * LogCategory.h
 * 
 * Defines log category system following UE5 architecture.
 * Each log category has a name, default verbosity, and compile-time verbosity.
 */

#include "Core/CoreTypes.h"
#include "Core/Logging/LogVerbosity.h"
#include <atomic>

namespace MonsterRender {

/**
 * Base class for all log categories.
 * Manages runtime verbosity filtering and category registration.
 */
class FLogCategoryBase {
public:
    /**
     * Constructor - registers the category with the log system
     * @param InCategoryName - Name of the category (e.g., "LogRenderer")
     * @param InDefaultVerbosity - Default runtime verbosity level
     * @param InCompileTimeVerbosity - Maximum verbosity compiled into the code
     */
    FLogCategoryBase(
        const char* InCategoryName,
        ELogVerbosity::Type InDefaultVerbosity,
        ELogVerbosity::Type InCompileTimeVerbosity)
        : m_categoryName(InCategoryName)
        , m_verbosity(InDefaultVerbosity)
        , m_defaultVerbosity(InDefaultVerbosity)
        , m_compileTimeVerbosity(InCompileTimeVerbosity)
        , m_debugBreakOnLog(false)
    {
    }

    /**
     * Check if a log at the given verbosity level should be suppressed
     * @param VerbosityLevel - The verbosity level to check
     * @return true if the log should be suppressed (not printed)
     */
    FORCEINLINE bool IsSuppressed(ELogVerbosity::Type VerbosityLevel) const {
        return !((VerbosityLevel & ELogVerbosity::VerbosityMask) <= m_verbosity.load(std::memory_order_relaxed));
    }

    /** Get the category name */
    FORCEINLINE const char* GetCategoryName() const { return m_categoryName; }

    /** Get the current runtime verbosity */
    FORCEINLINE ELogVerbosity::Type GetVerbosity() const { 
        return static_cast<ELogVerbosity::Type>(m_verbosity.load(std::memory_order_relaxed)); 
    }

    /** Set the runtime verbosity (clamped to compile-time verbosity) */
    void SetVerbosity(ELogVerbosity::Type InVerbosity) {
        uint8 newVerbosity = InVerbosity & ELogVerbosity::VerbosityMask;
        if (newVerbosity > m_compileTimeVerbosity) {
            newVerbosity = m_compileTimeVerbosity;
        }
        m_verbosity.store(newVerbosity, std::memory_order_relaxed);
    }

    /** Get the compile-time verbosity */
    FORCEINLINE ELogVerbosity::Type GetCompileTimeVerbosity() const { 
        return m_compileTimeVerbosity; 
    }

    /** Reset verbosity to default */
    void ResetToDefault() {
        m_verbosity.store(m_defaultVerbosity, std::memory_order_relaxed);
    }

    /** Check if debug break is enabled for this category */
    FORCEINLINE bool ShouldDebugBreak() const { return m_debugBreakOnLog; }

    /** Enable/disable debug break on log */
    void SetDebugBreak(bool bEnable) { m_debugBreakOnLog = bEnable; }

private:
    const char* m_categoryName;
    std::atomic<uint8> m_verbosity;
    const uint8 m_defaultVerbosity;
    const ELogVerbosity::Type m_compileTimeVerbosity;
    bool m_debugBreakOnLog;
};

/**
 * Template for log categories that transfers compile-time constants to the base class.
 * @tparam InDefaultVerbosity - Default runtime verbosity
 * @tparam InCompileTimeVerbosity - Maximum verbosity compiled into the code
 */
template<ELogVerbosity::Type InDefaultVerbosity, ELogVerbosity::Type InCompileTimeVerbosity>
class FLogCategory : public FLogCategoryBase {
public:
    static_assert((InDefaultVerbosity & ELogVerbosity::VerbosityMask) < ELogVerbosity::NumVerbosity, 
                  "Invalid default verbosity.");
    static_assert(InCompileTimeVerbosity < ELogVerbosity::NumVerbosity, 
                  "Invalid compile time verbosity.");

    /** Compile-time verbosity constant for use in if constexpr */
    static constexpr ELogVerbosity::Type CompileTimeVerbosity = InCompileTimeVerbosity;

    /** Get compile-time verbosity (constexpr version) */
    static constexpr ELogVerbosity::Type GetCompileTimeVerbosity() { return CompileTimeVerbosity; }

    /** Constructor */
    FORCEINLINE FLogCategory(const char* InCategoryName)
        : FLogCategoryBase(InCategoryName, InDefaultVerbosity, CompileTimeVerbosity)
    {
    }
};

} // namespace MonsterRender

// ============================================================================
// Log Category Declaration/Definition Macros
// ============================================================================

/**
 * Declares a log category as extern (for use in header files).
 * Pair with DEFINE_LOG_CATEGORY in a source file.
 * 
 * @param CategoryName - Name of the category (e.g., LogRenderer)
 * @param DefaultVerbosity - Default runtime verbosity (e.g., Log, Warning)
 * @param CompileTimeVerbosity - Maximum verbosity to compile (e.g., All, Verbose)
 * 
 * Example:
 *   DECLARE_LOG_CATEGORY_EXTERN(LogRenderer, Log, All)
 */
#define DECLARE_LOG_CATEGORY_EXTERN(CategoryName, DefaultVerbosity, CompileTimeVerbosity) \
    extern struct FLogCategory##CategoryName : public MonsterRender::FLogCategory<MonsterRender::ELogVerbosity::DefaultVerbosity, MonsterRender::ELogVerbosity::CompileTimeVerbosity> \
    { \
        FORCEINLINE FLogCategory##CategoryName() : FLogCategory(#CategoryName) {} \
    } CategoryName;

/**
 * Defines a log category (for use in source files).
 * Pair with DECLARE_LOG_CATEGORY_EXTERN in a header file.
 * 
 * @param CategoryName - Name of the category
 * 
 * Example:
 *   DEFINE_LOG_CATEGORY(LogRenderer)
 */
#define DEFINE_LOG_CATEGORY(CategoryName) FLogCategory##CategoryName CategoryName;

/**
 * Defines a static log category (for use in a single source file only).
 * 
 * @param CategoryName - Name of the category
 * @param DefaultVerbosity - Default runtime verbosity
 * @param CompileTimeVerbosity - Maximum verbosity to compile
 * 
 * Example:
 *   DEFINE_LOG_CATEGORY_STATIC(LogMyModule, Log, All)
 */
#define DEFINE_LOG_CATEGORY_STATIC(CategoryName, DefaultVerbosity, CompileTimeVerbosity) \
    static struct FLogCategory##CategoryName : public MonsterRender::FLogCategory<MonsterRender::ELogVerbosity::DefaultVerbosity, MonsterRender::ELogVerbosity::CompileTimeVerbosity> \
    { \
        FORCEINLINE FLogCategory##CategoryName() : FLogCategory(#CategoryName) {} \
    } CategoryName;
