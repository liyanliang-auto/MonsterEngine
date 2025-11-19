#include "TriangleRenderer.h"
#include "Core/Log.h"
#include "Core/ShaderCompiler.h"
#include "Platform/Vulkan/VulkanPipelineState.h"
#include "Platform/Vulkan/VulkanDevice.h"

namespace MonsterRender {

    bool TriangleRenderer::initialize(RHI::IRHIDevice* device) {
        m_device = device;
        
        return createVertexBuffer() && 
               createShaders() && 
               createPipelineState();
    }
    
    void TriangleRenderer::render(RHI::IRHICommandList* cmdList) {
        if (!cmdList || !m_pipelineState) {
            MR_LOG_ERROR("Cannot render: cmdList invalid or pipelineState invalid");
            return;
        }
        
        MR_LOG_DEBUG("TriangleRenderer::render() called");

        // Set up rendering state
        MR_LOG_DEBUG("Setting pipeline state");
        cmdList->setPipelineState(m_pipelineState);
        
        MR_LOG_DEBUG("Setting vertex buffers");
        TArray<TSharedPtr<RHI::IRHIBuffer>> vertexBuffers = {m_vertexBuffer};
        cmdList->setVertexBuffers(0, TSpan<TSharedPtr<RHI::IRHIBuffer>>(vertexBuffers));
        
        // Set viewport and scissor (matching window size)
        MR_LOG_DEBUG("Setting viewport to 1280x720");
        RHI::Viewport viewport(1280.0f, 720.0f);
        cmdList->setViewport(viewport);
        
        RHI::ScissorRect scissor;
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = 1280;
        scissor.bottom = 720;
        cmdList->setScissorRect(scissor);
        
        // Draw the triangle
        MR_LOG_DEBUG("Drawing 3 vertices");
        cmdList->draw(3); // 3 vertices
        MR_LOG_DEBUG("TriangleRenderer::render() completed");
    }
    
