#include "TriangleRenderer.h"
#include "Core/Log.h"

namespace MonsterRender {

    bool TriangleRenderer::initialize(RHI::IRHIDevice* device) {
        m_device = device;
        
        return createVertexBuffer() && 
               createShaders() && 
               createPipelineState();
    }
    
    void TriangleRenderer::render(RHI::IRHICommandList* cmdList) {
        if (!cmdList || !m_pipelineState) {
            return;
        }
        
        // Set up rendering state
        cmdList->setPipelineState(m_pipelineState);
        TArray<TSharedPtr<RHI::IRHIBuffer>> vertexBuffers = {m_vertexBuffer};
        cmdList->setVertexBuffers(0, TSpan<TSharedPtr<RHI::IRHIBuffer>>(vertexBuffers));
        
        // Set viewport (matching window size)
        RHI::Viewport viewport(1280.0f, 720.0f);
        cmdList->setViewport(viewport);
        
        // Draw the triangle
        cmdList->draw(3); // 3 vertices
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
        // Simple vertex shader SPIR-V bytecode
        // This is a minimal valid SPIR-V shader for demonstration
        TArray<uint8> vertexShaderCode = {
            // SPIR-V magic number and minimal header
            0x03, 0x02, 0x23, 0x07,  // Magic number
            0x00, 0x00, 0x01, 0x00,  // Version 1.0
            0x0A, 0x00, 0x0B, 0x00,  // Generator magic number
            0x10, 0x00, 0x00, 0x00,  // Bound on all ids
            0x00, 0x00, 0x00, 0x00,  // Reserved, must be 0
            
            // OpCapability Shader
            0x11, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00,
            
            // OpMemoryModel Logical GLSL450
            0x0E, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
            
            // OpEntryPoint Vertex %main "main"
            0x0F, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
            
            // OpName %main "main"
            0x05, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x00, 0x6D, 0x61, 0x69, 0x6E,
            
            // OpTypeVoid %void
            0x13, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00,
            
            // OpTypeFunction %void_func %void
            0x21, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
            
            // OpFunction %void %main None %void_func
            0x36, 0x00, 0x05, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
            
            // OpLabel
            0xF8, 0x00, 0x02, 0x00, 0x05, 0x00, 0x00, 0x00,
            
            // OpReturn
            0xFD, 0x00, 0x01, 0x00,
            
            // OpFunctionEnd
            0x38, 0x00, 0x01, 0x00
        };
        
        TArray<uint8> fragmentShaderCode = {
            // SPIR-V magic number and minimal header
            0x03, 0x02, 0x23, 0x07,  // Magic number
            0x00, 0x00, 0x01, 0x00,  // Version 1.0
            0x0A, 0x00, 0x0B, 0x00,  // Generator magic number
            0x10, 0x00, 0x00, 0x00,  // Bound on all ids
            0x00, 0x00, 0x00, 0x00,  // Reserved, must be 0
            
            // OpCapability Shader
            0x11, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00,
            
            // OpMemoryModel Logical GLSL450
            0x0E, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
            
            // OpEntryPoint Fragment %main "main"
            0x0F, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
            
            // OpExecutionMode %main OriginUpperLeft
            0x10, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
            
            // OpName %main "main"
            0x05, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x00, 0x6D, 0x61, 0x69, 0x6E,
            
            // OpTypeVoid %void
            0x13, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00,
            
            // OpTypeFunction %void_func %void
            0x21, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
            
            // OpFunction %void %main None %void_func
            0x36, 0x00, 0x05, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
            
            // OpLabel
            0xF8, 0x00, 0x02, 0x00, 0x05, 0x00, 0x00, 0x00,
            
            // OpReturn
            0xFD, 0x00, 0x01, 0x00,
            
            // OpFunctionEnd
            0x38, 0x00, 0x01, 0x00
        };
        
        // Create vertex shader
        m_vertexShader = m_device->createVertexShader(TSpan<const uint8>(vertexShaderCode.data(), vertexShaderCode.size()));
        if (!m_vertexShader) {
            MR_LOG_ERROR("Failed to create vertex shader");
            return false;
        }
        
        // Create pixel shader
        m_pixelShader = m_device->createPixelShader(TSpan<const uint8>(fragmentShaderCode.data(), fragmentShaderCode.size()));
        if (!m_pixelShader) {
            MR_LOG_ERROR("Failed to create pixel shader");
            return false;
        }
        
        MR_LOG_INFO("Triangle shaders created successfully");
        return true;
    }
    
    bool TriangleRenderer::createPipelineState() {
        if (!m_vertexShader || !m_pixelShader) {
            MR_LOG_ERROR("Cannot create pipeline state without valid shaders");
            return false;
        }
        
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
        
        // Set up render target format (assuming RGBA8 SRGB)
        pipelineDesc.renderTargetFormats.push_back(RHI::EPixelFormat::R8G8B8A8_SRGB);
        
        pipelineDesc.debugName = "Triangle Pipeline State";
        
        m_pipelineState = m_device->createPipelineState(pipelineDesc);
        if (!m_pipelineState) {
            MR_LOG_ERROR("Failed to create pipeline state");
            return false;
        }
        
        MR_LOG_INFO("Triangle pipeline state created successfully");
        return true;
    }

}