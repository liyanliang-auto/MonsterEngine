#version 450 core

// ============================================================================
// Deferred Rendering - Lighting Pass Vertex Shader
// 全屏三角形顶点着色器（不需要 VBO，只需 draw(3, 0)）
//
// 原理：用 gl_VertexIndex 直接生成 3 个覆盖整个屏幕的顶点
//   V0 -> NDC (-1, -1)   V1 -> NDC ( 3, -1)   V2 -> NDC (-1,  3)
// 三角形超出 [-1, 1] 的部分会被硬件裁剪，等效于覆盖全屏四边形，
// 但只有 1 个三角形，省去了对角线像素的重复光栅化。
//
// 参考：UE5 PostProcess 全屏 Pass 通用实现
// ============================================================================

// 输出屏幕 UV（范围 [0, 1]，对应裁剪后的屏幕区域）
layout(location = 0) out vec2 vScreenUV;

void main() {
    // 使用位运算生成三角形顶点的 UV（范围 [0, 2]）
    //   VertexIndex = 0 -> uv = (0, 0)
    //   VertexIndex = 1 -> uv = (2, 0)
    //   VertexIndex = 2 -> uv = (0, 2)
    vec2 uv = vec2(
        float((gl_VertexIndex << 1) & 2),
        float(gl_VertexIndex & 2)
    );

    // UV 从 [0, 2] 映射到 NDC [-1, 3]
    // 这样超出 [-1, 1] 的部分会被光栅化阶段自动裁剪
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);

    // 输出 [0, 1] 范围的 UV 给片段着色器
    // 注意：若最终画面上下颠倒，说明 viewport Y-flip 导致 UV.y 需要取反
    //       届时改为  vScreenUV = vec2(uv.x, 1.0 - uv.y);
    vScreenUV = uv;
}