    bool TriangleRenderer::createVertexBuffer() {
        // Define triangle vertices (NDC coordinates)
        TArray<Vertex> vertices = {
            {{0.0f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},  // Bottom center - Red
            {{0.5f,  0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},  // Top right - Green
            {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}}   // Top left - Blue
        };
        
        // Create vertex buffer
        RHI::BufferDesc vertexBufferDesc;
        vertexBufferDesc.size = static_cast<uint32>(sizeof(Vertex) * vertices.size());
        vertexBufferDesc.usage = RHI::EResourceUsage::VertexBuffer;
        vertexBufferDesc.cpuAccessible = true;
        vertexBufferDesc.debugName = "Triangle Vertex Buffer";
        
        m_vertexBuffer = m_device->createBuffer(vertexBufferDesc);
        if (!m_vertexBuffer) {
            MR_LOG_ERROR("Failed to create vertex buffer");
            return false;
        }
        
        // Upload vertex data
        void* mappedData = m_vertexBuffer->map();
        if (!mappedData) {
            MR_LOG_ERROR("Failed to map vertex buffer");
            return false;
        }
        
        std::memcpy(mappedData, vertices.data(), vertexBufferDesc.size);
        m_vertexBuffer->unmap();
        
        MR_LOG_INFO("Triangle vertex buffer created successfully");
        return true;
    }
    
    bool TriangleRenderer::createShaders() {
        // Load pre-compiled SPV files
        String vsPath = "Shaders/Triangle.vert.spv";
        String psPath = "Shaders/Triangle.frag.spv";

        // Read vertex shader SPV
        TArray<uint8> vsSpv = ShaderCompiler::readFileBytes(vsPath);
        if (vsSpv.empty()) {
            MR_LOG_ERROR("Failed to load vertex shader: " + vsPath);
            return false;
        }
        
        // Read fragment shader SPV
        TArray<uint8> psSpv = ShaderCompiler::readFileBytes(psPath);
        if (psSpv.empty()) {
            MR_LOG_ERROR("Failed to load fragment shader: " + psPath);
            return false;
        }

        m_vertexShader = m_device->createVertexShader(TSpan<const uint8>(vsSpv.data(), vsSpv.size()));
        if (!m_vertexShader) return false;
        m_pixelShader = m_device->createPixelShader(TSpan<const uint8>(psSpv.data(), psSpv.size()));
        if (!m_pixelShader) return false;

        MR_LOG_INFO("Triangle shaders loaded successfully");
        return true;
    }
    
    bool TriangleRenderer::createPipelineState() {
        if (!m_vertexShader || !m_pixelShader) {
            MR_LOG_ERROR("Cannot create pipeline state without valid shaders");
            return false;
        }
        
        MR_LOG_INFO("=== Creating Pipeline State ===");
        
        // Get swapchain format from device
        auto* vulkanDevice = dynamic_cast<MonsterRender::RHI::Vulkan::VulkanDevice*>(m_device);
        if (!vulkanDevice) {
            MR_LOG_ERROR("Device is not a VulkanDevice!");
            return false;
        }
        
        VkFormat swapchainFormat = vulkanDevice->getSwapchainFormat();
        MR_LOG_INFO("Using swapchain format: " + std::to_string(swapchainFormat));
        
        // Convert VkFormat to EPixelFormat
        RHI::EPixelFormat renderTargetFormat = RHI::EPixelFormat::R8G8B8A8_SRGB; // Default
        if (swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB) {
            renderTargetFormat = RHI::EPixelFormat::B8G8R8A8_SRGB;
        } else if (swapchainFormat == VK_FORMAT_B8G8R8A8_UNORM) {
            renderTargetFormat = RHI::EPixelFormat::B8G8R8A8_UNORM;
        } else if (swapchainFormat == VK_FORMAT_R8G8B8A8_SRGB) {
            renderTargetFormat = RHI::EPixelFormat::R8G8B8A8_SRGB;
        } else if (swapchainFormat == VK_FORMAT_R8G8B8A8_UNORM) {
            renderTargetFormat = RHI::EPixelFormat::R8G8B8A8_UNORM;
        }
        
        MR_LOG_INFO("Converted to EPixelFormat: " + std::to_string(static_cast<int>(renderTargetFormat)));
        
        RHI::PipelineStateDesc pipelineDesc;
        pipelineDesc.vertexShader = m_vertexShader;
        pipelineDesc.pixelShader = m_pixelShader;
        pipelineDesc.primitiveTopology = RHI::EPrimitiveTopology::TriangleList;
        
        // Set up basic render state
        pipelineDesc.rasterizerState.fillMode = RHI::EFillMode::Solid;
        pipelineDesc.rasterizerState.cullMode = RHI::ECullMode::None;
        pipelineDesc.rasterizerState.frontCounterClockwise = false;
        
        // Disable depth testing for this simple triangle
        pipelineDesc.depthStencilState.depthEnable = false;
        pipelineDesc.depthStencilState.depthWriteEnable = false;
        
        // Disable blending
        pipelineDesc.blendState.blendEnable = false;
        
        // Set up render target format - MUST match swapchain format
        pipelineDesc.renderTargetFormats.push_back(renderTargetFormat);
        
        pipelineDesc.debugName = "Triangle Pipeline State";
        
        MR_LOG_INFO("Calling device->createPipelineState()...");
        m_pipelineState = m_device->createPipelineState(pipelineDesc);
        if (!m_pipelineState) {
            MR_LOG_ERROR("Failed to create pipeline state - device returned nullptr");
            return false;
        }
        
        MR_LOG_INFO("Pipeline state object created, verifying...");
        
        // Verify the pipeline is valid
        auto* vulkanPipeline = dynamic_cast<MonsterRender::RHI::Vulkan::VulkanPipelineState*>(m_pipelineState.get());
        if (!vulkanPipeline) {
            MR_LOG_ERROR("Pipeline state is not a VulkanPipelineState!");
            return false;
        }
        
        VkPipeline vkPipeline = vulkanPipeline->getPipeline();
        if (vkPipeline == VK_NULL_HANDLE) {
            MR_LOG_ERROR("Pipeline handle is NULL after creation!");
            return false;
        }
        
        MR_LOG_INFO("Triangle pipeline state created successfully");
        MR_LOG_INFO("  Pipeline handle: " + std::to_string(reinterpret_cast<uint64>(vkPipeline)));
        return true;
    }

}