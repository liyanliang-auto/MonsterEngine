// Copyright Monster Engine. All Rights Reserved.

/**
 * @file ImGuiRenderer.cpp
 * @brief Implementation of ImGui RHI renderer
 */

#include "Editor/ImGui/ImGuiRenderer.h"
#include "Core/Logging/LogMacros.h"
#include "Core/ShaderCompiler.h"
#include "RHI/RHIResources.h"
#include "Platform/Vulkan/VulkanTexture.h"

// ImGui includes
#include "imgui.h"

#include <cstring>
#include <fstream>
#include <vector>

// Bring ShaderCompiler into scope
using MonsterRender::ShaderCompiler;

namespace MonsterEngine
{
namespace Editor
{

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogImGuiRenderer, Log, All);

// ImGui uniform buffer structure (projection matrix only)
struct ImGuiUniformBuffer
{
    float ProjectionMatrix[16];
};

// ============================================================================
// Construction / Destruction
// ============================================================================

FImGuiRenderer::FImGuiRenderer()
    : Device(nullptr)
    , RHIBackend(MonsterRender::RHI::ERHIBackend::Unknown)
    , VertexBufferSize(0)
    , IndexBufferSize(0)
    , WindowWidth(1280)
    , WindowHeight(720)
    , bInitialized(false)
    , NextTextureID(1)  // Start from 1, 0 is reserved for font texture
{
    MR_LOG(LogImGuiRenderer, Log, "FImGuiRenderer created");
}

FImGuiRenderer::~FImGuiRenderer()
{
    Shutdown();
    MR_LOG(LogImGuiRenderer, Log, "FImGuiRenderer destroyed");
}

// ============================================================================
// Initialization / Shutdown
// ============================================================================

bool FImGuiRenderer::Initialize(MonsterRender::RHI::IRHIDevice* InDevice)
{
    if (bInitialized)
    {
        MR_LOG(LogImGuiRenderer, Warning, "ImGui renderer already initialized");
        return true;
    }

    if (!InDevice)
    {
        MR_LOG(LogImGuiRenderer, Error, "Cannot initialize ImGui renderer: null device");
        return false;
    }

    Device = InDevice;
    RHIBackend = Device->getRHIBackend();

    MR_LOG(LogImGuiRenderer, Log, "Initializing ImGui renderer...");
    MR_LOG(LogImGuiRenderer, Log, "  RHI Backend: %s", MonsterRender::RHI::GetRHIBackendName(RHIBackend));

    // Create font texture
    if (!CreateFontTexture())
    {
        MR_LOG(LogImGuiRenderer, Error, "Failed to create font texture");
        return false;
    }

    // Create shaders
    if (!CreateShaders())
    {
        MR_LOG(LogImGuiRenderer, Error, "Failed to create shaders");
        return false;
    }

    // Create pipeline state
    if (!CreatePipelineState())
    {
        MR_LOG(LogImGuiRenderer, Error, "Failed to create pipeline state");
        return false;
    }

    // Create uniform buffer
    MonsterRender::RHI::BufferDesc ubDesc;
    ubDesc.size = sizeof(ImGuiUniformBuffer);
    ubDesc.usage = MonsterRender::RHI::EResourceUsage::UniformBuffer;
    ubDesc.cpuAccessible = true;
    ubDesc.debugName = "ImGui Uniform Buffer";
    UniformBuffer = Device->createBuffer(ubDesc);
    if (!UniformBuffer)
    {
        MR_LOG(LogImGuiRenderer, Error, "Failed to create uniform buffer");
        return false;
    }

    bInitialized = true;
    MR_LOG(LogImGuiRenderer, Log, "ImGui renderer initialized successfully");

    return true;
}

void FImGuiRenderer::Shutdown()
{
    if (!bInitialized)
    {
        return;
    }

    MR_LOG(LogImGuiRenderer, Log, "Shutting down ImGui renderer...");

    // Release GPU resources
    PipelineState.Reset();
    PixelShader.Reset();
    VertexShader.Reset();
    FontSampler.Reset();
    FontTexture.Reset();
    UniformBuffer.Reset();
    IndexBuffer.Reset();
    VertexBuffer.Reset();

    Device = nullptr;
    bInitialized = false;

    MR_LOG(LogImGuiRenderer, Log, "ImGui renderer shutdown complete");
}

// ============================================================================
// Rendering
// ============================================================================

void FImGuiRenderer::RenderDrawData(MonsterRender::RHI::IRHICommandList* CmdList, ImDrawData* DrawData)
{
    if (!bInitialized || !CmdList || !DrawData)
    {
        return;
    }

    // Avoid rendering when minimized
    if (DrawData->DisplaySize.x <= 0.0f || DrawData->DisplaySize.y <= 0.0f)
    {
        return;
    }

    // Create or resize buffers if needed
    if (DrawData->TotalVtxCount > 0)
    {
        if (!CreateOrResizeBuffers(
            static_cast<uint32>(DrawData->TotalVtxCount),
            static_cast<uint32>(DrawData->TotalIdxCount)))
        {
            return;
        }

        // Update buffers with draw data
        UpdateBuffers(DrawData);
    }

    // Setup render state
    SetupRenderState(CmdList, DrawData);

    // Render command lists
    ImVec2 clipOff = DrawData->DisplayPos;
    ImVec2 clipScale = DrawData->FramebufferScale;

    int globalVtxOffset = 0;
    int globalIdxOffset = 0;

    for (int n = 0; n < DrawData->CmdListsCount; n++)
    {
        const ImDrawList* cmdList = DrawData->CmdLists[n];

        for (int cmdIdx = 0; cmdIdx < cmdList->CmdBuffer.Size; cmdIdx++)
        {
            const ImDrawCmd* pcmd = &cmdList->CmdBuffer[cmdIdx];

            if (pcmd->UserCallback != nullptr)
            {
                // User callback (used for custom rendering)
                if (pcmd->UserCallback != ImDrawCallback_ResetRenderState)
                {
                    pcmd->UserCallback(cmdList, pcmd);
                }
                else
                {
                    SetupRenderState(CmdList, DrawData);
                }
            }
            else
            {
                // Calculate clip rectangle
                ImVec2 clipMin((pcmd->ClipRect.x - clipOff.x) * clipScale.x,
                               (pcmd->ClipRect.y - clipOff.y) * clipScale.y);
                ImVec2 clipMax((pcmd->ClipRect.z - clipOff.x) * clipScale.x,
                               (pcmd->ClipRect.w - clipOff.y) * clipScale.y);

                // Clamp to viewport
                if (clipMin.x < 0.0f) clipMin.x = 0.0f;
                if (clipMin.y < 0.0f) clipMin.y = 0.0f;
                if (clipMax.x > DrawData->DisplaySize.x) clipMax.x = DrawData->DisplaySize.x;
                if (clipMax.y > DrawData->DisplaySize.y) clipMax.y = DrawData->DisplaySize.y;

                if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y)
                {
                    continue;
                }

                // Set scissor rect
                MonsterRender::RHI::ScissorRect scissor;
                scissor.left = static_cast<int32>(clipMin.x);
                scissor.top = static_cast<int32>(clipMin.y);
                scissor.right = static_cast<int32>(clipMax.x);
                scissor.bottom = static_cast<int32>(clipMax.y);
                CmdList->setScissorRect(scissor);

                // Bind texture based on TextureId
                // TextureId 0 = font texture, other IDs are registered custom textures
                // Note: ImGui shader uses binding = 1 for texture (binding = 0 is uniform buffer)
                ImTextureID texId = pcmd->GetTexID();
                uint64 textureIdValue = static_cast<uint64>(texId);
                
                if (textureIdValue == 0)
                {
                    // Use font texture at binding slot 1
                    CmdList->setShaderResource(1, FontTexture);
                }
                else
                {
                    // Look up registered texture
                    auto* foundTexture = RegisteredTextures.Find(textureIdValue);
                    if (foundTexture && *foundTexture)
                    {
                        CmdList->setShaderResource(1, *foundTexture);
                    }
                    else
                    {
                        // Fallback to font texture if not found
                        CmdList->setShaderResource(1, FontTexture);
                    }
                }

                // Draw indexed
                CmdList->drawIndexed(
                    pcmd->ElemCount,
                    pcmd->IdxOffset + globalIdxOffset,
                    pcmd->VtxOffset + globalVtxOffset);
            }
        }

        globalIdxOffset += cmdList->IdxBuffer.Size;
        globalVtxOffset += cmdList->VtxBuffer.Size;
    }
}

