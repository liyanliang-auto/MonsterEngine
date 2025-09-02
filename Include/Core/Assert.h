#pragma once

#include <cassert>
#include "Core/Log.h"

namespace MonsterRender {
    
    // Debug build assertions
    #ifdef _DEBUG
        #define MR_ASSERT(condition) assert(condition)
        #define MR_ASSERT_MSG(condition, message) \
            do { \
                if (!(condition)) { \
                    MR_LOG_FATAL("Assertion failed: " message); \
                } \
            } while(0)
    #else
        #define MR_ASSERT(condition) ((void)0)
        #define MR_ASSERT_MSG(condition, message) ((void)0)
    #endif
    
    // Always-on checks (even in release)
    #define MR_CHECK(condition) \
        do { \
            if (!(condition)) { \
                MR_LOG_FATAL("Check failed: " #condition); \
            } \
        } while(0)
        
    #define MR_CHECK_MSG(condition, message) \
        do { \
            if (!(condition)) { \
                MR_LOG_FATAL("Check failed: " message); \
            } \
        } while(0)
        
    // Verify macros (return false on failure in release, assert in debug)
    #ifdef _DEBUG
        #define MR_VERIFY(condition) MR_ASSERT(condition)
        #define MR_VERIFY_MSG(condition, message) MR_ASSERT_MSG(condition, message)
    #else
        #define MR_VERIFY(condition) (condition)
        #define MR_VERIFY_MSG(condition, message) (condition)
    #endif
}
