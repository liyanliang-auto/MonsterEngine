// Copyright Monster Engine. All Rights Reserved.

#include "Tests/TestCommandListPool.h"
#include "Core/Log.h"

/**
 * Standalone test program entry point for command list pool
 */
int main(int argc, char* argv[]) {
    MR_LOG_INFO("Starting Command List Pool Test Program");
    
    int result = RunCommandListPoolTests();
    
    if (result == 0) {
        MR_LOG_INFO("Command List Pool Test Program completed successfully");
    } else {
        MR_LOG_ERROR("Command List Pool Test Program failed with code: " + std::to_string(result));
    }
    
    return result;
}
