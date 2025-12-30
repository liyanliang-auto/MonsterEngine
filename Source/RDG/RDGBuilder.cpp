#include "RDG/RDGBuilder.h"
#include "Core/Logging/LogMacros.h"

// Use global log category (defined in LogCategories.cpp)
using MonsterRender::LogRDG;

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
    MR_LOG(LogRDG, Verbose, "Building dependency graph for %d passes", m_passes.Num());
    
    // Clear previous dependency data
    for (FRDGPass* pass : m_passes)
    {
        pass->m_dependencies.Empty();
        pass->m_dependents.Empty();
    }
    
    // Build read-after-write (RAW), write-after-read (WAR), and write-after-write (WAW) dependencies
    // Track last writer and readers for each resource
    TMap<FRDGTexture*, FRDGPassHandle> lastTextureWriter;
    TMap<FRDGTexture*, TArray<FRDGPassHandle>> lastTextureReaders;
    TMap<FRDGBuffer*, FRDGPassHandle> lastBufferWriter;
    TMap<FRDGBuffer*, TArray<FRDGPassHandle>> lastBufferReaders;
    
    for (FRDGPass* pass : m_passes)
    {
        const FRDGPassHandle passHandle = pass->getHandle();
        
        // Process texture accesses
        for (const FRDGTextureAccess& access : pass->getTextureAccesses())
        {
            if (!access.texture)
            {
                continue;
            }
            
            FRDGTexture* texture = access.texture;
            const bool bIsWrite = isWritableAccess(access.access);
            const bool bIsRead = isReadOnlyAccess(access.access);
            
            // Write-after-write (WAW) dependency
            if (bIsWrite)
            {
                auto* lastWriter = lastTextureWriter.Find(texture);
                if (lastWriter && lastWriter->isValid())
                {
                    FRDGPass* writerPass = m_passes[lastWriter->index];
                    pass->m_dependencies.AddUnique(writerPass->getHandle());
                    writerPass->m_dependents.AddUnique(passHandle);
                }
                
                // Write-after-read (WAR) dependency
                auto* readers = lastTextureReaders.Find(texture);
                if (readers)
                {
                    for (const FRDGPassHandle& readerHandle : *readers)
                    {
                        if (readerHandle.isValid())
                        {
                            FRDGPass* readerPass = m_passes[readerHandle.index];
                            pass->m_dependencies.AddUnique(readerHandle);
                            readerPass->m_dependents.AddUnique(passHandle);
                        }
                    }
                    readers->Empty();
                }
                
                lastTextureWriter.Add(texture, passHandle);
            }
            
            // Read-after-write (RAW) dependency
            if (bIsRead)
            {
                auto* lastWriter = lastTextureWriter.Find(texture);
                if (lastWriter && lastWriter->isValid())
                {
                    FRDGPass* writerPass = m_passes[lastWriter->index];
                    pass->m_dependencies.AddUnique(writerPass->getHandle());
                    writerPass->m_dependents.AddUnique(passHandle);
                }
                
                // Track this reader
                TArray<FRDGPassHandle>& readers = lastTextureReaders.FindOrAdd(texture);
                readers.AddUnique(passHandle);
            }
        }
        
        // Process buffer accesses (same logic as textures)
        for (const FRDGBufferAccess& access : pass->getBufferAccesses())
        {
            if (!access.buffer)
            {
                continue;
            }
            
            FRDGBuffer* buffer = access.buffer;
            const bool bIsWrite = isWritableAccess(access.access);
            const bool bIsRead = isReadOnlyAccess(access.access);
            
            // Write-after-write (WAW) dependency
            if (bIsWrite)
            {
                auto* lastWriter = lastBufferWriter.Find(buffer);
                if (lastWriter && lastWriter->isValid())
                {
                    FRDGPass* writerPass = m_passes[lastWriter->index];
                    pass->m_dependencies.AddUnique(writerPass->getHandle());
                    writerPass->m_dependents.AddUnique(passHandle);
                }
                
                // Write-after-read (WAR) dependency
                auto* readers = lastBufferReaders.Find(buffer);
                if (readers)
                {
                    for (const FRDGPassHandle& readerHandle : *readers)
                    {
                        if (readerHandle.isValid())
                        {
                            FRDGPass* readerPass = m_passes[readerHandle.index];
                            pass->m_dependencies.AddUnique(readerHandle);
                            readerPass->m_dependents.AddUnique(passHandle);
                        }
                    }
                    readers->Empty();
                }
                
                lastBufferWriter.Add(buffer, passHandle);
            }
            
            // Read-after-write (RAW) dependency
            if (bIsRead)
            {
                auto* lastWriter = lastBufferWriter.Find(buffer);
                if (lastWriter && lastWriter->isValid())
                {
                    FRDGPass* writerPass = m_passes[lastWriter->index];
                    pass->m_dependencies.AddUnique(writerPass->getHandle());
                    writerPass->m_dependents.AddUnique(passHandle);
                }
                
                // Track this reader
                TArray<FRDGPassHandle>& readers = lastBufferReaders.FindOrAdd(buffer);
                readers.AddUnique(passHandle);
            }
        }
    }
    
    // Log dependency statistics
    int32 totalDependencies = 0;
    for (const FRDGPass* pass : m_passes)
    {
        totalDependencies += pass->m_dependencies.Num();
    }
    
    MR_LOG(LogRDG, Verbose, "Dependency graph built: %d total dependencies", totalDependencies);
}

