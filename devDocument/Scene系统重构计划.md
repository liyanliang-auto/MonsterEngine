# Scene System Refactoring Plan

## Current Status

The MonsterEngine has two parallel Scene systems that need to be unified:

### 1. Engine Scene System (`Include/Engine/Scene.h`)
- **Location**: `Include/Engine/Scene.h`, `Source/Engine/Scene.cpp`
- **Namespace**: `MonsterEngine`
- **Key Classes**:
  - `FSceneInterface` - Abstract interface for scene operations
  - `FScene` - Inherits from `FSceneInterface`
  - `FPrimitiveSceneInfo` - Uses `UPrimitiveComponent`
  - `FPrimitiveSceneProxy` - Uses `UPrimitiveComponent` for construction
  - `FLightSceneInfo`, `FLightSceneProxy`
- **Features**:
  - Octree-based spatial acceleration (`FScenePrimitiveOctree`, `FSceneLightOctree`)
  - Component-based API (`AddPrimitive(UPrimitiveComponent*)`)
  - Full UE5-style architecture

### 2. Renderer Scene System (`Include/Renderer/Scene.h`)
- **Location**: `Include/Renderer/Scene.h`, `Source/Renderer/Scene.cpp`
- **Namespace**: `MonsterEngine::Renderer` (after refactoring)
- **Key Classes**:
  - `FScene` - Standalone class (no inheritance)
  - `FPrimitiveSceneInfo` - Uses `FPrimitiveSceneProxy` directly
  - `FPrimitiveSceneProxy` - Standalone, no component dependency
  - `FLightSceneInfo`, `FLightSceneProxy`
- **Features**:
  - Simpler, lower-level API (`AddPrimitive(FPrimitiveSceneProxy*)`)
  - Direct proxy-based rendering
  - Used by `CubeSceneRendererTest`

## Problem

Both systems define classes with the same names (`FScene`, `FPrimitiveSceneInfo`, etc.) in the same namespace, causing linker errors (LNK2005, LNK2019).

## Refactoring Progress

### Completed
1. Added `Renderer` namespace to all Renderer module files:
   - `Include/Renderer/Scene.h`
   - `Include/Renderer/SceneView.h`
   - `Include/Renderer/SceneRenderer.h`
   - `Include/Renderer/SceneVisibility.h`
   - `Include/Renderer/MeshDrawCommand.h`
   - `Include/Renderer/RenderQueue.h`
   - Corresponding `.cpp` files

2. Updated `CubeSceneRendererTest` to use `MonsterEngine::Renderer` namespace

### Remaining Issues
Complex type dependencies between namespaces:
- `FMeshBatch`, `FPrimitiveViewRelevance` defined in `MonsterEngine` namespace
- `FConvexVolume`, `FBoxSphereBounds` defined in `MonsterEngine` namespace
- These types are used by Renderer namespace classes

## Recommended Solution

### Option A: Complete Namespace Separation (Recommended)
Move all Renderer-specific types into `MonsterEngine::Renderer` namespace:
1. Move `FMeshBatch` to `Renderer` namespace
2. Move `FPrimitiveViewRelevance` to `Renderer` namespace
3. Update all references in Renderer module files
4. Keep Engine types in `MonsterEngine` namespace

### Option B: Merge Systems
Merge Renderer Scene into Engine Scene:
1. Use Engine's `FScene` as the single implementation
2. Modify `CubeSceneRendererTest` to use `UPrimitiveComponent`
3. Remove duplicate Renderer Scene classes

### Option C: Type Aliasing
Create type aliases in Renderer namespace:
```cpp
namespace Renderer {
    using FMeshBatch = MonsterEngine::FMeshBatch;
    using FPrimitiveViewRelevance = MonsterEngine::FPrimitiveViewRelevance;
}
```

## Current Workaround

Renderer scene files are temporarily excluded from compilation:
- `Source/Renderer/Scene.cpp`
- `Source/Renderer/SceneView.cpp`
- `Source/Renderer/SceneRenderer.cpp`
- `Source/Renderer/SceneVisibility.cpp`
- `Source/Renderer/MeshDrawCommand.cpp`
- `Source/Renderer/RenderQueue.cpp`
- `Source/Tests/CubeSceneRendererTest.cpp`

This allows the Engine Scene system and ImGui integration to work correctly.

## Next Steps

1. **Phase 1**: Complete ImGui renderer debugging and testing
2. **Phase 2**: Implement Option A (Complete Namespace Separation)
   - Move shared types to Renderer namespace
   - Update all Renderer module files
   - Re-enable Renderer scene files
3. **Phase 3**: Re-enable and test `CubeSceneRendererTest`
4. **Phase 4**: Consider merging systems in the future for cleaner architecture

## Files Affected

### Headers (Renderer Namespace Added)
- `Include/Renderer/Scene.h`
- `Include/Renderer/SceneView.h`
- `Include/Renderer/SceneRenderer.h`
- `Include/Renderer/SceneVisibility.h`
- `Include/Renderer/MeshDrawCommand.h`
- `Include/Renderer/RenderQueue.h`

### Source Files (Renderer Namespace Added)
- `Source/Renderer/Scene.cpp`
- `Source/Renderer/SceneView.cpp`
- `Source/Renderer/SceneRenderer.cpp`
- `Source/Renderer/SceneVisibility.cpp`
- `Source/Renderer/MeshDrawCommand.cpp`
- `Source/Renderer/RenderQueue.cpp`

### Test Files (Updated)
- `Include/Tests/CubeSceneRendererTest.h`
- `Source/Tests/CubeSceneRendererTest.cpp`

---
*Last Updated: 2024-12-11*
