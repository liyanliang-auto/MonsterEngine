#pragma once

#include <cassert>

namespace MonsterRender {
    
    // Debug build assertions
    // 注意：MR_CHECK, MR_VERIFY等宏已在LogMacros.h中定义
    // Note: MR_CHECK, MR_VERIFY and other macros are now defined in LogMacros.h
    #ifdef _DEBUG
        #define MR_ASSERT(condition) assert(condition)
        #define MR_ASSERT_MSG(condition, message) \
            do { \
                if (!(condition)) { \
                    assert(false && message); \
                } \
            } while(0)
    #else
        #define MR_ASSERT(condition) ((void)0)
        #define MR_ASSERT_MSG(condition, message) ((void)0)
    #endif
}
