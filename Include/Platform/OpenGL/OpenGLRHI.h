#pragma once

/**
 * OpenGLRHI.h
 * 
 * Main header for OpenGL 4.6 RHI backend
 * Include this file to use the OpenGL RHI implementation
 * 
 * Reference: UE5 OpenGLDrv/Public/OpenGLDrv.h
 */

// OpenGL definitions and constants
#include "Platform/OpenGL/OpenGLDefinitions.h"

// OpenGL function loader
#include "Platform/OpenGL/OpenGLFunctions.h"

// OpenGL context management
#include "Platform/OpenGL/OpenGLContext.h"

// OpenGL resources (Buffer, Texture, Sampler, Framebuffer)
#include "Platform/OpenGL/OpenGLResources.h"

// OpenGL shaders and programs
#include "Platform/OpenGL/OpenGLShaders.h"

// OpenGL pipeline state
#include "Platform/OpenGL/OpenGLPipeline.h"

// OpenGL device
#include "Platform/OpenGL/OpenGLDevice.h"

// OpenGL command list
#include "Platform/OpenGL/OpenGLCommandList.h"

namespace MonsterEngine::OpenGL {

/**
 * OpenGL RHI version information
 */
struct FOpenGLRHIVersion
{
    static constexpr int32 MajorVersion = 4;
    static constexpr int32 MinorVersion = 6;
    static constexpr const char* VersionString = "OpenGL 4.6 Core Profile";
    static constexpr const char* GLSLVersion = "460";
};

/**
 * Initialize the OpenGL RHI subsystem
 * This should be called before creating any OpenGL resources
 * 
 * @param windowHandle Native window handle (HWND on Windows)
 * @param config Optional context configuration
 * @return Shared pointer to the OpenGL device, or nullptr on failure
 */
inline TSharedPtr<FOpenGLDevice> InitializeOpenGLRHI(void* windowHandle, 
                                                      const FOpenGLContextConfig& config = FOpenGLContextConfig())
{
    return CreateOpenGLDevice(windowHandle, config);
}

/**
 * Check if OpenGL 4.6 is supported on this system
 * This creates a temporary context to check support
 * 
 * @return true if OpenGL 4.6 is supported
 */
bool IsOpenGL46Supported();

/**
 * Get the OpenGL version string from the driver
 * Must be called after context creation
 */
inline FString GetOpenGLVersionString()
{
    return FOpenGLContextManager::Get().GetVersionString();
}

/**
 * Get the OpenGL vendor string
 * Must be called after context creation
 */
inline FString GetOpenGLVendorString()
{
    return FOpenGLContextManager::Get().GetVendorString();
}

/**
 * Get the OpenGL renderer string
 * Must be called after context creation
 */
inline FString GetOpenGLRendererString()
{
    return FOpenGLContextManager::Get().GetRendererString();
}

/**
 * Get the GLSL version string
 * Must be called after context creation
 */
inline FString GetGLSLVersionString()
{
    return FOpenGLContextManager::Get().GetGLSLVersionString();
}

} // namespace MonsterEngine::OpenGL

// Convenience namespace alias
namespace MR_GL = MonsterEngine::OpenGL;
