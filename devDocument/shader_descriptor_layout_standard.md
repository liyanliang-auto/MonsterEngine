# Shader Descriptor Set Layout Standard

## Overview
This document defines the standard descriptor set layout for all shaders in MonsterEngine, using the linear flatten approach with `MAX_BINDINGS_PER_SET = 8`.

## Layout Strategy

### Set 0: Per-Frame Data (Updated once per frame)
- **Binding 0**: ViewUniformBuffer / TransformUBO (View, Projection, Camera)
- **Binding 1**: Reserved for future use
- **Binding 2-7**: Reserved

**Global Slots**: 0-7

### Set 1: Per-Pass Data (Updated per render pass)
- **Binding 0**: LightingUBO (Lights, ambient)
- **Binding 1**: ShadowUBO (Shadow matrices, parameters)
- **Binding 2**: ShadowMap (Depth texture)
- **Binding 3-7**: Reserved for additional pass-level resources

**Global Slots**: 8-15

### Set 2: Per-Material Data (Updated per material)
- **Binding 0**: MaterialUBO (Material properties)
- **Binding 1**: BaseColorMap / Texture1
- **Binding 2**: NormalMap / Texture2
- **Binding 3**: MetallicRoughnessMap / DiffuseTexture
- **Binding 4**: OcclusionMap
- **Binding 5**: EmissiveMap
- **Binding 6-7**: Additional material textures

**Global Slots**: 16-23

### Set 3: Per-Object Data (Updated per draw call)
- **Binding 0**: ObjectUBO (Model matrix, object-specific data)
- **Binding 1-7**: Reserved for object-specific resources

**Global Slots**: 24-31

## Slot Mapping Table

| Resource Type | Set | Binding | Global Slot | Update Frequency |
|---------------|-----|---------|-------------|------------------|
| ViewUBO       | 0   | 0       | 0           | Per-Frame        |
| LightingUBO   | 1   | 0       | 8           | Per-Pass         |
| ShadowUBO     | 1   | 1       | 9           | Per-Pass         |
| ShadowMap     | 1   | 2       | 10          | Per-Pass         |
| MaterialUBO   | 2   | 0       | 16          | Per-Material     |
| BaseColorMap  | 2   | 1       | 17          | Per-Material     |
| NormalMap     | 2   | 2       | 18          | Per-Material     |
| MetallicMap   | 2   | 3       | 19          | Per-Material     |
| OcclusionMap  | 2   | 4       | 20          | Per-Material     |
| EmissiveMap   | 2   | 5       | 21          | Per-Material     |
| ObjectUBO     | 3   | 0       | 24          | Per-Object       |

## Migration Status

### ✅ Completed
- [x] CubeLitShadow.vert
- [x] CubeLitShadow.frag

### 🔄 Pending
- [ ] PBR.vert
- [ ] PBR.frag
- [ ] ForwardLit.vert
- [ ] ForwardLit.frag
- [ ] BasicLit.vert
- [ ] BasicLit.frag
- [ ] DefaultMaterial.vert
- [ ] DefaultMaterial.frag
- [ ] ShadowDepth.vert
- [ ] ShadowDepth.frag
- [ ] ShadowProjection.vert
- [ ] ShadowProjection.frag
- [ ] Other shaders (ImGui, Skybox, etc.)

## Notes

1. **CubeLitShadow Exception**: Currently uses Set 0 for TransformUBO (combines View + Model), which differs from the standard. Consider refactoring to separate View (Set 0) and Model (Set 3) in future updates.

2. **Backward Compatibility**: OpenGL shaders (*_GL.vert/frag) do not need descriptor set syntax and should remain unchanged.

3. **Shader Preprocessor**: The `FShaderPreprocessor` automatically converts Vulkan `layout(set=X, binding=Y)` to OpenGL `layout(binding=globalBinding)` where `globalBinding = set * 8 + binding`.

4. **Application Layer**: When binding resources, use the global slot value calculated from the formula above.
