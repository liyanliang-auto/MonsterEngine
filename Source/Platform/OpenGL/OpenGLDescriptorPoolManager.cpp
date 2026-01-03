#include "Platform/OpenGL/OpenGLDescriptorPoolManager.h"
#include "Platform/OpenGL/OpenGLDevice.h"
#include "Platform/OpenGL/OpenGLResources.h"
#include "Core/Logging/LogMacros.h"
#include "glad/glad.h"

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

void FOpenGLDescriptorSet::updateTexture(uint32 binding, 
                                         TSharedPtr<MonsterRender::RHI::IRHITexture> texture,
                                         MonsterRender::RHI::IRHISampler* sampler)
{
    TextureBinding textureBinding;
    textureBinding.texture = texture;
    textureBinding.sampler = sampler;
    
    m_textureBindings[binding] = textureBinding;
    m_dirty = true;
    
    MR_LOG(LogOpenGLRHI, VeryVerbose, "Updated texture binding %u in descriptor set %u", 
           binding, m_setIndex);
}

void FOpenGLDescriptorSet::updateBuffers(
    MonsterRender::TSpan<const MonsterRender::RHI::FDescriptorBufferUpdate> updates)
{
    for (const auto& update : updates) {
        updateBuffer(update.binding, update.buffer, update.offset, update.range);
    }
}

void FOpenGLDescriptorSet::updateTextures(
    MonsterRender::TSpan<const MonsterRender::RHI::FDescriptorTextureUpdate> updates)
{
    for (const auto& update : updates) {
        updateTexture(update.binding, update.texture, update.sampler);
    }
}

void FOpenGLDescriptorSet::applyBindings()
{
    if (!m_dirty) {
        return; // Optimization: skip if nothing changed
    }
    
    // Apply buffer bindings (UBOs)
    for (const auto& pair : m_bufferBindings) {
        uint32 binding = pair.first;
        const BufferBinding& bufferBinding = pair.second;
        
        if (bufferBinding.buffer) {
            auto* glBuffer = dynamic_cast<FOpenGLBuffer*>(bufferBinding.buffer.get());
            if (glBuffer) {
                GLuint bufferHandle = glBuffer->GetGLBuffer();
                
                // Calculate actual binding point: setIndex * MAX_BINDINGS_PER_SET + binding
                // This ensures different sets don't conflict
                uint32 actualBindingPoint = m_setIndex * 16 + binding;
                
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
        uint32 binding = pair.first;
        const TextureBinding& textureBinding = pair.second;
        
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
    , m_valid(true)
{
    MR_LOG(LogOpenGLRHI, Verbose, 
           "Created OpenGL descriptor set layout for set %u with %llu bindings",
           m_setIndex, static_cast<uint64>(m_bindings.size()));
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
    , m_valid(true)
{
    MR_LOG(LogOpenGLRHI, Verbose, 
           "Created OpenGL pipeline layout with %llu descriptor sets",
           static_cast<uint64>(m_setLayouts.size()));
}

FOpenGLPipelineLayout::~FOpenGLPipelineLayout()
{
    m_setLayouts.clear();
}

// ============================================================================
// FOpenGLDescriptorPoolManager
// ============================================================================

FOpenGLDescriptorPoolManager::FOpenGLDescriptorPoolManager(FOpenGLDevice* device)
    : m_device(device)
{
    // Initialize binding point tracking
    m_bindingPointsInUse.resize(MAX_UBO_BINDING_POINTS);
    for (uint32 i = 0; i < MAX_UBO_BINDING_POINTS; ++i) {
        m_bindingPointsInUse[i] = false;
    }
    
    m_stats.totalSetsAllocated = 0;
    m_stats.currentFrameAllocations = 0;
    m_stats.maxBindingPointsUsed = 0;
    
    MR_LOG(LogOpenGLRHI, Log, "Initialized OpenGL descriptor pool manager");
}

FOpenGLDescriptorPoolManager::~FOpenGLDescriptorPoolManager()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_allocatedSets.clear();
    m_bindingPointsInUse.clear();
    
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
    auto descriptorSet = MakeShared<FOpenGLDescriptorSet>(m_device, layout->getSetIndex());
    
    if (!descriptorSet) {
        MR_LOG(LogOpenGLRHI, Error, "Failed to create OpenGL descriptor set");
        return nullptr;
    }
    
    // Track the allocated set
    m_allocatedSets.push_back(descriptorSet);
    
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
    for (int32 i = m_allocatedSets.size() - 1; i >= 0; --i) {
        if (m_allocatedSets[i].expired()) {
            m_allocatedSets.erase(m_allocatedSets.begin() + i);
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
    
    // Reset binding points
    for (uint32 i = 0; i < MAX_UBO_BINDING_POINTS; ++i) {
        m_bindingPointsInUse[i] = false;
    }
    m_nextBindingPoint = 0;
    
    m_stats.totalSetsAllocated = 0;
    m_stats.currentFrameAllocations = 0;
    m_stats.maxBindingPointsUsed = 0;
    
    MR_LOG(LogOpenGLRHI, Verbose, "Reset all OpenGL descriptor sets");
}

uint32 FOpenGLDescriptorPoolManager::allocateBindingPoint()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Find next available binding point
    for (uint32 i = 0; i < MAX_UBO_BINDING_POINTS; ++i) {
        uint32 candidatePoint = (m_nextBindingPoint + i) % MAX_UBO_BINDING_POINTS;
        
        if (!m_bindingPointsInUse[candidatePoint]) {
            m_bindingPointsInUse[candidatePoint] = true;
            m_nextBindingPoint = (candidatePoint + 1) % MAX_UBO_BINDING_POINTS;
            
            // Update max used
            uint32 usedCount = 0;
            for (uint32 j = 0; j < MAX_UBO_BINDING_POINTS; ++j) {
                if (m_bindingPointsInUse[j]) {
                    ++usedCount;
                }
            }
            m_stats.maxBindingPointsUsed = usedCount > m_stats.maxBindingPointsUsed 
                                          ? usedCount 
                                          : m_stats.maxBindingPointsUsed;
            
            MR_LOG(LogOpenGLRHI, VeryVerbose, 
                   "Allocated UBO binding point %u (total in use: %u)",
                   candidatePoint, usedCount);
            
            return candidatePoint;
        }
    }
    
    MR_LOG(LogOpenGLRHI, Error, 
           "Failed to allocate UBO binding point - all %u points are in use",
           MAX_UBO_BINDING_POINTS);
    
    return 0; // Return 0 as fallback (will likely cause issues)
}

void FOpenGLDescriptorPoolManager::freeBindingPoint(uint32 bindingPoint)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (bindingPoint < MAX_UBO_BINDING_POINTS) {
        m_bindingPointsInUse[bindingPoint] = false;
        
        MR_LOG(LogOpenGLRHI, VeryVerbose, "Freed UBO binding point %u", bindingPoint);
    }
}

} // namespace MonsterEngine::OpenGL