void FRDGBuilder::_topologicalSort()
{
    MR_LOG(LogRDG, Verbose, "Performing topological sort on %d passes", m_passes.Num());
    
    m_sortedPasses.Empty();
    m_sortedPasses.Reserve(m_passes.Num());
    
    // Kahn's algorithm for topological sorting
    // 1. Calculate in-degree for each pass
    TMap<FRDGPassHandle, int32> inDegree;
    for (const FRDGPass* pass : m_passes)
    {
        inDegree.Add(pass->getHandle(), pass->m_dependencies.Num());
    }
    
    // 2. Find all passes with in-degree 0 (no dependencies)
    TArray<FRDGPassHandle> queue;
    for (const FRDGPass* pass : m_passes)
    {
        if (inDegree[pass->getHandle()] == 0)
        {
            queue.Add(pass->getHandle());
        }
    }
    
    // 3. Process passes in topological order
    while (queue.Num() > 0)
    {
        // Pop front (BFS order)
        FRDGPassHandle currentHandle = queue[0];
        queue.RemoveAt(0);
        
        FRDGPass* currentPass = m_passes[currentHandle.index];
        m_sortedPasses.Add(currentPass);
        
        // Reduce in-degree of dependent passes
        for (const FRDGPassHandle& dependentHandle : currentPass->m_dependents)
        {
            int32& degree = inDegree[dependentHandle];
            degree--;
            
            if (degree == 0)
            {
                queue.Add(dependentHandle);
            }
        }
    }
    
    // 4. Check for cycles (if sorted passes count != total passes count)
    if (m_sortedPasses.Num() != m_passes.Num())
    {
        MR_LOG(LogRDG, Error, "Topological sort failed: cycle detected in dependency graph! "
               "Sorted %d passes out of %d total", 
               m_sortedPasses.Num(), m_passes.Num());
        
        // Find passes that weren't sorted (part of cycle)
        TArray<FString> cyclePassNames;
        for (const FRDGPass* pass : m_passes)
        {
            bool bFound = false;
            for (const FRDGPass* sortedPass : m_sortedPasses)
            {
                if (sortedPass == pass)
                {
                    bFound = true;
                    break;
                }
            }
            
            if (!bFound)
            {
                cyclePassNames.Add(pass->getName());
            }
        }
        
        MR_LOG(LogRDG, Error, "Passes involved in cycle: %d", cyclePassNames.Num());
        for (const FString& name : cyclePassNames)
        {
            MR_LOG(LogRDG, Error, "  - %s", *name);
        }
        
        // Fallback: use original pass order
        m_sortedPasses = m_passes;
    }
    
    MR_LOG(LogRDG, Verbose, "Topological sort complete: %d passes sorted", m_sortedPasses.Num());
    
#if RDG_ENABLE_DEBUG
    // Log sorted pass order
    MR_LOG(LogRDG, Verbose, "Pass execution order:");
    for (int32 i = 0; i < m_sortedPasses.Num(); ++i)
    {
        const FRDGPass* pass = m_sortedPasses[i];
        MR_LOG(LogRDG, Verbose, "  %d. %s (dependencies: %d, dependents: %d)",
               i, *pass->getName(), pass->m_dependencies.Num(), pass->m_dependents.Num());
    }
#endif
}