void FImGuiRenderer::OnWindowResize(uint32 Width, uint32 Height)
{
    WindowWidth = Width;
    WindowHeight = Height;
}

// ============================================================================
// Resource Creation
// ============================================================================

bool FImGuiRenderer::CreateFontTexture()
{
    MR_LOG(LogImGuiRenderer, Log, "Creating font texture...");

    ImGuiIO& io = ImGui::GetIO();

    // Get font texture data
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    // Create texture with initial data
    MonsterRender::RHI::TextureDesc texDesc;
    texDesc.width = static_cast<uint32>(width);
    texDesc.height = static_cast<uint32>(height);
    texDesc.depth = 1;
    texDesc.mipLevels = 1;
    texDesc.arraySize = 1;
    texDesc.format = MonsterRender::RHI::EPixelFormat::R8G8B8A8_UNORM;
    texDesc.usage = MonsterRender::RHI::EResourceUsage::ShaderResource;
    texDesc.debugName = "ImGui Font Texture";
    texDesc.initialData = pixels;
    texDesc.initialDataSize = static_cast<uint32>(width * height * 4);

    FontTexture = Device->createTexture(texDesc);
    if (!FontTexture)
    {
        MR_LOG(LogImGuiRenderer, Error, "Failed to create font texture");
        return false;
    }

    // Create sampler
    MonsterRender::RHI::SamplerDesc samplerDesc;
    samplerDesc.filter = MonsterRender::RHI::ESamplerFilter::Bilinear;
    samplerDesc.addressU = MonsterRender::RHI::ESamplerAddressMode::Clamp;
    samplerDesc.addressV = MonsterRender::RHI::ESamplerAddressMode::Clamp;
    samplerDesc.addressW = MonsterRender::RHI::ESamplerAddressMode::Clamp;
    samplerDesc.debugName = "ImGui Font Sampler";

    FontSampler = Device->createSampler(samplerDesc);
    if (!FontSampler)
    {
        MR_LOG(LogImGuiRenderer, Error, "Failed to create font sampler");
        return false;
    }

    // Store texture ID in ImGui
    io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(FontTexture.Get()));

    // Transition font texture to SHADER_READ_ONLY_OPTIMAL layout
    // This is needed because the texture was created with initial data but layout is UNDEFINED
    if (RHIBackend == MonsterRender::RHI::ERHIBackend::Vulkan)
    {
        auto* vulkanTexture = static_cast<MonsterRender::RHI::Vulkan::VulkanTexture*>(FontTexture.Get());
        if (vulkanTexture)
        {
            // Set the layout to SHADER_READ_ONLY_OPTIMAL since the texture upload already handled the transition
            vulkanTexture->setCurrentLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            MR_LOG(LogImGuiRenderer, Log, "Font texture layout set to SHADER_READ_ONLY_OPTIMAL");
        }
    }

    MR_LOG(LogImGuiRenderer, Log, "Font texture created (%dx%d)", width, height);
    return true;
}

