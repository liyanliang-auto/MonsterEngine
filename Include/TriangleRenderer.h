#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/RHI.h"

namespace MonsterRender {
    
    /**
     * Vertex structure for triangle rendering
     */
    struct Vertex {
        float position[3];
        float color[3];
    };
    
    /**
     * Simple triangle renderer for testing and demonstration
     */
    class TriangleRenderer {
    public:
        /**
         * Initialize the triangle renderer with given RHI device
         * @param device RHI device to use for rendering
         * @return true if initialization was successful
         */
        bool initialize(RHI::IRHIDevice* device);
        
        /**
         * Render the triangle using the given command list
         * @param cmdList Command list to record rendering commands
         */
        void render(RHI::IRHICommandList* cmdList);
        
        /**
         * Cleanup resources
         */
        ~TriangleRenderer() = default;
        
    private:
        /**
         * Create vertex buffer for triangle
         */
        bool createVertexBuffer();
        
        /**
         * Create and compile shaders
         */
        bool createShaders();
        
        /**
         * Create graphics pipeline state
         */
        bool createPipelineState();
        
    private:
        RHI::IRHIDevice* m_device = nullptr;
        std::shared_ptr<RHI::IRHIBuffer> m_vertexBuffer;
        std::shared_ptr<RHI::IRHIShader> m_vertexShader;
        std::shared_ptr<RHI::IRHIShader> m_pixelShader;
        std::shared_ptr<RHI::IRHIPipelineState> m_pipelineState;
    };
}
