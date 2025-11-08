#pragma once

/**
 * MonsterRender 日志系统
 * MonsterRender Logging System
 * 
 * 参考UE5的日志架构设计
 * Based on UE5's logging architecture
 * 
 * 主要组件 Main Components:
 * - LogVerbosity.h: 定义日志详细级别
 * - LogCategory.h: 日志类别系统
 * - LogMacros.h: 日志宏定义
 * 
 * 基本用法 Basic Usage:
 * 
 * 1. 使用预定义的日志类别：
 *    MR_LOG(Temp, Warning, TEXT("Warning message: %s"), "details");
 *    MR_LOG(RHI, Error, TEXT("Error code: %d"), errorCode);
 * 
 * 2. 声明自定义日志类别：
 *    在头文件中：
 *      DECLARE_LOG_CATEGORY_EXTERN(MySystem, Log, All);
 *    
 *    在cpp文件中：
 *      DEFINE_LOG_CATEGORY(MySystem);
 *    
 *    使用：
 *      MR_LOG(MySystem, Display, TEXT("My system initialized"));
 * 
 * 3. 支持的日志级别（从高到低）：
 *    - Fatal: 致命错误，会导致程序崩溃
 *    - Error: 错误
 *    - Warning: 警告
 *    - Display: 显示级别的信息
 *    - Log: 一般日志
 *    - Verbose: 详细日志
 *    - VeryVerbose: 非常详细的日志
 * 
 * 4. 格式化支持：
 *    - 整数: %d
 *    - 浮点数: %f
 *    - 字符串: %s
 *    - 指针: %p
 *    - 十六进制: %x, %X
 */

#include "LogVerbosity.h"
#include "LogCategory.h"
#include "LogMacros.h"

// 这个头文件作为日志系统的统一入口点
// This header serves as the unified entry point for the logging system
