#pragma once

/**
 * @file Material.h
 * @brief Material class definition
 * 
 * FMaterial represents a complete material definition including
 * shader references, default parameter values, and material properties.
 * 
 * Reference UE5: Engine/Source/Runtime/Engine/Classes/Materials/Material.h
 */

#include "Core/CoreTypes.h"
#include "Core/CoreMinimal.h"
#include "Core/Templates/SharedPointer.h"
#include "Engine/Material/MaterialInterface.h"
#include "Engine/Material/MaterialTypes.h"
#include "Engine/Shader/Shader.h"
#include "Containers/Array.h"
#include "Containers/Map.h"

// Forward declare RHI types
namespace MonsterRender { namespace RHI {
    class IRHIDevice;
    class IRHIPipelineState;
}}

namespace MonsterEngine
{

// Forward declarations
class FMaterialRenderProxy;
class FShader;
class FVertexShader;
class FPixelShader;
class FTexture;
class FShaderManager;

// ============================================================================
// Material
// ============================================================================

/**
 * @class FMaterial
 * @brief Complete material definition
 * 
 * A material defines:
 * - Shader programs (vertex, pixel, etc.)
 * - Default parameter values
 * - Material properties (blend mode, shading model, etc.)
 * - Texture slots
 * 
 * Materials are the "template" from which material instances are created.
 * 
 * Reference UE5: UMaterial
 */
class FMaterial : public FMaterialInterface
{
public:
    FMaterial();
    explicit FMaterial(const FName& InName);
    virtual ~FMaterial();
    
    // ========================================================================
    // FMaterialInterface Implementation
    // ========================================================================
    
    virtual FMaterial* GetMaterial() override { return this; }
    virtual const FMaterial* GetMaterial() const override { return this; }
    
    virtual FMaterialRenderProxy* GetRenderProxy() override;
    virtual const FMaterialRenderProxy* GetRenderProxy() const override;
    
    virtual const FMaterialProperties& GetMaterialProperties() const override { return m_properties; }
    
    virtual bool GetScalarParameterValue(const FMaterialParameterInfo& ParameterInfo, float& OutValue) const override;
    virtual bool GetVectorParameterValue(const FMaterialParameterInfo& ParameterInfo, FLinearColor& OutValue) const override;
    virtual bool GetTextureParameterValue(const FMaterialParameterInfo& ParameterInfo, FTexture*& OutValue) const override;
    
    virtual void GetUsedTextures(TArray<FTexture*>& OutTextures) const override;
    
    // ========================================================================
    // Material Properties
    // ========================================================================
    
    /**
     * Set material properties
     */
    void SetMaterialProperties(const FMaterialProperties& Properties);
    
    /**
     * Set blend mode
     */
    void SetBlendMode(EMaterialBlendMode Mode);
    
    /**
     * Set shading model
     */
    void SetShadingModel(EMaterialShadingModel Model);
    
    /**
     * Set two-sided rendering
     */
    void SetTwoSided(bool bTwoSided);
    
    /**
     * Set opacity mask clip value
     */
    void SetOpacityMaskClipValue(float Value);
    
    // ========================================================================
    // Default Parameter Values
    // ========================================================================
    
    /**
     * Set a default scalar parameter value
     * @param ParameterName Parameter name
     * @param Value Default value
     */
    void SetDefaultScalarParameter(const FName& ParameterName, float Value);
    
    /**
     * Set a default vector parameter value
     * @param ParameterName Parameter name
     * @param Value Default value
     */
    void SetDefaultVectorParameter(const FName& ParameterName, const FLinearColor& Value);
    
    /**
     * Set a default texture parameter value
     * @param ParameterName Parameter name
     * @param Value Default texture
     */
    void SetDefaultTextureParameter(const FName& ParameterName, FTexture* Value);
    
    /**
     * Get all scalar parameter defaults
     */
    const TArray<FScalarParameterValue>& GetScalarParameterDefaults() const { return m_scalarParameters; }
    
    /**
     * Get all vector parameter defaults
     */
    const TArray<FVectorParameterValue>& GetVectorParameterDefaults() const { return m_vectorParameters; }
    
