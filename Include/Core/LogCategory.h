#pragma once

#include "LogVerbosity.h"
#include <string_view>

namespace MonsterRender {

/**
 * 日志类别结构
 * Log category structure, similar to UE5's FLogCategoryBase
 */
struct FLogCategory {
    /** 类别名称 */
    const char* CategoryName;
    
    /** 默认详细级别 */
    ELogVerbosity::Type DefaultVerbosity;
    
    /** 运行时详细级别（可以在运行时修改） */
    ELogVerbosity::Type CompileTimeVerbosity;
    
    /**
     * 构造函数
     * @param InCategoryName - 类别名称
     * @param InDefaultVerbosity - 默认详细级别
     */
    constexpr FLogCategory(const char* InCategoryName, 
                          ELogVerbosity::Type InDefaultVerbosity,
                          ELogVerbosity::Type InCompileTimeVerbosity = ELogVerbosity::All)
        : CategoryName(InCategoryName)
        , DefaultVerbosity(InDefaultVerbosity)
        , CompileTimeVerbosity(InCompileTimeVerbosity)
    {}
    
    /**
     * 检查是否应该在给定详细级别记录日志
     * @param Verbosity - 要检查的详细级别
     * @return 如果应该记录日志则返回true
     */
    inline bool IsSuppressed(ELogVerbosity::Type Verbosity) const {
        return Verbosity > DefaultVerbosity || Verbosity > CompileTimeVerbosity;
    }
    
    /**
     * 设置运行时详细级别
     * @param Verbosity - 新的详细级别
     */
    inline void SetVerbosity(ELogVerbosity::Type Verbosity) {
        DefaultVerbosity = Verbosity;
    }
};

/**
 * 声明日志类别（在头文件中使用）
 * Similar to UE5's DECLARE_LOG_CATEGORY_EXTERN
 */
#define DECLARE_LOG_CATEGORY_EXTERN(CategoryName, DefaultVerbosity, CompileTimeVerbosity) \
    extern struct FLogCategory LogCategory##CategoryName;

/**
 * 定义日志类别（在cpp文件中使用）
 * Similar to UE5's DEFINE_LOG_CATEGORY
 */
#define DEFINE_LOG_CATEGORY(CategoryName) \
    struct FLogCategory LogCategory##CategoryName(#CategoryName, ELogVerbosity::Log, MR_LOG_ACTIVE_LEVEL);

/**
 * 定义带自定义详细级别的日志类别
 */
#define DEFINE_LOG_CATEGORY_WITH_VERBOSITY(CategoryName, DefaultVerbosity) \
    struct FLogCategory LogCategory##CategoryName(#CategoryName, ELogVerbosity::DefaultVerbosity, MR_LOG_ACTIVE_LEVEL);

// 声明常用的日志类别
DECLARE_LOG_CATEGORY_EXTERN(Temp, Log, All);           // 临时日志
DECLARE_LOG_CATEGORY_EXTERN(Core, Log, All);           // 核心系统
DECLARE_LOG_CATEGORY_EXTERN(RHI, Log, All);            // 渲染硬件接口
DECLARE_LOG_CATEGORY_EXTERN(Renderer, Log, All);       // 渲染器
DECLARE_LOG_CATEGORY_EXTERN(Memory, Log, All);         // 内存管理
DECLARE_LOG_CATEGORY_EXTERN(Vulkan, Log, All);         // Vulkan后端
DECLARE_LOG_CATEGORY_EXTERN(D3D12, Log, All);          // D3D12后端
DECLARE_LOG_CATEGORY_EXTERN(Shader, Log, All);         // 着色器系统
DECLARE_LOG_CATEGORY_EXTERN(Texture, Log, All);        // 纹理系统
DECLARE_LOG_CATEGORY_EXTERN(Input, Log, All);          // 输入系统

} // namespace MonsterRender

