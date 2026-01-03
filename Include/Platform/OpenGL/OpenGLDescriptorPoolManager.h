#pragma once

/**
 * OpenGLDescriptorPoolManager.h
 * 
 * OpenGL Descriptor/Uniform Buffer binding management
 * Provides a Vulkan-like interface for OpenGL uniform buffer management
 * Reference: UE5 OpenGLState.h - OpenGL doesn't have descriptor sets,
 * but we provide a compatible abstraction for cross-platform code
 */

#include "Core/CoreMinimal.h"
#include "RHI/IRHIDescriptorSet.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include <mutex>

// Forward declarations
namespace MonsterRender::RHI {
    class IRHIBuffer;
    class IRHITexture;
}

namespace MonsterEngine::OpenGL {

// Forward declarations
class FOpenGLDevice;
class FOpenGLBuffer;
class FOpenGLTexture;

/**
 * OpenGL descriptor set representation
 * In OpenGL, this tracks UBO bindings and texture unit assignments
 * Reference: UE5 doesn't have a direct equivalent, but we model after Vulkan for consistency
 */
class FOpenGLDescriptorSet : public MonsterRender::RHI::IRHIDescriptorSet {
public:
    FOpenGLDescriptorSet(FOpenGLDevice* device, uint32 setIndex);
    virtual ~FOpenGLDescriptorSet();
    
    // IRHIDescriptorSet interface
    virtual void updateBuffer(uint32 binding, TSharedPtr<MonsterRender::RHI::IRHIBuffer> buffer,
                             uint64 offset, uint64 range) override;
    
    virtual void updateTexture(uint32 binding, TSharedPtr<MonsterRender::RHI::IRHITexture> texture,
                              MonsterRender::RHI::IRHISampler* sampler) override;
    
    virtual void updateBuffers(MonsterRender::TSpan<const MonsterRender::RHI::FDescriptorBufferUpdate> updates) override;
    virtual void updateTextures(MonsterRender::TSpan<const MonsterRender::RHI::FDescriptorTextureUpdate> updates) override;
    
    // OpenGL-specific methods
    
    /**
     * Apply bindings to OpenGL state
     * This is called before draw calls to bind UBOs and textures
     */
    void applyBindings();
    
    /**
     * Get set index
     */
    uint32 getSetIndex() const { return m_setIndex; }
    
private:
    struct BufferBinding {
        TSharedPtr<MonsterRender::RHI::IRHIBuffer> buffer;
        uint64 offset = 0;
        uint64 range = 0;
    };
    
    struct TextureBinding {
        TSharedPtr<MonsterRender::RHI::IRHITexture> texture;
        MonsterRender::RHI::IRHISampler* sampler = nullptr;
    };
    
    FOpenGLDevice* m_device;
    uint32 m_setIndex;
    
    // Binding tracking
    TMap<uint32, BufferBinding> m_bufferBindings;
    TMap<uint32, TextureBinding> m_textureBindings;
    
    // Dirty tracking for optimization
    bool m_dirty = true;
};

/**
 * OpenGL descriptor set layout
 * Describes the structure of bindings in a descriptor set
 */
class FOpenGLDescriptorSetLayout : public MonsterRender::RHI::IRHIDescriptorSetLayout {
public:
    FOpenGLDescriptorSetLayout(FOpenGLDevice* device, 
                               const MonsterRender::RHI::FDescriptorSetLayoutDesc& desc);
    virtual ~FOpenGLDescriptorSetLayout();
    
    /**
     * Get set index
     */
    uint32 getSetIndex() const { return m_setIndex; }
    
    /**
     * Get bindings
     */
    const TArray<MonsterRender::RHI::FDescriptorSetLayoutBinding>& getBindings() const { 
        return m_bindings; 
    }
    
    /**
     * Check if valid
     */
    bool isValid() const { return m_valid; }
    
private:
    FOpenGLDevice* m_device;
    uint32 m_setIndex;
    TArray<MonsterRender::RHI::FDescriptorSetLayoutBinding> m_bindings;
    bool m_valid = false;
};

/**
 * OpenGL pipeline layout
 * Manages multiple descriptor set layouts
 */
class FOpenGLPipelineLayout : public MonsterRender::RHI::IRHIPipelineLayout {
public:
    FOpenGLPipelineLayout(FOpenGLDevice* device,
                         const MonsterRender::RHI::FPipelineLayoutDesc& desc);
    virtual ~FOpenGLPipelineLayout();
    
    /**
     * Get descriptor set layouts
     */
    const TArray<TSharedPtr<MonsterRender::RHI::IRHIDescriptorSetLayout>>& getSetLayouts() const {
        return m_setLayouts;
    }
    
    /**
     * Check if valid
     */
    bool isValid() const { return m_valid; }
    
private:
    FOpenGLDevice* m_device;
    TArray<TSharedPtr<MonsterRender::RHI::IRHIDescriptorSetLayout>> m_setLayouts;
    bool m_valid = false;
};

/**
 * OpenGL Descriptor Pool Manager
 * Manages descriptor set allocation and UBO binding point assignment
 * Reference: UE5 doesn't need this for OpenGL, but we provide it for API consistency
 */
class FOpenGLDescriptorPoolManager {
public:
    FOpenGLDescriptorPoolManager(FOpenGLDevice* device);
    ~FOpenGLDescriptorPoolManager();
    
    // Non-copyable, non-movable
    FOpenGLDescriptorPoolManager(const FOpenGLDescriptorPoolManager&) = delete;
    FOpenGLDescriptorPoolManager& operator=(const FOpenGLDescriptorPoolManager&) = delete;
    
    /**
     * Allocate a descriptor set
     * In OpenGL, this just creates a tracking object
     * @param layout Descriptor set layout
     * @return Allocated descriptor set
     */
    TSharedPtr<FOpenGLDescriptorSet> allocateDescriptorSet(
        TSharedPtr<FOpenGLDescriptorSetLayout> layout);
    
    /**
     * Begin new frame
     * In OpenGL, this is mostly a no-op but maintains API consistency
     * @param frameNumber Current frame number
     */
    void beginFrame(uint64 frameNumber);
    
    /**
     * Get statistics
     */
    struct Stats {
        uint32 totalSetsAllocated;
        uint32 currentFrameAllocations;
        uint32 maxBindingPointsUsed;
    };
    Stats getStats() const;
    
    /**
     * Reset all descriptor sets (for cleanup or testing)
     */
    void resetAll();
    
    /**
     * Allocate a UBO binding point
     * OpenGL has a limited number of UBO binding points (typically 36-96)
     */
    uint32 allocateBindingPoint();
    
    /**
     * Free a UBO binding point
     */
    void freeBindingPoint(uint32 bindingPoint);
    
private:
    FOpenGLDevice* m_device;
    
    // Frame tracking
    uint64 m_currentFrame = 0;
    
    // Statistics
    mutable Stats m_stats;
    mutable std::mutex m_mutex;
    
    // UBO binding point management
    static constexpr uint32 MAX_UBO_BINDING_POINTS = 96; // GL_MAX_UNIFORM_BUFFER_BINDINGS
    TArray<bool> m_bindingPointsInUse;
    uint32 m_nextBindingPoint = 0;
    
    // Track allocated descriptor sets for cleanup
    TArray<TWeakPtr<FOpenGLDescriptorSet>> m_allocatedSets;
};

} // namespace MonsterEngine::OpenGL
