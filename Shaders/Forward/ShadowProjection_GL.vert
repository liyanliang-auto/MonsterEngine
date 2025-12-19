// Copyright Monster Engine. All Rights Reserved.
// Shadow Projection Vertex Shader - OpenGL Version
// 
// Projects shadow depth maps onto the scene during lighting pass.
// Renders a full-screen quad to sample shadow maps.
//
// Reference: UE5 ShadowProjectionVertexShader.usf

#version 330 core

// ============================================================================
// Vertex Output
// ============================================================================

out vec2 TexCoord;
out vec4 ScreenPosition;

// ============================================================================
// Main - Full Screen Triangle
// ============================================================================

void main()
{
    // Generate full-screen triangle from vertex ID
    // This technique uses 3 vertices to cover the entire screen
    // without needing a vertex buffer
    
    vec2 position;
    position.x = (gl_VertexID == 1) ? 3.0 : -1.0;
    position.y = (gl_VertexID == 2) ? 3.0 : -1.0;
    
    gl_Position = vec4(position, 0.0, 1.0);
    
    // Calculate texture coordinates (0-1 range)
    // Note: OpenGL has Y=0 at bottom, so no flip needed
    TexCoord = position * 0.5 + 0.5;
    
    // Pass screen position for depth reconstruction
    ScreenPosition = vec4(position, 0.0, 1.0);
}