void FRDGBuilder::_analyzeResourceLifetimes()
{
    MR_LOG(LogRDG, Verbose, "Analyzing resource lifetimes for %d textures and %d buffers",
           m_textures.Num(), m_buffers.Num());
    
    // Initialize all resources as unused
    for (FRDGTexture* texture : m_textures)
    {
        for (FRDGSubresourceState& state : texture->getSubresourceStates())
        {
            state.firstPass = FRDGPassHandle();
            state.lastPass = FRDGPassHandle();
        }
    }
    
    for (FRDGBuffer* buffer : m_buffers)
    {
        FRDGSubresourceState& state = buffer->getState();
        state.firstPass = FRDGPassHandle();
        state.lastPass = FRDGPassHandle();
    }
    
    // Analyze texture lifetimes based on sorted pass order
    for (int32 passIndex = 0; passIndex < m_sortedPasses.Num(); ++passIndex)
    {
        const FRDGPass* pass = m_sortedPasses[passIndex];
        const FRDGPassHandle passHandle = pass->getHandle();
        
        // Track texture usage
        for (const FRDGTextureAccess& access : pass->getTextureAccesses())
        {
            if (!access.texture)
            {
                continue;
            }
            
            FRDGTexture* texture = access.texture;
            
            // For simplicity, track all subresources together
            // In a full implementation, we would track per-mip and per-array-slice
            for (FRDGSubresourceState& state : texture->getSubresourceStates())
            {
                if (!state.firstPass.isValid())
                {
                    state.firstPass = passHandle;
                }
                state.lastPass = passHandle;
            }
        }
        
        // Track buffer usage
        for (const FRDGBufferAccess& access : pass->getBufferAccesses())
        {
            if (!access.buffer)
            {
                continue;
            }
            
            FRDGBuffer* buffer = access.buffer;
            FRDGSubresourceState& state = buffer->getState();
            
            if (!state.firstPass.isValid())
            {
                state.firstPass = passHandle;
            }
            state.lastPass = passHandle;
        }
    }
    
    // Log resource lifetime statistics
#if RDG_ENABLE_DEBUG
    int32 transientTextures = 0;
    int32 persistentTextures = 0;
    
    for (const FRDGTexture* texture : m_textures)
    {
        const FRDGSubresourceState& state = texture->getSubresourceStates()[0];
        if (state.firstPass.isValid() && state.lastPass.isValid())
        {
            const int32 lifetime = state.lastPass.index - state.firstPass.index;
            if (lifetime < m_sortedPasses.Num() / 2)
            {
                transientTextures++;
            }
            else
            {
                persistentTextures++;
            }
            
            MR_LOG(LogRDG, VeryVerbose, "Texture '%s': first pass %d, last pass %d, lifetime %d passes",
                   *texture->getName(), state.firstPass.index, state.lastPass.index, lifetime);
        }
    }
    
    MR_LOG(LogRDG, Verbose, "Resource lifetime analysis: %d transient textures, %d persistent textures",
           transientTextures, persistentTextures);
#endif
}