    /**
     * Get all texture parameter defaults
     */
    const TArray<FTextureParameterValue>& GetTextureParameterDefaults() const { return m_textureParameters; }
    
    // ========================================================================
    // Shader Management
    // ========================================================================
    
    /**
     * Set the vertex shader
     */
    void SetVertexShader(TSharedPtr<FShader> Shader);
    
    /**
     * Set the pixel shader
     */
    void SetPixelShader(TSharedPtr<FShader> Shader);
    
    /**
     * Get the vertex shader
     */
    TSharedPtr<FShader> GetVertexShader() const { return m_vertexShader; }
    
    /**
     * Get the pixel shader
     */
    TSharedPtr<FShader> GetPixelShader() const { return m_pixelShader; }
    
    /**
     * Check if shaders are valid
     */
    bool HasValidShaders() const;
    
    /**
     * Load shaders from source files
     * @param VertexShaderPath Path to vertex shader source
     * @param PixelShaderPath Path to pixel shader source
     * @return true if both shaders loaded successfully
     */
    bool LoadShadersFromFiles(const String& VertexShaderPath, const String& PixelShaderPath);
    
    /**
     * Get shader source paths
     */
    const String& GetVertexShaderPath() const { return m_vertexShaderPath; }
    const String& GetPixelShaderPath() const { return m_pixelShaderPath; }
    
    // ========================================================================
    // Pipeline State
    // ========================================================================
    
    /**
     * Get or create the pipeline state for this material
     * @param Device RHI device
     * @return Pipeline state, or nullptr on failure
     */
    TSharedPtr<MonsterRender::RHI::IRHIPipelineState> GetOrCreatePipelineState(
        MonsterRender::RHI::IRHIDevice* Device);
    
    /**
     * Invalidate cached pipeline state (call when material changes)
     */
    void InvalidatePipelineState();
    
    // ========================================================================
    // Compilation
    // ========================================================================
    
    /**
     * Compile the material shaders
     * @param Device RHI device for shader compilation
     * @return true if compilation succeeded
     */
    bool Compile(MonsterRender::RHI::IRHIDevice* Device);
    
    /**
     * Check if material is compiled and ready for rendering
     */
    bool IsCompiled() const { return m_isCompiled; }
    
    // ========================================================================
    // Dirty State
    // ========================================================================
    
    /**
     * Mark material as dirty (needs recompilation)
     */
    void MarkDirty();
    
    /**
     * Check if material is dirty
     */
    bool IsDirty() const { return m_isDirty; }
    
protected:
    /**
     * Create the render proxy
     */
    void CreateRenderProxy();
    
    /**
     * Find parameter index by name
     */
    int32 FindScalarParameterIndex(const FName& Name) const;
    int32 FindVectorParameterIndex(const FName& Name) const;
    int32 FindTextureParameterIndex(const FName& Name) const;
    
protected:
    /** Material properties */
    FMaterialProperties m_properties;
    
    /** Default scalar parameters */
    TArray<FScalarParameterValue> m_scalarParameters;
    
    /** Default vector parameters */
    TArray<FVectorParameterValue> m_vectorParameters;
    
    /** Default texture parameters */
    TArray<FTextureParameterValue> m_textureParameters;
    
    /** Vertex shader */
    TSharedPtr<FShader> m_vertexShader;
    
    /** Pixel shader */
    TSharedPtr<FShader> m_pixelShader;
    
    /** Vertex shader source path */
    String m_vertexShaderPath;
    
    /** Pixel shader source path */
    String m_pixelShaderPath;
    
    /** Cached pipeline state */
    TSharedPtr<MonsterRender::RHI::IRHIPipelineState> m_pipelineState;
    
    /** Render proxy for this material */
    TSharedPtr<FMaterialRenderProxy> m_renderProxy;
    
    /** Whether material is compiled */
    bool m_isCompiled = false;
    
    /** Whether material needs recompilation */
    bool m_isDirty = true;
};

// ============================================================================
// Material Ref
// ============================================================================

/** Shared pointer type for materials */
using FMaterialRef = TSharedPtr<FMaterial>;
using FMaterialWeakRef = TWeakPtr<FMaterial>;

} // namespace MonsterEngine
