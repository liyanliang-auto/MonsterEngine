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
    virtual void updateUniformBuffer(uint32 binding, TSharedPtr<MonsterRender::RHI::IRHIBuffer> buffer,
                                    uint32 offset = 0, uint32 range = 0) override;
    
    virtual void updateTexture(uint32 binding, TSharedPtr<MonsterRender::RHI::IRHITexture> texture) override;
    
    virtual void updateSampler(uint32 binding, TSharedPtr<MonsterRender::RHI::IRHISampler> sampler) override;
    
    virtual void updateCombinedTextureSampler(uint32 binding, TSharedPtr<MonsterRender::RHI::IRHITexture> texture,
                                             TSharedPtr<MonsterRender::RHI::IRHISampler> sampler) override;
    
    virtual TSharedPtr<MonsterRender::RHI::IRHIDescriptorSetLayout> getLayout() const override;
    
    // IRHIResource interface
    virtual MonsterRender::RHI::ERHIBackend getBackendType() const override {
        return MonsterRender::RHI::ERHIBackend::OpenGL;
    }
    
    // OpenGL-specific helper methods
    void updateBuffer(uint32 binding, TSharedPtr<MonsterRender::RHI::IRHIBuffer> buffer,
                     uint64 offset, uint64 range);
    
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
    
    // IRHIDescriptorSetLayout interface
    virtual uint32 getSetIndex() const override { return m_setIndex; }
    
    virtual const TArray<MonsterRender::RHI::FDescriptorSetLayoutBinding>& getBindings() const override { 
        return m_bindings; 
    }
    
    // IRHIResource interface
    virtual MonsterRender::RHI::ERHIBackend getBackendType() const override {
        return MonsterRender::RHI::ERHIBackend::OpenGL;
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
    
    // IRHIPipelineLayout interface
    virtual const TArray<TSharedPtr<MonsterRender::RHI::IRHIDescriptorSetLayout>>& getSetLayouts() const override {
        return m_setLayouts;
    }
    
    virtual const TArray<MonsterRender::RHI::FPushConstantRange>& getPushConstantRanges() const override;
    
    // IRHIResource interface
    virtual MonsterRender::RHI::ERHIBackend getBackendType() const override {
        return MonsterRender::RHI::ERHIBackend::OpenGL;
    }
    
    /**
     * Check if valid
     */
    bool isValid() const { return m_valid; }
    
private:
    FOpenGLDevice* m_device;
    TArray<TSharedPtr<MonsterRender::RHI::IRHIDescriptorSetLayout>> m_setLayouts;
    TArray<MonsterRender::RHI::FPushConstantRange> m_pushConstantRanges;
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
    };
    Stats getStats() const;
    
    /**
     * Reset all descriptor sets (for cleanup or testing)
     */
    void resetAll();
    
private:
    FOpenGLDevice* m_device;
    
    // Frame tracking
    uint64 m_currentFrame = 0;
    
    // Statistics
    mutable Stats m_stats;
    mutable std::mutex m_mutex;
    
    // Track allocated descriptor sets for cleanup
    TArray<TWeakPtr<FOpenGLDescriptorSet>> m_allocatedSets;
};

} // namespace MonsterEngine::OpenGL