bool FImGuiRenderer::CreateShaders()
{
    MR_LOG(LogImGuiRenderer, Log, "Creating ImGui shaders for %s...", 
           MonsterRender::RHI::GetRHIBackendName(RHIBackend));

    if (RHIBackend == MonsterRender::RHI::ERHIBackend::Vulkan)
    {
        // Load SPIR-V shaders
        std::vector<uint8> vsSpv = ShaderCompiler::readFileBytes("Shaders/ImGui.vert.spv");
        std::vector<uint8> psSpv = ShaderCompiler::readFileBytes("Shaders/ImGui.frag.spv");

        if (vsSpv.empty() || psSpv.empty())
        {
            MR_LOG(LogImGuiRenderer, Error, "Failed to load ImGui SPIR-V shaders");
            return false;
        }

        VertexShader = Device->createVertexShader(vsSpv);
        PixelShader = Device->createPixelShader(psSpv);
    }
    else if (RHIBackend == MonsterRender::RHI::ERHIBackend::OpenGL)
    {
        // Load GLSL shaders
        std::ifstream vsFile("Shaders/ImGui_GL.vert", std::ios::binary);
        std::ifstream psFile("Shaders/ImGui_GL.frag", std::ios::binary);

        if (!vsFile.is_open() || !psFile.is_open())
        {
            MR_LOG(LogImGuiRenderer, Error, "Failed to open ImGui GLSL shader files");
            return false;
        }

        std::string vsSource((std::istreambuf_iterator<char>(vsFile)), std::istreambuf_iterator<char>());
        std::string psSource((std::istreambuf_iterator<char>(psFile)), std::istreambuf_iterator<char>());

        std::vector<uint8> vsData(vsSource.begin(), vsSource.end());
        vsData.push_back(0);
        std::vector<uint8> psData(psSource.begin(), psSource.end());
        psData.push_back(0);

        VertexShader = Device->createVertexShader(vsData);
        PixelShader = Device->createPixelShader(psData);
    }
    else
    {
        MR_LOG(LogImGuiRenderer, Error, "Unsupported RHI backend for ImGui");
        return false;
    }

    if (!VertexShader || !PixelShader)
    {
        MR_LOG(LogImGuiRenderer, Error, "Failed to create ImGui shaders");
        return false;
    }

    MR_LOG(LogImGuiRenderer, Log, "ImGui shaders created successfully");
    return true;
}

