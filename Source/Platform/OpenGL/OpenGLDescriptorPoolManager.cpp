#include "Platform/OpenGL/OpenGLDescriptorPoolManager.h"
#include "Platform/OpenGL/OpenGLDevice.h"
#include "Platform/OpenGL/OpenGLResources.h"
#include "Platform/OpenGL/OpenGLFunctions.h"
#include "Core/Logging/LogMacros.h"
#include "RHI/RHIDefinitions.h"

DEFINE_LOG_CATEGORY_STATIC(LogOpenGLRHI, Log, All);

namespace MonsterEngine::OpenGL {

// ============================================================================
// FOpenGLDescriptorSet
// ============================================================================

FOpenGLDescriptorSet::FOpenGLDescriptorSet(FOpenGLDevice* device, uint32 setIndex)
    : m_device(device)
    , m_setIndex(setIndex)
{
    MR_LOG(LogOpenGLRHI, Verbose, "Created OpenGL descriptor set for set index %u", setIndex);
}

FOpenGLDescriptorSet::~FOpenGLDescriptorSet()
{
    m_bufferBindings.clear();
    m_textureBindings.clear();
}

// IRHIDescriptorSet interface implementation
void FOpenGLDescriptorSet::updateUniformBuffer(uint32 binding, 
                                              TSharedPtr<MonsterRender::RHI::IRHIBuffer> buffer,
                                              uint32 offset, uint32 range)
{
    updateBuffer(binding, buffer, offset, range);
}

void FOpenGLDescriptorSet::updateTexture(uint32 binding, 
                                        TSharedPtr<MonsterRender::RHI::IRHITexture> texture)
{
    TextureBinding textureBinding;
    textureBinding.texture = texture;
    textureBinding.sampler = nullptr;
    
    m_textureBindings[binding] = textureBinding;
    m_dirty = true;
    
    MR_LOG(LogOpenGLRHI, VeryVerbose, "Updated texture binding %u in descriptor set %u", 
           binding, m_setIndex);
}

void FOpenGLDescriptorSet::updateSampler(uint32 binding, 
                                        TSharedPtr<MonsterRender::RHI::IRHISampler> sampler)
{
    // In OpenGL, samplers are typically combined with textures
    // Store sampler for later combination
    if (m_textureBindings.Find(binding) != nullptr) {
        m_textureBindings[binding].sampler = sampler.get();
        m_dirty = true;
    }
    
    MR_LOG(LogOpenGLRHI, VeryVerbose, "Updated sampler binding %u in descriptor set %u", 
           binding, m_setIndex);
}

void FOpenGLDescriptorSet::updateCombinedTextureSampler(uint32 binding, 
                                                       TSharedPtr<MonsterRender::RHI::IRHITexture> texture,
                                                       TSharedPtr<MonsterRender::RHI::IRHISampler> sampler)
{
    TextureBinding textureBinding;
    textureBinding.texture = texture;
    textureBinding.sampler = sampler.get();
    
    m_textureBindings[binding] = textureBinding;
    m_dirty = true;
    
    MR_LOG(LogOpenGLRHI, VeryVerbose, "Updated combined texture+sampler binding %u in descriptor set %u", 
           binding, m_setIndex);
}

TSharedPtr<MonsterRender::RHI::IRHIDescriptorSetLayout> FOpenGLDescriptorSet::getLayout() const
{
    // OpenGL descriptor sets don't store layout reference
    // This would need to be added if required
    return nullptr;
}

// OpenGL-specific helper method
void FOpenGLDescriptorSet::updateBuffer(uint32 binding, 
                                        TSharedPtr<MonsterRender::RHI::IRHIBuffer> buffer,
                                        uint64 offset, uint64 range)
{
    BufferBinding bufferBinding;
    bufferBinding.buffer = buffer;
    bufferBinding.offset = offset;
    bufferBinding.range = range;
    
    m_bufferBindings[binding] = bufferBinding;
    m_dirty = true;
    
    MR_LOG(LogOpenGLRHI, VeryVerbose, "Updated buffer binding %u in descriptor set %u", 
           binding, m_setIndex);
}

void FOpenGLDescriptorSet::applyBindings()
{
    if (!m_dirty) {
        return; // Optimization: skip if nothing changed
    }
    
    // Apply buffer bindings (UBOs)
    for (const auto& pair : m_bufferBindings) {
        uint32 binding = pair.Key;
        const BufferBinding& bufferBinding = pair.Value;
        
        if (bufferBinding.buffer) {
            auto* glBuffer = dynamic_cast<FOpenGLBuffer*>(bufferBinding.buffer.get());
            if (glBuffer) {
                GLuint bufferHandle = glBuffer->GetGLBuffer();
                
                // Calculate actual binding point using unified constant: setIndex * MAX_BINDINGS_PER_SET + binding
                // This ensures deterministic mapping from (set, binding) to OpenGL binding point
                // Reference: UE5 uses similar fixed mapping for descriptor sets
                uint32 actualBindingPoint = m_setIndex * MonsterRender::RHI::RHILimits::MAX_BINDINGS_PER_SET + binding;
                
                if (bufferBinding.range > 0) {
                    glBindBufferRange(GL_UNIFORM_BUFFER, actualBindingPoint, bufferHandle,
                                    bufferBinding.offset, bufferBinding.range);
                } else {
                    glBindBufferBase(GL_UNIFORM_BUFFER, actualBindingPoint, bufferHandle);
                }
                
                MR_LOG(LogOpenGLRHI, VeryVerbose, 
                       "Applied UBO binding: set=%u, binding=%u, actualPoint=%u, buffer=%u",
                       m_setIndex, binding, actualBindingPoint, bufferHandle);
            }
        }
    }
    
    // Apply texture bindings
    uint32 textureUnit = 0;
    for (const auto& pair : m_textureBindings) {
        uint32 binding = pair.Key;
        const TextureBinding& textureBinding = pair.Value;
        
        if (textureBinding.texture) {
            auto* glTexture = dynamic_cast<FOpenGLTexture*>(textureBinding.texture.get());
            if (glTexture) {
                // Activate texture unit and bind texture
                glActiveTexture(GL_TEXTURE0 + textureUnit);
                glBindTexture(glTexture->GetGLTarget(), glTexture->GetGLTexture());
                
                // Bind sampler if provided
                if (textureBinding.sampler) {
                    auto* glSampler = dynamic_cast<FOpenGLSampler*>(textureBinding.sampler);
                    if (glSampler) {
                        glBindSampler(textureUnit, glSampler->GetGLSampler());
                    }
                }
                
                MR_LOG(LogOpenGLRHI, VeryVerbose, 
                       "Applied texture binding: set=%u, binding=%u, unit=%u, texture=%u",
                       m_setIndex, binding, textureUnit, glTexture->GetGLTexture());
                
                textureUnit++;
            }
        }
    }
    
    m_dirty = false;
}

// ============================================================================
// FOpenGLDescriptorSetLayout
// ============================================================================

FOpenGLDescriptorSetLayout::FOpenGLDescriptorSetLayout(
    FOpenGLDevice* device,
    const MonsterRender::RHI::FDescriptorSetLayoutDesc& desc)
    : m_device(device)
    , m_setIndex(desc.setIndex)
    , m_bindings(desc.bindings)
    , m_valid(false)
{
    // Validate set index
    if (m_setIndex >= MonsterRender::RHI::RHILimits::MAX_DESCRIPTOR_SETS) {
        MR_LOG(LogOpenGLRHI, Error, 
               "Descriptor set index %u exceeds maximum allowed (%u)",
               m_setIndex, MonsterRender::RHI::RHILimits::MAX_DESCRIPTOR_SETS);
        return;
    }
    
    // Validate binding indices and count
    // Reference: UE5 validates descriptor set layout against device limits
    uint32 maxBindingIndex = 0;
    for (const auto& binding : m_bindings) {
        if (binding.binding >= MonsterRender::RHI::RHILimits::MAX_BINDINGS_PER_SET) {
            MR_LOG(LogOpenGLRHI, Error, 
                   "Binding index %u in set %u exceeds maximum allowed (%u)",
                   binding.binding, m_setIndex, MonsterRender::RHI::RHILimits::MAX_BINDINGS_PER_SET);
            return;
        }
        maxBindingIndex = (maxBindingIndex > binding.binding) ? maxBindingIndex : binding.binding;
    }
    
    // Calculate actual OpenGL binding point range for this set
    const uint32 baseBindingPoint = m_setIndex * MonsterRender::RHI::RHILimits::MAX_BINDINGS_PER_SET;
    const uint32 maxBindingPoint = baseBindingPoint + maxBindingIndex;
    
    MR_LOG(LogOpenGLRHI, Verbose, 
           "Created OpenGL descriptor set layout: set=%u, bindings=%llu, GL binding range=[%u, %u]",
           m_setIndex, static_cast<uint64>(m_bindings.size()), baseBindingPoint, maxBindingPoint);
    
    m_valid = true;
}

FOpenGLDescriptorSetLayout::~FOpenGLDescriptorSetLayout()
{
    m_bindings.clear();
}

// ============================================================================
// FOpenGLPipelineLayout
// ============================================================================

FOpenGLPipelineLayout::FOpenGLPipelineLayout(
    FOpenGLDevice* device,
    const MonsterRender::RHI::FPipelineLayoutDesc& desc)
    : m_device(device)
    , m_setLayouts(desc.setLayouts)
    , m_pushConstantRanges(desc.pushConstantRanges)
    , m_valid(true)
{
    MR_LOG(LogOpenGLRHI, Verbose, 
           "Created OpenGL pipeline layout with %llu descriptor sets",
           static_cast<uint64>(m_setLayouts.size()));
}

FOpenGLPipelineLayout::~FOpenGLPipelineLayout()
{
    m_setLayouts.clear();
    m_pushConstantRanges.Empty();
}

const TArray<MonsterRender::RHI::FPushConstantRange>& FOpenGLPipelineLayout::getPushConstantRanges() const
{
    return m_pushConstantRanges;
}

// ============================================================================
// FOpenGLDescriptorPoolManager
// ============================================================================

FOpenGLDescriptorPoolManager::FOpenGLDescriptorPoolManager(FOpenGLDevice* device)
    : m_device(device)
{
    m_stats.totalSetsAllocated = 0;
    m_stats.currentFrameAllocations = 0;
    
    MR_LOG(LogOpenGLRHI, Log, "Initialized OpenGL descriptor pool manager with deterministic binding mapping");
}

FOpenGLDescriptorPoolManager::~FOpenGLDescriptorPoolManager()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_allocatedSets.clear();
    
