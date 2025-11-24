#include "CubeRenderer.h"
#include "Core/Log.h"
#include "Core/ShaderCompiler.h"
#include "Platform/Vulkan/VulkanPipelineState.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Renderer/FTextureLoader.h"
#include <cmath>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace MonsterRender {

    CubeRenderer::CubeRenderer() 
        : m_startTime(std::chrono::high_resolution_clock::now()) {
    }
    
    bool CubeRenderer::initialize(RHI::IRHIDevice* device) {
        MR_LOG_INFO("Initializing Cube Renderer...");
        m_device = device;
        
        // Initialize in correct order
        if (!createVertexBuffer()) {
            MR_LOG_ERROR("Failed to create vertex buffer");
            return false;
        }
        
        if (!createUniformBuffer()) {
            MR_LOG_ERROR("Failed to create uniform buffer");
            return false;
        }
        
        if (!loadTextures()) {
            MR_LOG_ERROR("Failed to load textures");
            return false;
        }
        
        if (!createShaders()) {
            MR_LOG_ERROR("Failed to create shaders");
            return false;
        }
        
        if (!createPipelineState()) {
            MR_LOG_ERROR("Failed to create pipeline state");
            return false;
        }
        
        MR_LOG_INFO("Cube Renderer initialized successfully");
        return true;
    }
    
    void CubeRenderer::render(RHI::IRHICommandList* cmdList, float32 deltaTime) {
        if (!cmdList || !m_pipelineState) {
            MR_LOG_ERROR("Cannot render: invalid command list or pipeline state");
            return;
        }
        
        // Update animation
        update(deltaTime);
        
        // Update uniform buffer with current MVP matrices
        updateUniformBuffer();
        
        // Set pipeline state
        cmdList->setPipelineState(m_pipelineState);
        
        // Bind vertex buffer
        TArray<TSharedPtr<RHI::IRHIBuffer>> vertexBuffers = {m_vertexBuffer};
        cmdList->setVertexBuffers(0, TSpan<TSharedPtr<RHI::IRHIBuffer>>(vertexBuffers));
        
        // Set viewport
        RHI::Viewport viewport(static_cast<float32>(m_windowWidth), static_cast<float32>(m_windowHeight));
        cmdList->setViewport(viewport);
        
        // Set scissor rect
        RHI::ScissorRect scissor;
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = m_windowWidth;
        scissor.bottom = m_windowHeight;
        cmdList->setScissorRect(scissor);
        
        // Draw cube (36 vertices = 6 faces * 2 triangles * 3 vertices)
        cmdList->draw(36, 0);
        
        MR_LOG_DEBUG("Cube rendered at angle: " + std::to_string(m_rotationAngle));
    }
    
    void CubeRenderer::update(float32 deltaTime) {
        m_totalTime += deltaTime;
        
        // Get actual time since start for smooth animation
        auto currentTime = std::chrono::high_resolution_clock::now();
        float32 time = std::chrono::duration<float32, std::chrono::seconds::period>(
            currentTime - m_startTime).count();
        
        m_rotationAngle = time;  // Rotate based on time
    }
    
    void CubeRenderer::setWindowDimensions(uint32 width, uint32 height) {
        m_windowWidth = width;
        m_windowHeight = height;
        MR_LOG_INFO("Window dimensions updated: " + std::to_string(width) + "x" + std::to_string(height));
    }
    
    bool CubeRenderer::createVertexBuffer() {
        MR_LOG_INFO("Creating cube vertex buffer...");
        
        // Cube vertices with position (x, y, z) and texture coordinates (u, v)
        // Following LearnOpenGL tutorial cube layout
        TArray<CubeVertex> vertices = {
            // Back face (z = -0.5)
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}},
            {{ 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f}},
            {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}},
            {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}},
            {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f}},
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}},

            // Front face (z = 0.5)
            {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}},
            {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}},
            {{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f}},
            {{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f}},
            {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f}},
            {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}},

            // Left face (x = -0.5)
            {{-0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}},
            {{-0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}},
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}},
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}},
            {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}},
            {{-0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}},

            // Right face (x = 0.5)
            {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}},
            {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}},
            {{ 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}},
            {{ 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}},
            {{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}},
            {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}},

            // Bottom face (y = -0.5)
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}},
            {{ 0.5f, -0.5f, -0.5f}, {1.0f, 1.0f}},
            {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}},
            {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}},
            {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}},
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}},

            // Top face (y = 0.5)
            {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f}},
            {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}},
            {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}},
            {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}},
            {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f}},
            {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f}}
        };
        
        // Create vertex buffer descriptor
        RHI::BufferDesc vertexBufferDesc;
        vertexBufferDesc.size = static_cast<uint32>(sizeof(CubeVertex) * vertices.size());
        vertexBufferDesc.usage = RHI::EResourceUsage::VertexBuffer;
        vertexBufferDesc.cpuAccessible = true;
        vertexBufferDesc.debugName = "Cube Vertex Buffer";
        
        m_vertexBuffer = m_device->createBuffer(vertexBufferDesc);
        if (!m_vertexBuffer) {
            MR_LOG_ERROR("Failed to create vertex buffer");
            return false;
        }
        
        // Upload vertex data to GPU
        void* mappedData = m_vertexBuffer->map();
        if (!mappedData) {
            MR_LOG_ERROR("Failed to map vertex buffer");
            return false;
        }
        
        std::memcpy(mappedData, vertices.data(), vertexBufferDesc.size);
        m_vertexBuffer->unmap();
        
        MR_LOG_INFO("Cube vertex buffer created successfully (36 vertices)");
        return true;
    }
    
    bool CubeRenderer::createUniformBuffer() {
        MR_LOG_INFO("Creating uniform buffer for MVP matrices...");
        
        // Create uniform buffer descriptor
        RHI::BufferDesc uniformBufferDesc;
        uniformBufferDesc.size = sizeof(CubeUniformBufferObject);
        uniformBufferDesc.usage = RHI::EResourceUsage::UniformBuffer;
        uniformBufferDesc.cpuAccessible = true;  // Need to update every frame
        uniformBufferDesc.debugName = "Cube Uniform Buffer (MVP)";
        
        m_uniformBuffer = m_device->createBuffer(uniformBufferDesc);
        if (!m_uniformBuffer) {
            MR_LOG_ERROR("Failed to create uniform buffer");
            return false;
        }
        
        MR_LOG_INFO("Uniform buffer created successfully");
        return true;
    }
    
    bool CubeRenderer::loadTextures() {
        MR_LOG_INFO("Loading textures from disk...");
        
        // Texture paths (LearnOpenGL tutorial style)
        String texture1Path = "resources/textures/container.jpg";
        String texture2Path = "resources/textures/awesomeface.png";
        
        // Load texture 1 (container.jpg)
        MR_LOG_INFO("Loading container texture: " + texture1Path);
        FTextureLoadInfo loadInfo1;
        loadInfo1.FilePath = texture1Path;
        loadInfo1.bGenerateMips = true;
        loadInfo1.bSRGB = true;
        loadInfo1.bFlipVertical = true;
        loadInfo1.DesiredChannels = 4;  // Force RGBA
        
        m_texture1 = FTextureLoader::LoadFromFile(m_device, loadInfo1);
        if (!m_texture1) {
            MR_LOG_ERROR("Failed to load container texture from: " + texture1Path);
            MR_LOG_WARNING("Creating placeholder texture 1");
            
            // Fallback to placeholder
            RHI::TextureDesc desc;
            desc.width = 512;
            desc.height = 512;
            desc.depth = 1;
            desc.mipLevels = 1;
            desc.arraySize = 1;
            desc.format = RHI::EPixelFormat::R8G8B8A8_SRGB;
            desc.usage = RHI::EResourceUsage::ShaderResource;
            desc.debugName = "Container Texture (Placeholder)";
            
            m_texture1 = m_device->createTexture(desc);
            if (!m_texture1) {
                MR_LOG_ERROR("Failed to create placeholder texture 1");
                return false;
            }
        }
        
        // Load texture 2 (awesomeface.png)
        MR_LOG_INFO("Loading awesomeface texture: " + texture2Path);
        FTextureLoadInfo loadInfo2;
        loadInfo2.FilePath = texture2Path;
        loadInfo2.bGenerateMips = true;
        loadInfo2.bSRGB = true;
        loadInfo2.bFlipVertical = true;
        loadInfo2.DesiredChannels = 4;  // Force RGBA
        
        m_texture2 = FTextureLoader::LoadFromFile(m_device, loadInfo2);
        if (!m_texture2) {
            MR_LOG_ERROR("Failed to load awesomeface texture from: " + texture2Path);
            MR_LOG_WARNING("Creating placeholder texture 2");
            
            // Fallback to placeholder
            RHI::TextureDesc desc;
            desc.width = 512;
            desc.height = 512;
            desc.depth = 1;
            desc.mipLevels = 1;
            desc.arraySize = 1;
            desc.format = RHI::EPixelFormat::R8G8B8A8_SRGB;
            desc.usage = RHI::EResourceUsage::ShaderResource;
            desc.debugName = "Awesomeface Texture (Placeholder)";
            
            m_texture2 = m_device->createTexture(desc);
            if (!m_texture2) {
                MR_LOG_ERROR("Failed to create placeholder texture 2");
                return false;
            }
        }
        
        MR_LOG_INFO("Textures loaded successfully");
        return true;
    }
    
    bool CubeRenderer::createShaders() {
        MR_LOG_INFO("Loading cube shaders...");
        
        // Load pre-compiled SPV files
        String vsPath = "Shaders/Cube.vert.spv";
        String psPath = "Shaders/Cube.frag.spv";

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
        if (!m_vertexShader) {
            MR_LOG_ERROR("Failed to create vertex shader");
            return false;
        }
        
        m_pixelShader = m_device->createPixelShader(TSpan<const uint8>(psSpv.data(), psSpv.size()));
        if (!m_pixelShader) {
            MR_LOG_ERROR("Failed to create pixel shader");
            return false;
        }

        MR_LOG_INFO("Cube shaders loaded successfully");
        return true;
    }
    
    bool CubeRenderer::createPipelineState() {
        MR_LOG_INFO("Creating cube pipeline state...");
        
        if (!m_vertexShader || !m_pixelShader) {
            MR_LOG_ERROR("Cannot create pipeline state without valid shaders");
            return false;
        }
        
        // Get swapchain format from device
        auto* vulkanDevice = dynamic_cast<MonsterRender::RHI::Vulkan::VulkanDevice*>(m_device);
        if (!vulkanDevice) {
            MR_LOG_ERROR("Device is not a VulkanDevice!");
            return false;
        }
        
        VkFormat swapchainFormat = vulkanDevice->getSwapchainFormat();
        MR_LOG_INFO("Using swapchain format: " + std::to_string(swapchainFormat));
        
        // Convert VkFormat to EPixelFormat
        RHI::EPixelFormat renderTargetFormat = RHI::EPixelFormat::R8G8B8A8_SRGB;
        if (swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB) {
            renderTargetFormat = RHI::EPixelFormat::B8G8R8A8_SRGB;
        } else if (swapchainFormat == VK_FORMAT_B8G8R8A8_UNORM) {
            renderTargetFormat = RHI::EPixelFormat::B8G8R8A8_UNORM;
        } else if (swapchainFormat == VK_FORMAT_R8G8B8A8_SRGB) {
            renderTargetFormat = RHI::EPixelFormat::R8G8B8A8_SRGB;
        } else if (swapchainFormat == VK_FORMAT_R8G8B8A8_UNORM) {
            renderTargetFormat = RHI::EPixelFormat::R8G8B8A8_UNORM;
        }
        
        // Create pipeline state descriptor
        RHI::PipelineStateDesc pipelineDesc;
        pipelineDesc.vertexShader = m_vertexShader;
        pipelineDesc.pixelShader = m_pixelShader;
        pipelineDesc.primitiveTopology = RHI::EPrimitiveTopology::TriangleList;
        
        // Rasterizer state
        pipelineDesc.rasterizerState.fillMode = RHI::EFillMode::Solid;
        pipelineDesc.rasterizerState.cullMode = RHI::ECullMode::Back;  // Enable back-face culling
        pipelineDesc.rasterizerState.frontCounterClockwise = false;
        
        // Depth stencil state - ENABLE depth testing for 3D cube
        pipelineDesc.depthStencilState.depthEnable = true;
        pipelineDesc.depthStencilState.depthWriteEnable = true;
        pipelineDesc.depthStencilState.depthCompareOp = RHI::ECompareOp::Less;
        
        // Blend state - disable blending for opaque cube
        pipelineDesc.blendState.blendEnable = false;
        
        // Render target format
        pipelineDesc.renderTargetFormats.push_back(renderTargetFormat);
        
        // Depth format
        pipelineDesc.depthStencilFormat = RHI::EPixelFormat::D32_FLOAT;
        
        pipelineDesc.debugName = "Cube Pipeline State";
        
        MR_LOG_INFO("Creating pipeline state...");
        m_pipelineState = m_device->createPipelineState(pipelineDesc);
        if (!m_pipelineState) {
            MR_LOG_ERROR("Failed to create pipeline state");
            return false;
        }
        
        MR_LOG_INFO("Cube pipeline state created successfully");
        return true;
    }
    
    void CubeRenderer::updateUniformBuffer() {
        CubeUniformBufferObject ubo{};
        
        // Build MVP matrices
        buildModelMatrix(ubo.model);
        buildViewMatrix(ubo.view);
        buildProjectionMatrix(ubo.projection);
        
        // Upload to GPU
        void* mappedData = m_uniformBuffer->map();
        if (mappedData) {
            std::memcpy(mappedData, &ubo, sizeof(CubeUniformBufferObject));
            m_uniformBuffer->unmap();
        }
    }
    
    void CubeRenderer::buildModelMatrix(float* outMatrix) {
        // Create rotation matrix
        // Rotate around axis (0.5, 1.0, 0.0) like in LearnOpenGL tutorial
        matrixIdentity(outMatrix);
        matrixRotate(outMatrix, m_rotationAngle, 0.5f, 1.0f, 0.0f);
    }
    
    void CubeRenderer::buildViewMatrix(float* outMatrix) {
        // Camera at position (0, 0, -3) looking at origin
        // Simple translation matrix
        matrixIdentity(outMatrix);
        matrixTranslate(outMatrix, 0.0f, 0.0f, -3.0f);
    }
    
    void CubeRenderer::buildProjectionMatrix(float* outMatrix) {
        // Perspective projection
        float32 fov = 45.0f * static_cast<float32>(M_PI) / 180.0f;  // 45 degrees in radians
        float32 aspect = static_cast<float32>(m_windowWidth) / static_cast<float32>(m_windowHeight);
        float32 nearPlane = 0.1f;
        float32 farPlane = 100.0f;
        
        matrixPerspective(outMatrix, fov, aspect, nearPlane, farPlane);
    }
    
    // ============================================================================
    // Matrix Math Utilities (Simple implementation without GLM dependency)
    // ============================================================================
    
    void CubeRenderer::matrixIdentity(float* matrix) {
        std::memset(matrix, 0, 16 * sizeof(float));
        matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
    }
    
    void CubeRenderer::matrixMultiply(const float* a, const float* b, float* result) {
        float temp[16];
        
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                temp[i * 4 + j] = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    temp[i * 4 + j] += a[i * 4 + k] * b[k * 4 + j];
                }
            }
        }
        
        std::memcpy(result, temp, 16 * sizeof(float));
    }
    
    void CubeRenderer::matrixRotate(float* matrix, float angle, float x, float y, float z) {
        // Normalize axis
        float length = std::sqrt(x * x + y * y + z * z);
        if (length < 0.0001f) return;
        
        x /= length;
        y /= length;
        z /= length;
        
        float c = std::cos(angle);
        float s = std::sin(angle);
        float t = 1.0f - c;
        
        float rotation[16];
        matrixIdentity(rotation);
        
        rotation[0] = t * x * x + c;
        rotation[1] = t * x * y + s * z;
        rotation[2] = t * x * z - s * y;
        
        rotation[4] = t * x * y - s * z;
        rotation[5] = t * y * y + c;
        rotation[6] = t * y * z + s * x;
        
        rotation[8] = t * x * z + s * y;
        rotation[9] = t * y * z - s * x;
        rotation[10] = t * z * z + c;
        
        float temp[16];
        std::memcpy(temp, matrix, 16 * sizeof(float));
        matrixMultiply(temp, rotation, matrix);
    }
    
    void CubeRenderer::matrixTranslate(float* matrix, float x, float y, float z) {
        float translation[16];
        matrixIdentity(translation);
        
        translation[12] = x;
        translation[13] = y;
        translation[14] = z;
        
        float temp[16];
        std::memcpy(temp, matrix, 16 * sizeof(float));
        matrixMultiply(temp, translation, matrix);
    }
    
    void CubeRenderer::matrixPerspective(float* matrix, float fovRadians, float aspect, float nearPlane, float farPlane) {
        std::memset(matrix, 0, 16 * sizeof(float));
        
        float tanHalfFov = std::tan(fovRadians / 2.0f);
        
        matrix[0] = 1.0f / (aspect * tanHalfFov);
        matrix[5] = 1.0f / tanHalfFov;
        matrix[10] = -(farPlane + nearPlane) / (farPlane - nearPlane);
        matrix[11] = -1.0f;
        matrix[14] = -(2.0f * farPlane * nearPlane) / (farPlane - nearPlane);
    }
    
} // namespace MonsterRender
