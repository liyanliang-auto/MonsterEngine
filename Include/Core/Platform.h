#pragma once

// Keep platform selection in one header. Android defines __linux__ on the NDK,
// so it must always win over the desktop Linux branch. The values are numeric
// on every target because callers use these macros in #if expressions.
#undef PLATFORM_ANDROID
#undef PLATFORM_WINDOWS
#undef PLATFORM_LINUX

#if defined(__ANDROID__)
#define PLATFORM_ANDROID 1
#define PLATFORM_WINDOWS 0
#define PLATFORM_LINUX 0
#elif defined(_WIN32) || defined(_WIN64)
#define PLATFORM_ANDROID 0
#define PLATFORM_WINDOWS 1
#define PLATFORM_LINUX 0
#elif defined(__linux__)
#define PLATFORM_ANDROID 0
#define PLATFORM_WINDOWS 0
#define PLATFORM_LINUX 1
#else
#define PLATFORM_ANDROID 0
#define PLATFORM_WINDOWS 0
#define PLATFORM_LINUX 0
#endif

#if PLATFORM_ANDROID || PLATFORM_LINUX
#define PLATFORM_POSIX 1
#else
#define PLATFORM_POSIX 0
#endif

#if defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__) || defined(__LP64__)
#define PLATFORM_64BITS 1
#define PLATFORM_32BITS 0
#else
#define PLATFORM_64BITS 0
#define PLATFORM_32BITS 1
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#define PLATFORM_CPU_ARM64 1
#else
#define PLATFORM_CPU_ARM64 0
#endif

#if PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#ifndef FORCEINLINE
#if defined(_MSC_VER)
#define FORCEINLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define FORCEINLINE inline __attribute__((always_inline))
#else
#define FORCEINLINE inline
#endif
#endif