    MR_LOG(LogOpenGLRHI, Log, "Destroyed OpenGL descriptor pool manager");
}

TSharedPtr<FOpenGLDescriptorSet> FOpenGLDescriptorPoolManager::allocateDescriptorSet(
    TSharedPtr<FOpenGLDescriptorSetLayout> layout)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!layout) {
        MR_LOG(LogOpenGLRHI, Error, "Cannot allocate descriptor set with null layout");
        return nullptr;
    }
    
    // Create descriptor set
    TSharedPtr<FOpenGLDescriptorSet> descriptorSet = MakeShared<FOpenGLDescriptorSet>(m_device, layout->getSetIndex());
    
    // Track the allocated set
    m_allocatedSets.Add(descriptorSet);
    
    // Update statistics
    ++m_stats.totalSetsAllocated;
    ++m_stats.currentFrameAllocations;
    
    MR_LOG(LogOpenGLRHI, VeryVerbose, 
           "Allocated descriptor set for set index %u (total: %u)",
           layout->getSetIndex(), m_stats.totalSetsAllocated);
    
    return descriptorSet;
}

void FOpenGLDescriptorPoolManager::beginFrame(uint64 frameNumber)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_currentFrame = frameNumber;
    m_stats.currentFrameAllocations = 0;
    
    // Clean up expired weak pointers
    uint32 cleanedCount = 0;
    for (int32 i = static_cast<int32>(m_allocatedSets.Num()) - 1; i >= 0; --i) {
        if (!m_allocatedSets[i].IsValid()) {
            m_allocatedSets.RemoveAt(i);
            ++cleanedCount;
        }
    }
    
    if (cleanedCount > 0) {
        MR_LOG(LogOpenGLRHI, VeryVerbose, 
               "Cleaned up %u expired descriptor sets in frame %llu",
               cleanedCount, frameNumber);
    }
}

FOpenGLDescriptorPoolManager::Stats FOpenGLDescriptorPoolManager::getStats() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stats;
}

void FOpenGLDescriptorPoolManager::resetAll()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_allocatedSets.clear();
    
    m_stats.totalSetsAllocated = 0;
    m_stats.currentFrameAllocations = 0;
    
    MR_LOG(LogOpenGLRHI, Verbose, "Reset all OpenGL descriptor sets");
}


} // namespace MonsterEngine::OpenGL
