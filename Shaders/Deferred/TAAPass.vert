#version 450 core

// ============================================================================
// TAA (Temporal Anti-Aliasing) Pass - Vertex Shader
// Fullscreen triangle for post-processing
// ============================================================================

layout(location = 0) out vec2 vScreenUV;

void main() {
    // Generate fullscreen triangle using vertex ID
    // Vertex 0: (-1, -1) -> UV (0, 0)
    // Vertex 1: ( 3, -1) -> UV (2, 0)
    // Vertex 2: (-1,  3) -> UV (0, 2)
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vScreenUV = uv;
    
    vec2 pos = uv * 2.0 - 1.0;
    gl_Position = vec4(pos, 0.0, 1.0);
}
