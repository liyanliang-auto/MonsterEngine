#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/RHI.h"
#include "RHI/IRHISwapChain.h"
#include <chrono>

namespace MonsterRender {
    
    /**
     * Vertex structure for cube rendering with position and texture coordinates
     * Following UE5 style and LearnOpenGL tutorial
     */
    struct CubeVertex {
        float position[3];      // Position (x, y, z)
        float texCoord[2];      // Texture coordinates (u, v)
    };
    
    /**
     * Uniform buffer structure for MVP matrices
     * Aligned to 16 bytes following Vulkan std140 layout requirements
     */
    struct alignas(16) CubeUniformBufferObject {
        float model[16];        // Model matrix (4x4)
        float view[16];         // View matrix (4x4)
        float projection[16];   // Projection matrix (4x4)
    };
    
    /**
     * Textured Cube Renderer with rotation animation
     * 
     * Demonstrates:
     * - 3D cube rendering with texture mapping
     * - MVP (Model-View-Projection) transformations
     * - Multiple texture sampling
     * - Depth testing
     * - Rotation animation
     * 
     * Supports both Vulkan and OpenGL RHI backends
     * 
     * Reference: LearnOpenGL Coordinate Systems tutorial
     * https://learnopengl-cn.github.io/01%20Getting%20started/08%20Coordinate%20Systems/
     */
    class CubeRenderer {
    public:
        CubeRenderer();
        ~CubeRenderer() = default;
        
        /**
         * Initialize the cube renderer with given RHI device
         * Automatically detects RHI backend and loads appropriate shaders
         * @param device RHI device to use for rendering
         * @return true if initialization was successful
         */
        bool initialize(RHI::IRHIDevice* device);
        
        /**
         * Render the textured rotating cube
         * @param cmdList Command list to record rendering commands
         * @param deltaTime Time elapsed since last frame (for animation)
         */
        void render(RHI::IRHICommandList* cmdList, float32 deltaTime);
        
        /**
         * Update rotation animation
         * @param deltaTime Time elapsed since last frame
         */
        void update(float32 deltaTime);
        
        /**
         * Set window dimensions (for projection matrix)
         * @param width Window width
         * @param height Window height
         */
        void setWindowDimensions(uint32 width, uint32 height);
        
    private:
        /**
         * Create vertex buffer for cube (36 vertices for 6 faces)
         */
        bool createVertexBuffer();
        
        /**
         * Create uniform buffer for MVP matrices
         */
        bool createUniformBuffer();
        
        /**
         * Load textures from file
         */
        bool loadTextures();
        
        /**
         * Create and compile shaders
         * Loads appropriate shaders based on RHI backend
         */
        bool createShaders();
        
        /**
         * Create shaders for Vulkan (SPIR-V)
         */
        bool createVulkanShaders();
        
        /**
         * Create shaders for OpenGL (GLSL)
         */
        bool createOpenGLShaders();
        
        /**
         * Create graphics pipeline state
         */
        bool createPipelineState();
        
        /**
         * Update uniform buffer with current MVP matrices
         */
        void updateUniformBuffer();
        
        /**
         * Build Model matrix (rotation animation)
         */
        void buildModelMatrix(float* outMatrix);
        
        /**
         * Build View matrix (camera position)
         */
        void buildViewMatrix(float* outMatrix);
        
        /**
         * Build Projection matrix (perspective)
         */
        void buildProjectionMatrix(float* outMatrix);
        
        /**
         * Matrix multiplication helper (4x4 matrices)
         */
        static void matrixMultiply(const float* a, const float* b, float* result);
        
        /**
         * Create identity matrix
         */
        static void matrixIdentity(float* matrix);
        
        /**
         * Create rotation matrix around arbitrary axis
         */
        static void matrixRotate(float* matrix, float angle, float x, float y, float z);
        
        /**
         * Create translation matrix
         */
        static void matrixTranslate(float* matrix, float x, float y, float z);
        
        /**
         * Create perspective projection matrix
         * @param flipY If true, flip Y axis for Vulkan coordinate system
         */
        static void matrixPerspective(float* matrix, float fovRadians, float aspect, float nearPlane, float farPlane, bool flipY = true);
        
    private:
        RHI::IRHIDevice* m_device = nullptr;
        RHI::ERHIBackend m_rhiBackend = RHI::ERHIBackend::Unknown;
        
        // Rendering resources
        TSharedPtr<RHI::IRHIBuffer> m_vertexBuffer;
        TSharedPtr<RHI::IRHIBuffer> m_uniformBuffer;
        TSharedPtr<RHI::IRHITexture> m_texture1;        // container.jpg
        TSharedPtr<RHI::IRHITexture> m_texture2;        // awesomeface.png
        TSharedPtr<RHI::IRHISampler> m_sampler;         // Default texture sampler
        TSharedPtr<RHI::IRHIVertexShader> m_vertexShader;
        TSharedPtr<RHI::IRHIPixelShader> m_pixelShader;
        TSharedPtr<RHI::IRHIPipelineState> m_pipelineState;
        
        // Animation state
        float32 m_totalTime = 0.0f;                     // Total elapsed time
        float32 m_rotationAngle = 0.0f;                 // Current rotation angle
        
        // Window dimensions
        uint32 m_windowWidth = 1280;
        uint32 m_windowHeight = 720;
        
        // Timing
        std::chrono::high_resolution_clock::time_point m_startTime;
    };
}