bool FImGuiRenderer::CreatePipelineState()
{
    MR_LOG(LogImGuiRenderer, Log, "Creating ImGui pipeline state...");

    if (!VertexShader || !PixelShader)
    {
        return false;
    }

    MonsterRender::RHI::EPixelFormat renderTargetFormat = Device->getSwapChainFormat();

    MonsterRender::RHI::PipelineStateDesc pipelineDesc;
    pipelineDesc.vertexShader = VertexShader;
    pipelineDesc.pixelShader = PixelShader;
    pipelineDesc.primitiveTopology = MonsterRender::RHI::EPrimitiveTopology::TriangleList;

    // Vertex layout matching ImDrawVert structure
    // struct ImDrawVert { ImVec2 pos; ImVec2 uv; ImU32 col; };
    MonsterRender::RHI::VertexAttribute posAttr;
    posAttr.location = 0;
    posAttr.format = MonsterRender::RHI::EVertexFormat::Float2;
    posAttr.offset = offsetof(ImDrawVert, pos);
    posAttr.semanticName = "POSITION";

    MonsterRender::RHI::VertexAttribute uvAttr;
    uvAttr.location = 1;
    uvAttr.format = MonsterRender::RHI::EVertexFormat::Float2;
    uvAttr.offset = offsetof(ImDrawVert, uv);
    uvAttr.semanticName = "TEXCOORD";

    MonsterRender::RHI::VertexAttribute colAttr;
    colAttr.location = 2;
    colAttr.format = MonsterRender::RHI::EVertexFormat::UByte4Norm;
    colAttr.offset = offsetof(ImDrawVert, col);
    colAttr.semanticName = "COLOR";

    pipelineDesc.vertexLayout.attributes.push_back(posAttr);
    pipelineDesc.vertexLayout.attributes.push_back(uvAttr);
    pipelineDesc.vertexLayout.attributes.push_back(colAttr);
    pipelineDesc.vertexLayout.stride = sizeof(ImDrawVert);

    // Rasterizer state - no culling for UI
    pipelineDesc.rasterizerState.fillMode = MonsterRender::RHI::EFillMode::Solid;
    pipelineDesc.rasterizerState.cullMode = MonsterRender::RHI::ECullMode::None;
    pipelineDesc.rasterizerState.frontCounterClockwise = false;
    pipelineDesc.rasterizerState.scissorEnable = true;

    // Depth stencil state - no depth testing for UI
    pipelineDesc.depthStencilState.depthEnable = false;
    pipelineDesc.depthStencilState.depthWriteEnable = false;

    // Blend state - alpha blending for UI
    pipelineDesc.blendState.blendEnable = true;
    pipelineDesc.blendState.srcColorBlend = MonsterRender::RHI::EBlendFactor::SrcAlpha;
    pipelineDesc.blendState.destColorBlend = MonsterRender::RHI::EBlendFactor::InvSrcAlpha;
    pipelineDesc.blendState.colorBlendOp = MonsterRender::RHI::EBlendOp::Add;
    pipelineDesc.blendState.srcAlphaBlend = MonsterRender::RHI::EBlendFactor::One;
    pipelineDesc.blendState.destAlphaBlend = MonsterRender::RHI::EBlendFactor::InvSrcAlpha;
    pipelineDesc.blendState.alphaBlendOp = MonsterRender::RHI::EBlendOp::Add;

    // Render target format
    pipelineDesc.renderTargetFormats.push_back(renderTargetFormat);
    pipelineDesc.debugName = "ImGui Pipeline";

    PipelineState = Device->createPipelineState(pipelineDesc);
    if (!PipelineState)
    {
        MR_LOG(LogImGuiRenderer, Error, "Failed to create ImGui pipeline state");
        return false;
    }

    MR_LOG(LogImGuiRenderer, Log, "ImGui pipeline state created successfully");
    return true;
}