void FRDGBuilder::_insertTransitions()
{
    MR_LOG(LogRDG, Verbose, "Inserting resource transitions between passes");
    
    // Clear previous transition data
    m_passTransitions.Empty();
    
    // Track current state of each resource
    TMap<FRDGTexture*, ERHIAccess> currentTextureStates;
    TMap<FRDGBuffer*, ERHIAccess> currentBufferStates;
    
    // Initialize external resources with their initial states
    for (FRDGTexture* texture : m_textures)
    {
        if (texture->hasRHI())
        {
            // External texture - use its initial state
            const FRDGSubresourceState& state = texture->getSubresourceStates()[0];
            currentTextureStates.Add(texture, state.access);
        }
        else
        {
            // RDG-created texture - starts in Unknown state
            currentTextureStates.Add(texture, ERHIAccess::Unknown);
        }
    }
    
    for (FRDGBuffer* buffer : m_buffers)
    {
        if (buffer->hasRHI())
        {
            // External buffer - use its initial state
            const FRDGSubresourceState& state = buffer->getState();
            currentBufferStates.Add(buffer, state.access);
        }
        else
        {
            // RDG-created buffer - starts in Unknown state
            currentBufferStates.Add(buffer, ERHIAccess::Unknown);
        }
    }
    
    // Process passes in sorted order and insert transitions
    for (const FRDGPass* pass : m_sortedPasses)
    {
        TArray<FRDGTransition> passTransitions;
        
        // Check texture state transitions
        for (const FRDGTextureAccess& access : pass->getTextureAccesses())
        {
            if (!access.texture)
            {
                continue;
            }
            
            FRDGTexture* texture = access.texture;
            ERHIAccess currentState = currentTextureStates[texture];
            ERHIAccess requiredState = access.access;
            
            // Insert transition if state change is needed
            if (currentState != requiredState && requiredState != ERHIAccess::Unknown)
            {
                FRDGTransition transition;
                transition.resource = texture;
                transition.stateBefore = currentState;
                transition.stateAfter = requiredState;
                transition.bIsTexture = true;
                
                passTransitions.Add(transition);
                
                MR_LOG(LogRDG, VeryVerbose, "Transition for texture '%s' before pass '%s': %d -> %d",
                       *texture->getName(), *pass->getName(), 
                       static_cast<int32>(currentState), static_cast<int32>(requiredState));
            }
            
            // Update current state
            // If this is a write access, the resource will be in this state after the pass
            if (isWritableAccess(requiredState))
            {
                currentTextureStates[texture] = requiredState;
            }
        }
        
        // Check buffer state transitions
        for (const FRDGBufferAccess& access : pass->getBufferAccesses())
        {
            if (!access.buffer)
            {
                continue;
            }
            
            FRDGBuffer* buffer = access.buffer;
            ERHIAccess currentState = currentBufferStates[buffer];
            ERHIAccess requiredState = access.access;
            
            // Insert transition if state change is needed
            if (currentState != requiredState && requiredState != ERHIAccess::Unknown)
            {
                FRDGTransition transition;
                transition.resource = buffer;
                transition.stateBefore = currentState;
                transition.stateAfter = requiredState;
                transition.bIsTexture = false;
                
                passTransitions.Add(transition);
                
                MR_LOG(LogRDG, VeryVerbose, "Transition for buffer '%s' before pass '%s': %d -> %d",
                       *buffer->getName(), *pass->getName(),
                       static_cast<int32>(currentState), static_cast<int32>(requiredState));
            }
            
            // Update current state
            if (isWritableAccess(requiredState))
            {
                currentBufferStates[buffer] = requiredState;
            }
        }
        
        // Store transitions for this pass
        if (passTransitions.Num() > 0)
        {
            m_passTransitions.Add(pass->getHandle(), passTransitions);
        }
    }
    
    // Log transition statistics
    int32 totalTransitions = 0;
    for (const auto& pair : m_passTransitions)
    {
        totalTransitions += pair.Value.Num();
    }
    
    MR_LOG(LogRDG, Verbose, "Inserted %d resource transitions across %d passes",
           totalTransitions, m_passTransitions.Num());
}

