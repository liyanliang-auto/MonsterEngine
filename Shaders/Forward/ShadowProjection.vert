// Copyright Monster Engine. All Rights Reserved.
// Shadow Projection Vertex Shader
// 
// Projects shadow depth maps onto the scene during lighting pass.
// Renders a full-screen quad to sample shadow maps.
//
// Reference: UE5 ShadowProjectionVertexShader.usf

#version 450

// ============================================================================
// Vertex Output
// ============================================================================

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec4 outScreenPosition;

// ============================================================================
// Main - Full Screen Triangle
// ============================================================================

void main()
{
    // Generate full-screen triangle from vertex ID
    // This technique uses 3 vertices to cover the entire screen
    // without needing a vertex buffer
    
    // Vertex positions for full-screen triangle:
    // ID 0: (-1, -1) -> (0, 0)
    // ID 1: ( 3, -1) -> (2, 0)
    // ID 2: (-1,  3) -> (0, 2)
    
    vec2 position;
    position.x = (gl_VertexIndex == 1) ? 3.0 : -1.0;
    position.y = (gl_VertexIndex == 2) ? 3.0 : -1.0;
    
    gl_Position = vec4(position, 0.0, 1.0);
    
    // Calculate texture coordinates (0-1 range, with Y flipped for Vulkan)
    outTexCoord = position * 0.5 + 0.5;
    
    // Pass screen position for depth reconstruction
    outScreenPosition = vec4(position, 0.0, 1.0);
}
