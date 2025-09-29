#include "TriangleRenderer.h"
#include "Core/Log.h"
#include "Core/ShaderCompiler.h"

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
        // Compile GLSL from Shaders directory
        ShaderCompileOptions vsOpt; vsOpt.language = EShaderLanguage::GLSL; vsOpt.stage = EShaderStageKind::Vertex; vsOpt.entryPoint = "main";
        ShaderCompileOptions psOpt = vsOpt; psOpt.stage = EShaderStageKind::Fragment;

        String vsPath = "Shaders/Triangle.vert";
        String psPath = "Shaders/Triangle.frag";

        auto vsSpv = ShaderCompiler::compileFromFile(vsPath, vsOpt);
        if (vsSpv.empty()) {
            MR_LOG_ERROR("Failed to compile vertex shader: " + vsPath);
            return false;
        }
        auto psSpv = ShaderCompiler::compileFromFile(psPath, psOpt);
        if (psSpv.empty()) {
            MR_LOG_ERROR("Failed to compile fragment shader: " + psPath);
            return false;
        }

        m_vertexShader = m_device->createVertexShader(TSpan<const uint8>(vsSpv.data(), vsSpv.size()));
        if (!m_vertexShader) return false;
        m_pixelShader = m_device->createPixelShader(TSpan<const uint8>(psSpv.data(), psSpv.size()));
        if (!m_pixelShader) return false;

        MR_LOG_INFO("Triangle shaders compiled and created successfully");
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