void FRDGBuilder::_allocateResources(RHI::IRHICommandList& rhiCmdList)
{
    MR_LOG(LogRDG, Verbose, "Allocating RHI resources for %d textures and %d buffers",
           m_textures.Num(), m_buffers.Num());
    
    int32 allocatedTextures = 0;
    int32 allocatedBuffers = 0;
    
    // Allocate textures
    for (FRDGTexture* texture : m_textures)
    {
        if (!texture)
        {
            continue;
        }
        
        // Skip external resources that already have RHI resources
        if (texture->hasRHI())
        {
            MR_LOG(LogRDG, VeryVerbose, "Texture '%s' already has RHI resource (external)", 
                   *texture->getName());
            continue;
        }
        
        // Check if this texture is actually used
        const FRDGSubresourceState& state = texture->getSubresourceStates()[0];
        if (!state.firstPass.isValid())
        {
            MR_LOG(LogRDG, Warning, "Texture '%s' is not used by any pass, skipping allocation",
                   *texture->getName());
            continue;
        }
        
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
            // Store shared pointer for lifetime management (will be added to FRDGTexture later)
            allocatedTextures++;
            
            MR_LOG(LogRDG, Verbose, "Allocated RHI texture: %s (%dx%d, format %d)",
                   *texture->getName(), desc.width, desc.height, static_cast<int32>(desc.format));
        }
        else
        {
            MR_LOG(LogRDG, Error, "Failed to allocate RHI texture: %s", *texture->getName());
        }
    }
    
    // Allocate buffers
    for (FRDGBuffer* buffer : m_buffers)
    {
        if (!buffer)
        {
            continue;
        }
        
        // Skip external resources that already have RHI resources
        if (buffer->hasRHI())
        {
            MR_LOG(LogRDG, VeryVerbose, "Buffer '%s' already has RHI resource (external)",
                   *buffer->getName());
            continue;
        }
        
        // Check if this buffer is actually used
        const FRDGSubresourceState& state = buffer->getState();
        if (!state.firstPass.isValid())
        {
            MR_LOG(LogRDG, Warning, "Buffer '%s' is not used by any pass, skipping allocation",
                   *buffer->getName());
            continue;
        }
        
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
            // Store shared pointer for lifetime management (will be added to FRDGBuffer later)
            allocatedBuffers++;
            
            MR_LOG(LogRDG, Verbose, "Allocated RHI buffer: %s (size %llu bytes)",
                   *buffer->getName(), desc.size);
        }
        else
        {
            MR_LOG(LogRDG, Error, "Failed to allocate RHI buffer: %s", *buffer->getName());
        }
    }
    
    MR_LOG(LogRDG, Log, "Resource allocation complete: %d textures, %d buffers allocated",
           allocatedTextures, allocatedBuffers);
}

void FRDGBuilder::_executePass(RHI::IRHICommandList& rhiCmdList, FRDGPass* pass)
{
    if (!pass)
    {
        MR_LOG(LogRDG, Error, "Attempted to execute null pass");
        return;
    }
    
    MR_LOG(LogRDG, Verbose, "Executing pass: %s (flags: %d)", 
           *pass->getName(), static_cast<int32>(pass->getFlags()));
    
    // Execute pre-pass transitions
    auto transitionsIt = m_passTransitions.Find(pass->getHandle());
    if (transitionsIt)
    {
        MR_LOG(LogRDG, VeryVerbose, "Executing %d transitions before pass '%s'",
               transitionsIt->Num(), *pass->getName());
        _executeTransitions(rhiCmdList, *transitionsIt);
    }
    
    // Begin debug event
    String passName(pass->getName().begin(), pass->getName().end());
    rhiCmdList.beginEvent(passName);
    
    // Setup render targets for raster passes
    const bool bIsRasterPass = enumHasAnyFlags(pass->getFlags(), ERDGPassFlags::Raster);
    if (bIsRasterPass)
    {
        _setupRenderTargets(rhiCmdList, pass);
    }
    
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
    
    // Execute the pass lambda
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
    
    // End render pass for raster passes
    if (bIsRasterPass)
    {
        rhiCmdList.endRenderPass();
        MR_LOG(LogRDG, VeryVerbose, "Ended render pass for '%s'", *pass->getName());
    }
    
    // End debug event
    rhiCmdList.endEvent();
    
    MR_LOG(LogRDG, VeryVerbose, "Pass '%s' execution complete", *pass->getName());
}

