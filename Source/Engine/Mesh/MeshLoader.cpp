// Copyright Monster Engine. All Rights Reserved.

/**
 * @file MeshLoader.cpp
 * @brief Implementation of mesh loader base class and registry
 */

#include "Engine/Mesh/MeshLoader.h"
#include "Engine/Mesh/MeshBuilder.h"
#include "Engine/Mesh/StaticMesh.h"
#include "RHI/IRHIDevice.h"
#include "Core/Logging/LogMacros.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace MonsterEngine
{

// Use MonsterRender::RHI types
using MonsterRender::RHI::IRHIDevice;

// Define log category for mesh loading operations
DEFINE_LOG_CATEGORY_STATIC(LogMeshLoader, Log, All);

// ============================================================================
// IMeshLoader Implementation
// ============================================================================

/**
 * Check if this loader can handle a specific file
 */
bool IMeshLoader::CanLoad(const String& FilePath) const
{
    String Ext = GetExtension(FilePath);
    TArray<String> Supported = GetSupportedExtensions();
    
    for (const String& SupportedExt : Supported)
    {
        if (Ext == SupportedExt)
        {
            return true;
        }
    }
    
    return false;
}

/**
 * Load a mesh file from memory (default implementation)
 */
EMeshLoadResult IMeshLoader::LoadFromMemory(
    const void* Data,
    size_t DataSize,
    FMeshBuilder& OutBuilder,
    const FMeshLoadOptions& Options)
{
    // Default implementation - not supported
    MR_LOG(LogMeshLoader, Warning, "LoadFromMemory not implemented for loader: %s", GetName());
    return EMeshLoadResult::UnsupportedFormat;
}

/**
 * Get file extension from path
 */
String IMeshLoader::GetExtension(const String& FilePath)
{
    size_t DotPos = FilePath.rfind('.');
    if (DotPos == String::npos || DotPos == FilePath.length() - 1)
    {
        return "";
    }
    
    String Ext = FilePath.substr(DotPos + 1);
    
    // Convert to lowercase
    std::transform(Ext.begin(), Ext.end(), Ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    
    return Ext;
}

/**
 * Read entire file into memory
 */
bool IMeshLoader::ReadFile(const String& FilePath, TArray<uint8>& OutData)
{
    std::ifstream File(FilePath, std::ios::binary | std::ios::ate);
    if (!File.is_open())
    {
        MR_LOG(LogMeshLoader, Error, "Failed to open file: %s", FilePath.c_str());
        return false;
    }
    
    std::streamsize Size = File.tellg();
    File.seekg(0, std::ios::beg);
    
    OutData.SetNum(static_cast<int32>(Size));
    
    if (!File.read(reinterpret_cast<char*>(OutData.GetData()), Size))
    {
        MR_LOG(LogMeshLoader, Error, "Failed to read file: %s", FilePath.c_str());
        return false;
    }
    
    return true;
}

/**
 * Read file as text
 */
bool IMeshLoader::ReadTextFile(const String& FilePath, String& OutText)
{
    std::ifstream File(FilePath);
    if (!File.is_open())
    {
        MR_LOG(LogMeshLoader, Error, "Failed to open text file: %s", FilePath.c_str());
        return false;
    }
    
    std::stringstream Buffer;
    Buffer << File.rdbuf();
    OutText = Buffer.str();
    
    return true;
}

// ============================================================================
// FMeshLoaderRegistry Implementation
// ============================================================================

/**
 * Get the singleton instance
 */
FMeshLoaderRegistry& FMeshLoaderRegistry::Get()
{
    static FMeshLoaderRegistry Instance;
    return Instance;
}

/**
 * Private constructor
 */
FMeshLoaderRegistry::FMeshLoaderRegistry()
    : bDefaultLoadersInitialized(false)
{
}

/**
 * Destructor
 */
FMeshLoaderRegistry::~FMeshLoaderRegistry()
{
    Loaders.Empty();
    ExtensionMap.Empty();
}

/**
 * Register a mesh loader (unique ownership)
 */
void FMeshLoaderRegistry::RegisterLoader(TUniquePtr<IMeshLoader> Loader)
{
    if (Loader)
    {
        RegisterLoader(TSharedPtr<IMeshLoader>(Loader.Release()));
    }
}

/**
 * Register a mesh loader (shared ownership)
 */
void FMeshLoaderRegistry::RegisterLoader(TSharedPtr<IMeshLoader> Loader)
{
    if (!Loader)
    {
        return;
    }
    
    MR_LOG(LogMeshLoader, Log, "Registering mesh loader: %s", Loader->GetName());
    
    Loaders.Add(Loader);
    RebuildExtensionMap();
}

/**
 * Unregister a loader by name
 */
bool FMeshLoaderRegistry::UnregisterLoader(const String& LoaderName)
{
    for (int32 i = 0; i < Loaders.Num(); ++i)
    {
        if (Loaders[i] && Loaders[i]->GetName() == LoaderName)
        {
            MR_LOG(LogMeshLoader, Log, "Unregistering mesh loader: %s", LoaderName.c_str());
            Loaders.RemoveAt(i);
            RebuildExtensionMap();
            return true;
        }
    }
    
    return false;
}

/**
 * Find a loader for a specific file
 */
IMeshLoader* FMeshLoaderRegistry::FindLoader(const String& FilePath) const
{
    String Ext = IMeshLoader::GetExtension(FilePath);
    return FindLoaderByExtension(Ext);
}

/**
 * Find a loader by extension
 */
IMeshLoader* FMeshLoaderRegistry::FindLoaderByExtension(const String& Extension) const
{
    // Ensure default loaders are initialized
    const_cast<FMeshLoaderRegistry*>(this)->InitializeDefaultLoaders();
    
    String NormExt = NormalizeExtension(Extension);
    
    const IMeshLoader* const* Found = ExtensionMap.Find(NormExt);
    if (Found)
    {
        return const_cast<IMeshLoader*>(*Found);
    }
    
    return nullptr;
}

/**
 * Check if a file format is supported
 */
bool FMeshLoaderRegistry::IsFormatSupported(const String& FilePath) const
{
    return FindLoader(FilePath) != nullptr;
}

/**
 * Get all supported extensions
 */
TArray<String> FMeshLoaderRegistry::GetSupportedExtensions() const
{
    // Ensure default loaders are initialized
    const_cast<FMeshLoaderRegistry*>(this)->InitializeDefaultLoaders();
    
    TArray<String> Extensions;
    
    for (const auto& Pair : ExtensionMap)
    {
        Extensions.Add(Pair.Key);
    }
    
    return Extensions;
}

/**
 * Load a mesh file
 */
EMeshLoadResult FMeshLoaderRegistry::LoadMesh(
    const String& FilePath,
    FMeshBuilder& OutBuilder,
    const FMeshLoadOptions& Options) const
{
    // Find appropriate loader
    IMeshLoader* Loader = FindLoader(FilePath);
    if (!Loader)
    {
        MR_LOG(LogMeshLoader, Error, "No loader found for file: %s", FilePath.c_str());
        return EMeshLoadResult::UnsupportedFormat;
    }
    
    MR_LOG(LogMeshLoader, Log, "Loading mesh '%s' with loader '%s'", 
           FilePath.c_str(), Loader->GetName());
    
    // Load the mesh
    EMeshLoadResult Result = Loader->Load(FilePath, OutBuilder, Options);
    
    if (Result == EMeshLoadResult::Success)
    {
        MR_LOG(LogMeshLoader, Log, "Successfully loaded mesh: %d vertices, %d triangles",
               OutBuilder.GetNumVertices(), OutBuilder.GetNumTriangles());
    }
    else
    {
        MR_LOG(LogMeshLoader, Error, "Failed to load mesh '%s': %s",
               FilePath.c_str(), MeshLoadResultToString(Result));
    }
    
    return Result;
}

/**
 * Load a mesh file and build directly to FStaticMesh
 */
FStaticMesh* FMeshLoaderRegistry::LoadStaticMesh(
    const String& FilePath,
    IRHIDevice* Device,
    const FMeshLoadOptions& Options) const
{
    FMeshBuilder Builder;
    
    // Configure builder from options
    Builder.GetSettings().bComputeNormals = Options.bComputeNormals;
    Builder.GetSettings().bComputeTangents = Options.bComputeTangents;
    Builder.GetSettings().bUseSmoothNormals = Options.bUseSmoothNormals;
    
    // Load mesh data
    EMeshLoadResult Result = LoadMesh(FilePath, Builder, Options);
    if (Result != EMeshLoadResult::Success)
    {
        return nullptr;
    }
    
    // Extract mesh name from file path
    size_t SlashPos = FilePath.rfind('/');
    if (SlashPos == String::npos)
    {
        SlashPos = FilePath.rfind('\\');
    }
    
    String MeshName;
    if (SlashPos != String::npos)
    {
        MeshName = FilePath.substr(SlashPos + 1);
    }
    else
    {
        MeshName = FilePath;
    }
    
    // Remove extension
    size_t DotPos = MeshName.rfind('.');
    if (DotPos != String::npos)
    {
        MeshName = MeshName.substr(0, DotPos);
    }
    
    // Build the mesh
    FStaticMesh* Mesh = Builder.Build(Device, MeshName);
    if (Mesh)
    {
        Mesh->SetSourceFilePath(FilePath);
    }
    
    return Mesh;
}

/**
 * Initialize default loaders
 */
void FMeshLoaderRegistry::InitializeDefaultLoaders()
{
    if (bDefaultLoadersInitialized)
    {
        return;
    }
    
    bDefaultLoadersInitialized = true;
    
    MR_LOG(LogMeshLoader, Log, "Initializing default mesh loaders");
    
    // Register OBJ loader
    RegisterLoader(MakeShared<FOBJMeshLoader>());
    
    // Register glTF loader
    RegisterLoader(MakeShared<FGLTFMeshLoader>());
    
    MR_LOG(LogMeshLoader, Log, "Registered %d default mesh loaders", Loaders.Num());
}

/**
 * Rebuild extension map
 */
void FMeshLoaderRegistry::RebuildExtensionMap()
{
    ExtensionMap.Empty();
    
    for (const TSharedPtr<IMeshLoader>& Loader : Loaders)
    {
        if (!Loader)
        {
            continue;
        }
        
        TArray<String> Extensions = Loader->GetSupportedExtensions();
        for (const String& Ext : Extensions)
        {
            String NormExt = NormalizeExtension(Ext);
            ExtensionMap.Add(NormExt, Loader.Get());
        }
    }
}

/**
 * Normalize extension to lowercase
 */
String FMeshLoaderRegistry::NormalizeExtension(const String& Extension)
{
    String Result = Extension;
    std::transform(Result.begin(), Result.end(), Result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return Result;
}

} // namespace MonsterEngine
