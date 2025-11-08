#pragma once

#include <cstdint>

namespace MonsterRender {

/**
 * 日志详细级别枚举
 * Enumerates available logging verbosity levels
 * 参考UE5的 ELogVerbosity 设计
 */
namespace ELogVerbosity {
    enum Type : uint8_t {
        /** 不输出任何日志 */
        NoLogging = 0,
        
        /** 
         * 总是输出致命错误，然后程序崩溃（即使在Shipping版本中）
         * Always prints a fatal error to console (and log file) and crashes (even if logging is disabled)
         */
        Fatal,
        
        /** 
         * 输出错误到控制台和日志文件
         * Prints an error to console (and log file)
         */
        Error,
        
        /** 
         * 输出警告到控制台和日志文件
         * Prints a warning to console (and log file)
         */
        Warning,
        
        /** 
         * 输出消息到控制台和日志文件
         * Prints a message to console (and log file)
         */
        Display,
        
        /** 
         * 输出消息到日志文件（不显示在控制台）
         * Prints a message to a log file (does not print to console)
         */
        Log,
        
        /** 
         * 输出详细消息到日志文件（通常用于详细的调试信息）
         * Prints a verbose message to a log file (if Verbose logging is enabled for the given category)
         */
        Verbose,
        
        /** 
         * 输出非常详细的消息到日志文件（通常用于极其详细的调试信息）
         * Prints a very verbose message to a log file (if VeryVerbose logging is enabled)
         */
        VeryVerbose,
        
        /** 所有日志级别 */
        All = VeryVerbose,
        
        /** 
         * 用于掩码和比较的数字值
         * Number of bits needed to represent verbosity
         */
        NumVerbosity,
        
        VerbosityMask = 0xf,
        SetColor = 0x40,
        BreakOnLog = 0x80
    };
}

/**
 * 日志类别编译时详细级别
 * Compile-time verbosity control
 */
#ifndef MR_LOG_ACTIVE_LEVEL
    #if defined(_DEBUG) || defined(DEBUG)
        #define MR_LOG_ACTIVE_LEVEL ELogVerbosity::All
    #else
        #define MR_LOG_ACTIVE_LEVEL ELogVerbosity::Log
    #endif
#endif

} // namespace MonsterRender