void FRDGBuilder::_executeTransitions(RHI::IRHICommandList& rhiCmdList, 
                                     const TArray<FRDGTransition>& transitions)
{
    if (transitions.Num() == 0)
    {
        return;
    }
    
    MR_LOG(LogRDG, VeryVerbose, "Executing %d resource transitions", transitions.Num());
    
    // Execute resource barriers
    for (const FRDGTransition& transition : transitions)
    {
        if (!transition.resource)
        {
            MR_LOG(LogRDG, Warning, "Null resource in transition");
            continue;
        }
        
        RHI::IRHIResource* rhiResource = transition.resource->getRHI();
        if (!rhiResource)
        {
            MR_LOG(LogRDG, Warning, "Resource '%s' has no RHI resource for transition",
                   *transition.resource->getName());
            continue;
        }
        
        MR_LOG(LogRDG, VeryVerbose, "Transition: %s (state %d -> %d)",
               *transition.resource->getName(),
               static_cast<int32>(transition.stateBefore),
               static_cast<int32>(transition.stateAfter));
        
        // Call RHI transition - use non-owning shared pointer
        rhiCmdList.transitionResource(
            TSharedPtr<RHI::IRHIResource>(rhiResource, [](RHI::IRHIResource*){}),
            static_cast<RHI::EResourceUsage>(transition.stateBefore),
            static_cast<RHI::EResourceUsage>(transition.stateAfter));
    }
}

void FRDGBuilder::_setupRenderTargets(RHI::IRHICommandList& rhiCmdList, FRDGPass* pass)
{
    if (!pass)
    {
        return;
    }
    
    MR_LOG(LogRDG, Log, "Setting up render targets for pass '%ls'", *pass->getName());
    
    // Collect render targets from pass texture accesses
    TArray<TSharedPtr<RHI::IRHITexture>> colorTargets;
    TSharedPtr<RHI::IRHITexture> depthTarget;
    
    MR_LOG(LogRDG, Log, "  Pass has %d texture accesses", pass->getTextureAccesses().Num());
    
    for (const FRDGTextureAccess& access : pass->getTextureAccesses())
    {
        if (!access.texture)
        {
            MR_LOG(LogRDG, Warning, "  Null texture in access");
            continue;
        }
        
        MR_LOG(LogRDG, Log, "  Checking texture '%ls' with access %d", 
               *access.texture->getName(), static_cast<int32>(access.access));
        
        if (!access.texture->hasRHI())
        {
            MR_LOG(LogRDG, Warning, "  Texture '%ls' has no RHI resource", *access.texture->getName());
            continue;
        }
        
        RHI::IRHITexture* rhiTexture = static_cast<RHI::IRHITexture*>(access.texture->getRHI());
        if (!rhiTexture)
        {
            MR_LOG(LogRDG, Warning, "  Failed to get RHI texture for '%ls'", *access.texture->getName());
            continue;
        }
        
        // Check if this is a render target write
        if (access.access == ERHIAccess::RTV)
        {
            // Color render target
            TSharedPtr<RHI::IRHITexture> target(rhiTexture, [](RHI::IRHITexture*){});
            colorTargets.Add(target);
            
            MR_LOG(LogRDG, Log, "  Added Color RT: %ls", *access.texture->getName());
        }
        else if (access.access == ERHIAccess::DSVWrite || access.access == ERHIAccess::DSVRead)
        {
            // Depth/stencil target
            depthTarget = TSharedPtr<RHI::IRHITexture>(rhiTexture, [](RHI::IRHITexture*){});
            
            MR_LOG(LogRDG, Log, "  Added Depth RT: %ls", *access.texture->getName());
        }
    }
    
    // Set render targets if any were found
    // If no explicit render targets are declared, assume swapchain rendering (empty array)
    // This matches UE5 behavior where passes without explicit RT declarations render to backbuffer
    MR_LOG(LogRDG, Log, "Calling setRenderTargets with %d color and %d depth targets",
           colorTargets.Num(), depthTarget ? 1 : 0);
    
    rhiCmdList.setRenderTargets(
        TSpan<TSharedPtr<RHI::IRHITexture>>(colorTargets.GetData(), colorTargets.Num()),
        depthTarget
    );
    
    if (colorTargets.Num() == 0 && !depthTarget)
    {
        MR_LOG(LogRDG, Log, "No explicit render targets - using swapchain rendering for pass '%ls'",
               *pass->getName());
    }
    else
    {
        MR_LOG(LogRDG, Log, "setRenderTargets completed for pass '%ls'", *pass->getName());
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
