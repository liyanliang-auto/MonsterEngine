// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Memory Manager Implementation

#include "Core/HAL/FMemoryManager.h"
#include "Core/Log.h"

#if PLATFORM_WINDOWS
    #include <Windows.h>
#elif PLATFORM_LINUX
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
    if (!bInitialized) return;

    MR_LOG_INFO("Shutting down FMemoryManager...");

    // Release allocator
    Allocator.reset();

    bInitialized = false;
    MR_LOG_INFO("FMemoryManager shut down");
}

void FMemoryManager::SetAllocator(TUniquePtr<FMalloc> NewAllocator) {
    if (!NewAllocator) {
        MR_LOG_ERROR("Cannot set null allocator");
        return;
    }

    Allocator = std::move(NewAllocator);
    MR_LOG_INFO("Custom allocator set");
}

void FMemoryManager::GetGlobalMemoryStats(FGlobalMemoryStats& OutStats) {
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

#elif PLATFORM_LINUX
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        OutStats.TotalPhysicalMemory = info.totalram * info.mem_unit;
        OutStats.AvailablePhysicalMemory = info.freeram * info.mem_unit;
        OutStats.TotalVirtualMemory = info.totalswap * info.mem_unit;
        OutStats.AvailableVirtualMemory = info.freeswap * info.mem_unit;
    }

    OutStats.PageSize = sysconf(_SC_PAGESIZE);
    OutStats.LargePageSize = 2 * 1024 * 1024;  // 2MB typical

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