bool FImGuiRenderer::CreateOrResizeBuffers(uint32 VertexCount, uint32 IndexCount)
{
    // Add some slack to avoid frequent reallocations
    uint32 requiredVtxSize = VertexCount + 5000;
    uint32 requiredIdxSize = IndexCount + 10000;

    // Resize vertex buffer if needed
    if (!VertexBuffer || VertexBufferSize < requiredVtxSize)
    {
        VertexBuffer.Reset();

        MonsterRender::RHI::BufferDesc vbDesc;
        vbDesc.size = requiredVtxSize * sizeof(ImDrawVert);
        vbDesc.usage = MonsterRender::RHI::EResourceUsage::VertexBuffer;
        vbDesc.cpuAccessible = true;
        vbDesc.debugName = "ImGui Vertex Buffer";

        VertexBuffer = Device->createBuffer(vbDesc);
        if (!VertexBuffer)
        {
            MR_LOG(LogImGuiRenderer, Error, "Failed to create vertex buffer");
            return false;
        }

        VertexBufferSize = requiredVtxSize;
    }

    // Resize index buffer if needed
    if (!IndexBuffer || IndexBufferSize < requiredIdxSize)
    {
        IndexBuffer.Reset();

        MonsterRender::RHI::BufferDesc ibDesc;
        ibDesc.size = requiredIdxSize * sizeof(ImDrawIdx);
        ibDesc.usage = MonsterRender::RHI::EResourceUsage::IndexBuffer;
        ibDesc.cpuAccessible = true;
        ibDesc.debugName = "ImGui Index Buffer";

        IndexBuffer = Device->createBuffer(ibDesc);
        if (!IndexBuffer)
        {
            MR_LOG(LogImGuiRenderer, Error, "Failed to create index buffer");
            return false;
        }

        IndexBufferSize = requiredIdxSize;
    }

    return true;
}

