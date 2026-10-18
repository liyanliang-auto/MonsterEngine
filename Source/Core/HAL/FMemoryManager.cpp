// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Memory Manager Implementation

#include "Core/HAL/FMemoryManager.h"
#include "Core/Log.h"
#include "Core/RuntimeLease.h"

#if PLATFORM_WINDOWS
    #include <Windows.h>
#elif PLATFORM_LINUX || PLATFORM_ANDROID
    #include <sys/sysinfo.h>
    #include <unistd.h>
#endif

namespace MonsterRender {

FMemoryManager& FMemoryManager::Get() {
    static FMemoryManager Instance;
    return Instance;
}

FMemoryManager::FMemoryManager() {
    MR_LOG_INFO("FMemoryManager created");
}

FMemoryManager::~FMemoryManager() {
    Shutdown();
}

bool FMemoryManager::Initialize() {
    std::lock_guard<std::mutex> Lock(LifecycleMutex);
    if (bInitialized) {
        MR_LOG_WARNING("FMemoryManager already initialized");
        return true;
    }

    MR_LOG_INFO("Initializing FMemoryManager...");

    // Detect system capabilities
    DetectSystemCapabilities();

    // Create default allocator (FMallocBinned2)
    Allocator = MakeUnique<FMallocBinned2>();

    bInitialized = true;
    MR_LOG_INFO("FMemoryManager initialized successfully");
    return true;
}

void FMemoryManager::Shutdown() {
    std::lock_guard<std::mutex> Lock(LifecycleMutex);
    if (FProcessRuntimeLease::ActiveCount() != 0) return;
    if (!bInitialized) return;

    MR_LOG_INFO("Shutting down FMemoryManager...");

    // Release allocator
    Allocator.reset();

    bInitialized = false;
    MR_LOG_INFO("FMemoryManager shut down");
}

void FMemoryManager::SetAllocator(TUniquePtr<FMalloc> NewAllocator) {
    std::lock_guard<std::mutex> Lock(LifecycleMutex);
    if (FProcessRuntimeLease::ActiveCount() != 0) {
        MR_LOG_ERROR("Cannot replace allocator while process owners are active");
        return;
    }
    if (!NewAllocator) {
        MR_LOG_ERROR("Cannot set null allocator");
        return;
    }

    Allocator = std::move(NewAllocator);
    MR_LOG_INFO("Custom allocator set");
}

void FMemoryManager::GetGlobalMemoryStats(FGlobalMemoryStats& OutStats) {
    OutStats = {};
#if PLATFORM_WINDOWS
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        OutStats.TotalPhysicalMemory = memStatus.ullTotalPhys;
        OutStats.AvailablePhysicalMemory = memStatus.ullAvailPhys;
        OutStats.TotalVirtualMemory = memStatus.ullTotalVirtual;
        OutStats.AvailableVirtualMemory = memStatus.ullAvailVirtual;
    }

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    OutStats.PageSize = sysInfo.dwPageSize;

    // Check for large pages
    SIZE_T largePageMin = GetLargePageMinimum();
    OutStats.LargePageSize = largePageMin;

#elif PLATFORM_LINUX || PLATFORM_ANDROID
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        OutStats.TotalPhysicalMemory = uint64(info.totalram) * info.mem_unit;
        OutStats.AvailablePhysicalMemory = uint64(info.freeram) * info.mem_unit;
        OutStats.TotalVirtualMemory = uint64(info.totalswap) * info.mem_unit;
        OutStats.AvailableVirtualMemory = uint64(info.freeswap) * info.mem_unit;
    }

    const long PageSize = sysconf(_SC_PAGESIZE);
    OutStats.PageSize = PageSize > 0 ? uint64(PageSize) : 0;
#if PLATFORM_LINUX
    OutStats.LargePageSize = 2 * 1024 * 1024;  // 2MB typical
#elif PLATFORM_ANDROID
    // Android's huge-page allocation path is intentionally disabled. Report
    // only the actual base OS page size rather than a guessed large page.
    OutStats.LargePageSize = 0;
#endif

#else
    OutStats = FGlobalMemoryStats{};
#endif

    MR_LOG_INFO("System Memory Stats:");
    MR_LOG_INFO("  Total RAM: " + std::to_string(OutStats.TotalPhysicalMemory / 1024 / 1024) + " MB");
    MR_LOG_INFO("  Available RAM: " + std::to_string(OutStats.AvailablePhysicalMemory / 1024 / 1024) + " MB");
    MR_LOG_INFO("  Page Size: " + std::to_string(OutStats.PageSize) + " bytes");
    MR_LOG_INFO("  Large Page Size: " + std::to_string(OutStats.LargePageSize) + " bytes");
}

bool FMemoryManager::EnableHugePages(bool bEnable) {
#if PLATFORM_ANDROID
    if (bEnable) {
        MR_LOG_WARNING("Huge pages are disabled on Android");
        return false;
    }
#endif
    if (!bHugePagesAvailable) {
        MR_LOG_WARNING("Huge pages not available on this system");
        return false;
    }

    bHugePagesEnabled = bEnable;
    MR_LOG_INFO(String("Huge pages ") + (bEnable ? "enabled" : "disabled"));
    return true;
}

void FMemoryManager::DetectSystemCapabilities() {
    MR_LOG_INFO("Detecting system memory capabilities...");

#if PLATFORM_WINDOWS
    // Check for SeLockMemoryPrivilege
    HANDLE hToken;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        LUID luid;
        if (LookupPrivilegeValue(nullptr, SE_LOCK_MEMORY_NAME, &luid)) {
            PRIVILEGE_SET privs;
            privs.PrivilegeCount = 1;
            privs.Control = PRIVILEGE_SET_ALL_NECESSARY;
            privs.Privilege[0].Luid = luid;
            privs.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;

            BOOL hasPrivilege = FALSE;
            if (PrivilegeCheck(hToken, &privs, &hasPrivilege)) {
                bHugePagesAvailable = (hasPrivilege == TRUE);
            }
        }
        CloseHandle(hToken);
    }

#elif PLATFORM_LINUX
    // Check /proc/meminfo for HugePages_Total
    FILE* fp = fopen("/proc/meminfo", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "HugePages_Total:", 16) == 0) {
                int total = 0;
                if (sscanf(line + 16, "%d", &total) == 1 && total > 0) {
                    bHugePagesAvailable = true;
                }
                break;
            }
        }
        fclose(fp);
    }
#endif

    if (bHugePagesAvailable) {
        MR_LOG_INFO("Huge pages are available on this system");
    } else {
        MR_LOG_INFO("Huge pages not available (requires privileges or system configuration)");
    }
}

} // namespace MonsterRender

