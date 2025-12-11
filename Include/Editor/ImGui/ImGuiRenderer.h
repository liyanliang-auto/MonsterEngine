// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file ImGuiRenderer.h
 * @brief ImGui RHI renderer for the editor
 * 
 * Renders ImGui draw data using the engine's RHI abstraction layer.
 * Supports both Vulkan and OpenGL backends.
 * 
 * Reference: imgui_impl_vulkan.cpp, imgui_impl_opengl3.cpp
 */

#include "Core/CoreMinimal.h"
#include "Core/CoreTypes.h"
#include "Core/Templates/SharedPointer.h"
#include "Containers/Map.h"
#include "RHI/RHI.h"

// Include ImGui header for ImTextureID type
#include "imgui.h"

// Forward declarations for ImGui types
struct ImDrawData;
struct ImDrawVert;

namespace MonsterEngine
{
namespace Editor
{

/**
 * @class FImGuiRenderer
 * @brief Renders ImGui draw data using the engine's RHI
 * 
 * This class handles:
 * - GPU resource creation (vertex/index buffers, textures, shaders, pipeline)
 * - Font texture atlas creation
 * - ImGui draw data rendering
 * - Window resize handling
 */
class FImGuiRenderer
{
public:
    FImGuiRenderer();
    ~FImGuiRenderer();

    // Non-copyable
    FImGuiRenderer(const FImGuiRenderer&) = delete;
    FImGuiRenderer& operator=(const FImGuiRenderer&) = delete;

    /**
     * Initialize the ImGui renderer with RHI device
     * @param InDevice RHI device to use for rendering
     * @return True if initialization successful
     */
    bool Initialize(MonsterRender::RHI::IRHIDevice* InDevice);

    /**
     * Shutdown and release all GPU resources
     */
    void Shutdown();

    /**
     * Render ImGui draw data
     * @param CmdList Command list to record rendering commands
     * @param DrawData ImGui draw data from ImGui::GetDrawData()
     */
    void RenderDrawData(MonsterRender::RHI::IRHICommandList* CmdList, ImDrawData* DrawData);

    /**
     * Handle window resize
     * @param Width New window width
     * @param Height New window height
     */
    void OnWindowResize(uint32 Width, uint32 Height);

    /**
     * Check if renderer is initialized
     * @return True if initialized
     */
    bool IsInitialized() const { return bInitialized; }

    /**
     * Get the font texture for external use
     * @return Shared pointer to font texture
     */
    TSharedPtr<MonsterRender::RHI::IRHITexture> GetFontTexture() const { return FontTexture; }

    /**
     * Register a texture for use with ImGui::Image
     * @param Texture The texture to register
     * @return ImTextureID that can be used with ImGui::Image
     */
    ImTextureID RegisterTexture(TSharedPtr<MonsterRender::RHI::IRHITexture> Texture);

    /**
     * Unregister a previously registered texture
     * @param TextureID The ImTextureID returned from RegisterTexture
     */
    void UnregisterTexture(ImTextureID TextureID);

    /**
     * Get texture by ImTextureID
     * @param TextureID The ImTextureID
     * @return The texture, or nullptr if not found
     */
    TSharedPtr<MonsterRender::RHI::IRHITexture> GetTextureByID(ImTextureID TextureID) const;

private:
    /**
     * Create font texture atlas from ImGui font data
     * @return True if successful
     */
    bool CreateFontTexture();

    /**
     * Create shaders for ImGui rendering
     * Loads appropriate shaders based on RHI backend
     * @return True if successful
     */
    bool CreateShaders();

    /**
     * Create pipeline state for ImGui rendering
     * @return True if successful
     */
    bool CreatePipelineState();

    /**
     * Create or resize vertex/index buffers
     * @param VertexCount Required vertex count
     * @param IndexCount Required index count
     * @return True if successful
     */
    bool CreateOrResizeBuffers(uint32 VertexCount, uint32 IndexCount);

    /**
     * Update vertex and index buffers with draw data
     * @param DrawData ImGui draw data
     */
    void UpdateBuffers(ImDrawData* DrawData);

    /**
     * Setup render state for ImGui rendering
     * @param CmdList Command list
     * @param DrawData Draw data for viewport info
     */
    void SetupRenderState(MonsterRender::RHI::IRHICommandList* CmdList, ImDrawData* DrawData);

private:
    /** RHI device reference */
    MonsterRender::RHI::IRHIDevice* Device;

    /** Current RHI backend type */
    MonsterRender::RHI::ERHIBackend RHIBackend;

    // ========================================================================
    // GPU Resources
    // ========================================================================

    /** Vertex buffer for ImGui geometry */
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> VertexBuffer;

    /** Index buffer for ImGui geometry */
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> IndexBuffer;

    /** Uniform buffer for projection matrix */
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> UniformBuffer;

    /** Font texture atlas */
    TSharedPtr<MonsterRender::RHI::IRHITexture> FontTexture;

    /** Font texture sampler */
    TSharedPtr<MonsterRender::RHI::IRHISampler> FontSampler;

    /** Vertex shader */
    TSharedPtr<MonsterRender::RHI::IRHIVertexShader> VertexShader;

    /** Pixel/Fragment shader */
    TSharedPtr<MonsterRender::RHI::IRHIPixelShader> PixelShader;

    /** Pipeline state object */
    TSharedPtr<MonsterRender::RHI::IRHIPipelineState> PipelineState;

    // ========================================================================
    // Buffer Management
    // ========================================================================

    /** Current vertex buffer capacity in vertices */
    uint32 VertexBufferSize;

    /** Current index buffer capacity in indices */
    uint32 IndexBufferSize;

    // ========================================================================
    // Window State
    // ========================================================================

    /** Current window width */
    uint32 WindowWidth;

    /** Current window height */
    uint32 WindowHeight;

    // ========================================================================
    // State
    // ========================================================================

    /** Initialization flag */
    bool bInitialized;

    // ========================================================================
    // Texture Registry for ImGui::Image
    // ========================================================================

    /** Map of registered textures by ID */
    TMap<uint64, TSharedPtr<MonsterRender::RHI::IRHITexture>> RegisteredTextures;

    /** Next available texture ID */
    uint64 NextTextureID;
};

} // namespace Editor
} // namespace MonsterEngine
