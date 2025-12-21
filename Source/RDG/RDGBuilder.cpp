#include "RDG/RDGBuilder.h"
#include "Core/Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRDG, Log, All);

namespace MonsterRender {
namespace RDG {

FRDGBuilder::FRDGBuilder(RHI::IRHIDevice* rhiDevice, const FString& debugName)
    : m_rhiDevice(rhiDevice)
    , m_debugName(debugName)
{
    if (!m_rhiDevice)
    {
        MR_LOG(LogRDG, Error, "FRDGBuilder created with null RHI device");
    }
    
    MR_LOG(LogRDG, Verbose, "FRDGBuilder created: %s", *m_debugName);
}

FRDGBuilder::~FRDGBuilder()
{
    // Clean up resources
    for (FRDGTexture* texture : m_textures)
    {
        delete texture;
    }
    m_textures.Empty();
    
    for (FRDGBuffer* buffer : m_buffers)
    {
        delete buffer;
    }
    m_buffers.Empty();
    
    for (FRDGPass* pass : m_passes)
    {
        delete pass;
    }
    m_passes.Empty();
    
    MR_LOG(LogRDG, Verbose, "FRDGBuilder destroyed: %s", *m_debugName);
}

FRDGTextureRef FRDGBuilder::createTexture(const FString& inName, const FRDGTextureDesc& desc)
{
    if (!m_rhiDevice)
    {
        MR_LOG(LogRDG, Error, "Cannot create texture '%s': RHI device is null", *inName);
        return nullptr;
    }
    
    // Create RDG texture wrapper
    FRDGTexture* texture = new FRDGTexture(inName, desc);
    m_textures.Add(texture);
    
    MR_LOG(LogRDG, Verbose, "Created RDG texture: %s (%ux%u, format=%d)", 
           *inName, desc.width, desc.height, static_cast<int32>(desc.format));
    
    return texture;
}

FRDGBufferRef FRDGBuilder::createBuffer(const FString& inName, const FRDGBufferDesc& desc)
{
    if (!m_rhiDevice)
    {
        MR_LOG(LogRDG, Error, "Cannot create buffer '%s': RHI device is null", *inName);
        return nullptr;
    }
    
    // Create RDG buffer wrapper
    FRDGBuffer* buffer = new FRDGBuffer(inName, desc);
    m_buffers.Add(buffer);
    
    MR_LOG(LogRDG, Verbose, "Created RDG buffer: %s (size=%u bytes)", 
           *inName, desc.size);
    
    return buffer;
}

FRDGTextureRef FRDGBuilder::registerExternalTexture(
    const FString& inName,
    RHI::IRHITexture* inTexture,
    ERHIAccess initialState)
{
    if (!inTexture)
    {
        MR_LOG(LogRDG, Error, "Cannot register null external texture: %s", *inName);
        return nullptr;
    }
    
    // Create descriptor from RHI texture
    const RHI::TextureDesc& rhiDesc = inTexture->getDesc();
    FRDGTextureDesc desc;
    desc.width = rhiDesc.width;
    desc.height = rhiDesc.height;
    desc.depth = rhiDesc.depth;
    desc.arraySize = rhiDesc.arraySize;
    desc.mipLevels = rhiDesc.mipLevels;
    desc.format = rhiDesc.format;
    desc.usage = rhiDesc.usage;
    desc.debugName = inName;
    
    // Create RDG texture wrapper
    FRDGTexture* rdgTexture = new FRDGTexture(inName, desc);
    rdgTexture->m_resourceRHI = inTexture;
    
    // Initialize subresource states with initial state
    for (FRDGSubresourceState& state : rdgTexture->getSubresourceStates())
    {
        state.access = initialState;
    }
    
    m_textures.Add(rdgTexture);
    
    MR_LOG(LogRDG, Verbose, "Registered external texture: %s (%ux%u)", 
           *inName, desc.width, desc.height);
    
    return rdgTexture;
}

FRDGBufferRef FRDGBuilder::registerExternalBuffer(
    const FString& inName,
    RHI::IRHIBuffer* inBuffer,
    ERHIAccess initialState)
{
    if (!inBuffer)
    {
        MR_LOG(LogRDG, Error, "Cannot register null external buffer: %s", *inName);
        return nullptr;
    }
    
    // Create descriptor from RHI buffer
    const RHI::BufferDesc& rhiDesc = inBuffer->getDesc();
    FRDGBufferDesc desc;
    desc.size = rhiDesc.size;
    desc.stride = rhiDesc.stride;
    desc.usage = rhiDesc.usage;
    desc.debugName = inName;
    
    // Create RDG buffer wrapper
    FRDGBuffer* rdgBuffer = new FRDGBuffer(inName, desc);
    rdgBuffer->m_resourceRHI = inBuffer;
    rdgBuffer->getState().access = initialState;
    
    m_buffers.Add(rdgBuffer);
    
    MR_LOG(LogRDG, Verbose, "Registered external buffer: %s (size=%u bytes)", 
           *inName, desc.size);
    
    return rdgBuffer;
}

void FRDGBuilder::execute(RHI::IRHICommandList& rhiCmdList)
{
    if (m_bExecuted)
    {
        MR_LOG(LogRDG, Error, "FRDGBuilder::Execute called multiple times on graph: %s", 
               *m_debugName);
        return;
    }
    
    MR_LOG(LogRDG, Log, "Executing render graph: %s (%d passes, %d textures, %d buffers)",
           *m_debugName, m_passes.Num(), m_textures.Num(), m_buffers.Num());
    
    // Phase 1: Validate graph
    IF_RDG_ENABLE_DEBUG(_validateGraph());
    
    // Phase 2: Build dependency graph (TODO: Implement in Phase 2)
    _buildDependencyGraph();
    
    // Phase 3: Topological sort (TODO: Implement in Phase 2)
    _topologicalSort();
    
    // Phase 4: Analyze resource lifetimes (TODO: Implement in Phase 3)
    _analyzeResourceLifetimes();
    
    // Phase 5: Insert transitions (TODO: Implement in Phase 3)
    _insertTransitions();
    
    m_bCompiled = true;
    
    // Phase 6: Allocate RHI resources (TODO: Implement in Phase 4)
    _allocateResources(rhiCmdList);
    
#if RDG_ENABLE_DEBUG
    m_bAllowRHIAccess = true;
#endif
    
    // Phase 7: Execute passes (TODO: Implement in Phase 4)
    for (FRDGPass* pass : m_sortedPasses)
    {
        if (!pass->isCulled())
        {
            _executePass(rhiCmdList, pass);
        }
    }
    
#if RDG_ENABLE_DEBUG
    m_bAllowRHIAccess = false;
#endif
    
    // Phase 8: Release resources (TODO: Implement in Phase 4)
    _releaseResources();
    
    m_bExecuted = true;
    
    MR_LOG(LogRDG, Log, "Render graph execution complete: %s", *m_debugName);
}

void FRDGBuilder::_buildDependencyGraph()
{
    // TODO: Implement in Phase 2
    // This will analyze pass dependencies based on resource read/write patterns
    MR_LOG(LogRDG, Verbose, "Building dependency graph (stub)");
}

void FRDGBuilder::_topologicalSort()
{
    // TODO: Implement in Phase 2
    // For now, just use the original pass order
    m_sortedPasses = m_passes;
    MR_LOG(LogRDG, Verbose, "Topological sort (stub): %d passes", m_sortedPasses.Num());
}

void FRDGBuilder::_analyzeResourceLifetimes()
{
    // TODO: Implement in Phase 3
    // This will determine when resources are first/last used
    MR_LOG(LogRDG, Verbose, "Analyzing resource lifetimes (stub)");
}

void FRDGBuilder::_insertTransitions()
{
    // TODO: Implement in Phase 3
    // This will insert resource barriers between passes
    MR_LOG(LogRDG, Verbose, "Inserting resource transitions (stub)");
}

void FRDGBuilder::_allocateResources(RHI::IRHICommandList& rhiCmdList)
{
    // TODO: Implement in Phase 4
    // Allocate RHI resources for RDG textures/buffers
    MR_LOG(LogRDG, Verbose, "Allocating RHI resources (stub)");
    
    // For now, just allocate all textures
    for (FRDGTexture* texture : m_textures)
    {
        if (!texture->hasRHI())
        {
            const FRDGTextureDesc& desc = texture->getDesc();
            
            // Convert RDG descriptor to RHI descriptor
            RHI::TextureDesc rhiDesc;
            rhiDesc.width = desc.width;
            rhiDesc.height = desc.height;
            rhiDesc.depth = desc.depth;
            rhiDesc.arraySize = desc.arraySize;
            rhiDesc.mipLevels = desc.mipLevels;
            rhiDesc.format = desc.format;
            rhiDesc.usage = desc.usage;
            rhiDesc.debugName = String(texture->getName().begin(), texture->getName().end());
            
            // Create RHI texture
            TSharedPtr<RHI::IRHITexture> rhiTexture = m_rhiDevice->createTexture(rhiDesc);
            if (rhiTexture)
            {
                texture->m_resourceRHI = rhiTexture.Get();
                MR_LOG(LogRDG, Verbose, "Allocated RHI texture: %s", *texture->getName());
            }
            else
            {
                MR_LOG(LogRDG, Error, "Failed to allocate RHI texture: %s", *texture->getName());
            }
        }
    }
    
    // Allocate buffers
    for (FRDGBuffer* buffer : m_buffers)
    {
        if (!buffer->hasRHI())
        {
            const FRDGBufferDesc& desc = buffer->getDesc();
            
            // Convert RDG descriptor to RHI descriptor
            RHI::BufferDesc rhiDesc;
            rhiDesc.size = desc.size;
            rhiDesc.stride = desc.stride;
            rhiDesc.usage = desc.usage;
            rhiDesc.debugName = String(buffer->getName().begin(), buffer->getName().end());
            
            // Create RHI buffer
            TSharedPtr<RHI::IRHIBuffer> rhiBuffer = m_rhiDevice->createBuffer(rhiDesc);
            if (rhiBuffer)
            {
                buffer->m_resourceRHI = rhiBuffer.Get();
                MR_LOG(LogRDG, Verbose, "Allocated RHI buffer: %s", *buffer->getName());
            }
            else
            {
                MR_LOG(LogRDG, Error, "Failed to allocate RHI buffer: %s", *buffer->getName());
            }
        }
    }
}

void FRDGBuilder::_executePass(RHI::IRHICommandList& rhiCmdList, FRDGPass* pass)
{
    if (!pass)
    {
        return;
    }
    
    MR_LOG(LogRDG, Verbose, "Executing pass: %s", *pass->getName());
    
    // Execute pre-pass transitions
    auto transitionsIt = m_passTransitions.Find(pass->getHandle());
    if (transitionsIt)
    {
        _executeTransitions(rhiCmdList, *transitionsIt);
    }
    
    // Begin debug event
    String passName(pass->getName().begin(), pass->getName().end());
    rhiCmdList.beginEvent(passName);
    
    // Execute pass
#if RDG_ENABLE_DEBUG
    // Enable RHI access for all resources used by this pass
    for (const FRDGTextureAccess& access : pass->getTextureAccesses())
    {
        if (access.texture)
        {
            access.texture->m_bAllowRHIAccess = true;
        }
    }
    for (const FRDGBufferAccess& access : pass->getBufferAccesses())
    {
        if (access.buffer)
        {
            access.buffer->m_bAllowRHIAccess = true;
        }
    }
#endif
    
    pass->execute(rhiCmdList);
    
#if RDG_ENABLE_DEBUG
    // Disable RHI access after pass execution
    for (const FRDGTextureAccess& access : pass->getTextureAccesses())
    {
        if (access.texture)
        {
            access.texture->m_bAllowRHIAccess = false;
        }
    }
    for (const FRDGBufferAccess& access : pass->getBufferAccesses())
    {
        if (access.buffer)
        {
            access.buffer->m_bAllowRHIAccess = false;
        }
    }
#endif
    
    // End debug event
    rhiCmdList.endEvent();
}

void FRDGBuilder::_executeTransitions(RHI::IRHICommandList& rhiCmdList, 
                                     const TArray<FRDGTransition>& transitions)
{
    // TODO: Implement in Phase 3
    // Execute resource barriers
    for (const FRDGTransition& transition : transitions)
    {
        if (transition.resource)
        {
            MR_LOG(LogRDG, Verbose, "Transition: %s (state %d -> %d)",
                   *transition.resource->getName(),
                   static_cast<int32>(transition.stateBefore),
                   static_cast<int32>(transition.stateAfter));
            
            // Call RHI transition
            rhiCmdList.transitionResource(
                TSharedPtr<RHI::IRHIResource>(transition.resource->getRHI(), [](RHI::IRHIResource*){}),
                static_cast<RHI::EResourceUsage>(transition.stateBefore),
                static_cast<RHI::EResourceUsage>(transition.stateAfter));
        }
    }
}

void FRDGBuilder::_releaseResources()
{
    // TODO: Implement in Phase 4
    // Release transient resources
    MR_LOG(LogRDG, Verbose, "Releasing transient resources (stub)");
}

void FRDGBuilder::_validateGraph()
{
#if RDG_ENABLE_DEBUG
    MR_LOG(LogRDG, Verbose, "Validating render graph: %s", *m_debugName);
    
    // Validate passes
    for (FRDGPass* pass : m_passes)
    {
        if (!pass)
        {
            MR_LOG(LogRDG, Error, "Null pass in graph");
            continue;
        }
        
        // Validate texture accesses
        for (const FRDGTextureAccess& access : pass->getTextureAccesses())
        {
            if (!access.texture)
            {
                MR_LOG(LogRDG, Error, "Pass '%s' has null texture access", *pass->getName());
                continue;
            }
            
            if (!isValidAccess(access.access))
            {
                MR_LOG(LogRDG, Error, "Pass '%s' has invalid access state for texture '%s'",
                       *pass->getName(), *access.texture->getName());
            }
        }
        
        // Validate buffer accesses
        for (const FRDGBufferAccess& access : pass->getBufferAccesses())
        {
            if (!access.buffer)
            {
                MR_LOG(LogRDG, Error, "Pass '%s' has null buffer access", *pass->getName());
                continue;
            }
            
            if (!isValidAccess(access.access))
            {
                MR_LOG(LogRDG, Error, "Pass '%s' has invalid access state for buffer '%s'",
                       *pass->getName(), *access.buffer->getName());
            }
        }
    }
    
    MR_LOG(LogRDG, Verbose, "Graph validation complete");
#endif
}

}} // namespace MonsterRender::RDG