void FImGuiRenderer::UpdateBuffers(ImDrawData* DrawData)
{
    // Map vertex buffer
    ImDrawVert* vtxDst = static_cast<ImDrawVert*>(VertexBuffer->map());
    ImDrawIdx* idxDst = static_cast<ImDrawIdx*>(IndexBuffer->map());

    if (!vtxDst || !idxDst)
    {
        if (vtxDst) VertexBuffer->unmap();
        if (idxDst) IndexBuffer->unmap();
        return;
    }

    // Copy vertex and index data
    for (int n = 0; n < DrawData->CmdListsCount; n++)
    {
        const ImDrawList* cmdList = DrawData->CmdLists[n];
        std::memcpy(vtxDst, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof(ImDrawVert));
        std::memcpy(idxDst, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtxDst += cmdList->VtxBuffer.Size;
        idxDst += cmdList->IdxBuffer.Size;
    }

    VertexBuffer->unmap();
    IndexBuffer->unmap();
}

void FImGuiRenderer::SetupRenderState(MonsterRender::RHI::IRHICommandList* CmdList, ImDrawData* DrawData)
{
    // Update projection matrix in uniform buffer
    float L = DrawData->DisplayPos.x;
    float R = DrawData->DisplayPos.x + DrawData->DisplaySize.x;
    float T = DrawData->DisplayPos.y;
    float B = DrawData->DisplayPos.y + DrawData->DisplaySize.y;

    // Orthographic projection matrix
    ImGuiUniformBuffer ubo;
    std::memset(&ubo, 0, sizeof(ubo));

    // Column-major matrix for OpenGL/Vulkan
    ubo.ProjectionMatrix[0] = 2.0f / (R - L);
    ubo.ProjectionMatrix[5] = 2.0f / (T - B);  // Flip Y for Vulkan
    ubo.ProjectionMatrix[10] = -1.0f;
    ubo.ProjectionMatrix[12] = (R + L) / (L - R);
    ubo.ProjectionMatrix[13] = (T + B) / (B - T);
    ubo.ProjectionMatrix[15] = 1.0f;

    // Adjust for Vulkan's flipped Y
    if (RHIBackend == MonsterRender::RHI::ERHIBackend::Vulkan)
    {
        ubo.ProjectionMatrix[5] = 2.0f / (B - T);
        ubo.ProjectionMatrix[13] = (T + B) / (T - B);
    }

    // Upload uniform buffer
    void* mappedData = UniformBuffer->map();
    if (mappedData)
    {
        std::memcpy(mappedData, &ubo, sizeof(ImGuiUniformBuffer));
        UniformBuffer->unmap();
    }

    // Set pipeline state
    CmdList->setPipelineState(PipelineState);

    // Bind uniform buffer
    CmdList->setConstantBuffer(0, UniformBuffer);

    // Bind vertex buffer
    TArray<TSharedPtr<MonsterRender::RHI::IRHIBuffer>> vertexBuffers;
    vertexBuffers.Add(VertexBuffer);
    CmdList->setVertexBuffers(0, vertexBuffers);

    // Bind index buffer (is32Bit = true for 32-bit indices, false for 16-bit)
    CmdList->setIndexBuffer(IndexBuffer, sizeof(ImDrawIdx) == 4);

    // Set viewport
    MonsterRender::RHI::Viewport viewport(DrawData->DisplaySize.x, DrawData->DisplaySize.y);
    CmdList->setViewport(viewport);
}

// ============================================================================
// Texture Registration for ImGui::Image
// ============================================================================

ImTextureID FImGuiRenderer::RegisterTexture(TSharedPtr<MonsterRender::RHI::IRHITexture> Texture)
{
    if (!Texture)
    {
        MR_LOG(LogImGuiRenderer, Warning, "Attempted to register null texture");
        return static_cast<ImTextureID>(0);
    }

    // Assign a unique ID to this texture
    uint64 textureId = NextTextureID++;
    RegisteredTextures.Add(textureId, Texture);

    MR_LOG(LogImGuiRenderer, Log, "Registered texture with ID %llu", textureId);

    return static_cast<ImTextureID>(textureId);
}

void FImGuiRenderer::UnregisterTexture(ImTextureID TextureID)
{
    uint64 textureIdValue = static_cast<uint64>(TextureID);
    
    if (textureIdValue == 0)
    {
        MR_LOG(LogImGuiRenderer, Warning, "Cannot unregister font texture (ID 0)");
        return;
    }

    if (RegisteredTextures.Remove(textureIdValue) > 0)
    {
        MR_LOG(LogImGuiRenderer, Log, "Unregistered texture with ID %llu", textureIdValue);
    }
    else
    {
        MR_LOG(LogImGuiRenderer, Warning, "Texture ID %llu not found in registry", textureIdValue);
    }
}

TSharedPtr<MonsterRender::RHI::IRHITexture> FImGuiRenderer::GetTextureByID(ImTextureID TextureID) const
{
    uint64 textureIdValue = static_cast<uint64>(TextureID);
    
    if (textureIdValue == 0)
    {
        return FontTexture;
    }

    const auto* foundTexture = RegisteredTextures.Find(textureIdValue);
    if (foundTexture)
    {
        return *foundTexture;
    }

    return nullptr;
}

} // namespace Editor
} // namespace MonsterEngine
