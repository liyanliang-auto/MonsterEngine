// Copyright Monster Engine. All Rights Reserved.

/**
 * @file GLTFLoader.cpp
 * @brief Implementation of glTF 2.0 asset loader
 */

// Disable MSVC security warnings for third-party code - must be first
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996) // 'fopen', 'strncpy', etc. unsafe
#endif

// Standard library includes first (before any engine headers)
#include <cstring>
#include <cmath>
#include <cfloat>

// cgltf implementation - must be before engine headers to avoid namespace issues
#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"

#ifdef _MSC_VER
#pragma warning(pop)
#endif

// stb_image for image loading - use existing stb_image from engine
extern "C" {
    unsigned char* stbi_load_from_memory(
        const unsigned char* buffer, int len,
        int* x, int* y, int* channels_in_file, int desired_channels);
    unsigned char* stbi_load(
        const char* filename,
        int* x, int* y, int* channels_in_file, int desired_channels);
    void stbi_image_free(void* retval_from_stbi_load);
}

// Engine headers
#include "Core/CoreMinimal.h"
#include "Engine/Asset/GLTFLoader.h"
#include "Engine/Asset/GLTFTypes.h"
#include "Core/Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogGLTFLoader, Log, All);

namespace MonsterEngine
{

// ============================================================================
// Helper Functions
// ============================================================================

const char* GLTFLoadResultToString(EGLTFLoadResult Result)
{
    switch (Result)
    {
        case EGLTFLoadResult::Success:            return "Success";
        case EGLTFLoadResult::FileNotFound:       return "File not found";
        case EGLTFLoadResult::InvalidFormat:      return "Invalid format";
        case EGLTFLoadResult::ParseError:         return "Parse error";
        case EGLTFLoadResult::UnsupportedVersion: return "Unsupported version";
        case EGLTFLoadResult::MissingBuffers:     return "Missing buffers";
        case EGLTFLoadResult::TextureLoadFailed:  return "Texture load failed";
        case EGLTFLoadResult::OutOfMemory:        return "Out of memory";
        default:                                   return "Unknown error";
    }
}

static String GetDirectoryPath(const String& FilePath)
{
    size_t LastSlash = FilePath.find_last_of("/\\");
    if (LastSlash != String::npos)
    {
        return FilePath.substr(0, LastSlash + 1);
    }
    return "";
}

static String CombinePaths(const String& BasePath, const String& RelativePath)
{
    if (BasePath.empty()) return RelativePath;
    if (RelativePath.length() > 1 && RelativePath[1] == ':') return RelativePath;
    if (RelativePath.length() > 0 && (RelativePath[0] == '/' || RelativePath[0] == '\\'))
        return RelativePath;
    return BasePath + RelativePath;
}

static uint32 GetAccessorComponentCount(cgltf_type Type)
{
    switch (Type)
    {
        case cgltf_type_scalar: return 1;
        case cgltf_type_vec2:   return 2;
        case cgltf_type_vec3:   return 3;
        case cgltf_type_vec4:   return 4;
        case cgltf_type_mat2:   return 4;
        case cgltf_type_mat3:   return 9;
        case cgltf_type_mat4:   return 16;
        default:                return 0;
    }
}

// ============================================================================
// FGLTFNode Implementation
// ============================================================================

Math::FMatrix FGLTFNode::GetLocalTransform() const
{
    if (bHasMatrix) return LocalMatrix;
    
    // Build TRS matrix manually
    // Quaternion to rotation matrix conversion
    double qx = static_cast<double>(Rotation.X);
    double qy = static_cast<double>(Rotation.Y);
    double qz = static_cast<double>(Rotation.Z);
    double qw = static_cast<double>(Rotation.W);
    
    double xx = qx * qx;
    double yy = qy * qy;
    double zz = qz * qz;
    double xy = qx * qy;
    double xz = qx * qz;
    double yz = qy * qz;
    double wx = qw * qx;
    double wy = qw * qy;
    double wz = qw * qz;
    
    Math::FMatrix Result = Math::FMatrix::Identity;
    
    // Rotation matrix from quaternion
    Result.M[0][0] = 1.0 - 2.0 * (yy + zz);
    Result.M[0][1] = 2.0 * (xy - wz);
    Result.M[0][2] = 2.0 * (xz + wy);
    
    Result.M[1][0] = 2.0 * (xy + wz);
    Result.M[1][1] = 1.0 - 2.0 * (xx + zz);
    Result.M[1][2] = 2.0 * (yz - wx);
    
    Result.M[2][0] = 2.0 * (xz - wy);
    Result.M[2][1] = 2.0 * (yz + wx);
    Result.M[2][2] = 1.0 - 2.0 * (xx + yy);
    
    // Apply scale
    Result.M[0][0] *= static_cast<double>(Scale.X);
    Result.M[0][1] *= static_cast<double>(Scale.X);
    Result.M[0][2] *= static_cast<double>(Scale.X);
    
    Result.M[1][0] *= static_cast<double>(Scale.Y);
    Result.M[1][1] *= static_cast<double>(Scale.Y);
    Result.M[1][2] *= static_cast<double>(Scale.Y);
    
    Result.M[2][0] *= static_cast<double>(Scale.Z);
    Result.M[2][1] *= static_cast<double>(Scale.Z);
    Result.M[2][2] *= static_cast<double>(Scale.Z);
    
    // Apply translation
    Result.M[3][0] = static_cast<double>(Translation.X);
    Result.M[3][1] = static_cast<double>(Translation.Y);
    Result.M[3][2] = static_cast<double>(Translation.Z);
    
    return Result;
}

// ============================================================================
// FGLTFLoader Implementation
// ============================================================================

FGLTFLoader::FGLTFLoader()
    : m_lastError(EGLTFLoadResult::Success)
{
}

FGLTFLoader::~FGLTFLoader()
{
}

TSharedPtr<FGLTFModel> FGLTFLoader::LoadFromFile(
    const String& FilePath,
    const FGLTFLoadOptions& Options)
{
    MR_LOG(LogGLTFLoader, Log, "Loading glTF file: %s", FilePath.c_str());
    
    cgltf_options cgltfOptions = {};
    cgltf_data* data = nullptr;
    
    cgltf_result result = cgltf_parse_file(&cgltfOptions, FilePath.c_str(), &data);
    if (result != cgltf_result_success)
    {
        _setError(EGLTFLoadResult::ParseError, "Failed to parse glTF file");
        return nullptr;
    }
    
    result = cgltf_load_buffers(&cgltfOptions, data, FilePath.c_str());
    if (result != cgltf_result_success)
    {
        _setError(EGLTFLoadResult::MissingBuffers, "Failed to load glTF buffers");
        cgltf_free(data);
        return nullptr;
    }
    
    String basePath = GetDirectoryPath(FilePath);
    TSharedPtr<FGLTFModel> model = MakeShared<FGLTFModel>();
    model->SourcePath = FilePath;
    
    if (!_parseGLTFData(data, basePath, Options, *model))
    {
        cgltf_free(data);
        return nullptr;
    }
    
    cgltf_free(data);
    
    MR_LOG(LogGLTFLoader, Log, "Loaded glTF: %d meshes, %d materials, %u vertices",
           model->GetMeshCount(), model->GetMaterialCount(), model->GetTotalVertexCount());
    
    m_lastError = EGLTFLoadResult::Success;
    return model;
}

TSharedPtr<FGLTFModel> FGLTFLoader::LoadFromMemory(
    const void* Data,
    uint64 DataSize,
    const String& BasePath,
    const FGLTFLoadOptions& Options)
{
    cgltf_options cgltfOptions = {};
    cgltf_data* cgltfData = nullptr;
    
    cgltf_result result = cgltf_parse(&cgltfOptions, Data, static_cast<cgltf_size>(DataSize), &cgltfData);
    if (result != cgltf_result_success)
    {
        _setError(EGLTFLoadResult::ParseError, "Failed to parse glTF data");
        return nullptr;
    }
    
    TSharedPtr<FGLTFModel> model = MakeShared<FGLTFModel>();
    model->SourcePath = BasePath;
    
    if (!_parseGLTFData(cgltfData, BasePath, Options, *model))
    {
        cgltf_free(cgltfData);
        return nullptr;
    }
    
    cgltf_free(cgltfData);
    m_lastError = EGLTFLoadResult::Success;
    return model;
}

bool FGLTFLoader::IsValidGLTFFile(const String& FilePath)
{
    return HasGLTFExtension(FilePath);
}

bool FGLTFLoader::HasGLTFExtension(const String& FilePath)
{
    size_t dotPos = FilePath.find_last_of('.');
    if (dotPos == String::npos) return false;
    
    String ext = FilePath.substr(dotPos);
    for (char& c : ext)
    {
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    }
    return ext == ".gltf" || ext == ".glb";
}

bool FGLTFLoader::_parseGLTFData(
    cgltf_data* Data,
    const String& BasePath,
    const FGLTFLoadOptions& Options,
    FGLTFModel& OutModel)
{
    if (Data->asset.version) OutModel.Version = Data->asset.version;
    if (Data->asset.generator) OutModel.Generator = Data->asset.generator;
    
    _parseTextures(Data, BasePath, Options, OutModel);
    _parseMaterials(Data, OutModel);
    
    if (!_parseMeshes(Data, Options, OutModel))
    {
        _setError(EGLTFLoadResult::ParseError, "Failed to parse meshes");
        return false;
    }
    
    _parseNodes(Data, OutModel);
    
    if (Options.bComputeBounds) _computeBounds(OutModel);
    
    return true;
}

bool FGLTFLoader::_parseMeshes(
    cgltf_data* Data,
    const FGLTFLoadOptions& Options,
    FGLTFModel& OutModel)
{
    OutModel.Meshes.SetNum(static_cast<int32>(Data->meshes_count));
    
    for (cgltf_size i = 0; i < Data->meshes_count; ++i)
    {
        cgltf_mesh* srcMesh = &Data->meshes[i];
        FGLTFMesh& dstMesh = OutModel.Meshes[static_cast<int32>(i)];
        
        dstMesh.Name = srcMesh->name ? srcMesh->name : ("Mesh_" + std::to_string(i));
        dstMesh.Primitives.SetNum(static_cast<int32>(srcMesh->primitives_count));
        
        for (cgltf_size j = 0; j < srcMesh->primitives_count; ++j)
        {
            cgltf_primitive* srcPrim = &srcMesh->primitives[j];
            FGLTFPrimitive& dstPrim = dstMesh.Primitives[static_cast<int32>(j)];
            
            _parsePrimitive(srcPrim, Options, dstPrim);
            
            if (srcPrim->material)
                dstPrim.MaterialIndex = static_cast<int32>(srcPrim->material - Data->materials);
        }
    }
    
    return OutModel.Meshes.Num() > 0;
}

bool FGLTFLoader::_parsePrimitive(
    cgltf_primitive* Primitive,
    const FGLTFLoadOptions& Options,
    FGLTFPrimitive& OutPrimitive)
{
    OutPrimitive.Mode = static_cast<EGLTFPrimitiveMode>(Primitive->type);
    
    for (cgltf_size i = 0; i < Primitive->attributes_count; ++i)
    {
        cgltf_attribute* attr = &Primitive->attributes[i];
        cgltf_accessor* accessor = attr->data;
        if (!accessor || accessor->count == 0) continue;
        
        uint32 count = static_cast<uint32>(accessor->count);
        
        switch (attr->type)
        {
            case cgltf_attribute_type_position:
            {
                OutPrimitive.Positions.SetNum(count);
                for (uint32 v = 0; v < count; ++v)
                {
                    float values[3] = {0.0f, 0.0f, 0.0f};
                    cgltf_accessor_read_float(accessor, v, values, 3);
                    OutPrimitive.Positions[v] = Math::FVector3f(values[0], values[1], values[2]);
                }
                if (accessor->has_min && accessor->has_max)
                {
                    OutPrimitive.BoundsMin = Math::FVector3f(
                        static_cast<float>(accessor->min[0]),
                        static_cast<float>(accessor->min[1]),
                        static_cast<float>(accessor->min[2]));
                    OutPrimitive.BoundsMax = Math::FVector3f(
                        static_cast<float>(accessor->max[0]),
                        static_cast<float>(accessor->max[1]),
                        static_cast<float>(accessor->max[2]));
                }
                break;
            }
            case cgltf_attribute_type_normal:
            {
                OutPrimitive.Normals.SetNum(count);
                for (uint32 v = 0; v < count; ++v)
                {
                    float values[3] = {0.0f, 0.0f, 1.0f};
                    cgltf_accessor_read_float(accessor, v, values, 3);
                    OutPrimitive.Normals[v] = Math::FVector3f(values[0], values[1], values[2]);
                }
                break;
            }
            case cgltf_attribute_type_tangent:
            {
                OutPrimitive.Tangents.SetNum(count);
                for (uint32 v = 0; v < count; ++v)
                {
                    float values[4] = {1.0f, 0.0f, 0.0f, 1.0f};
                    cgltf_accessor_read_float(accessor, v, values, 4);
                    OutPrimitive.Tangents[v] = Math::FVector4f(values[0], values[1], values[2], values[3]);
                }
                break;
            }
            case cgltf_attribute_type_texcoord:
            {
                TArray<Math::FVector2f>* uvArray = (attr->index == 0) ? &OutPrimitive.TexCoords0 : 
                                                   (attr->index == 1) ? &OutPrimitive.TexCoords1 : nullptr;
                if (uvArray)
                {
                    uvArray->SetNum(count);
                    for (uint32 v = 0; v < count; ++v)
                    {
                        float values[2] = {0.0f, 0.0f};
                        cgltf_accessor_read_float(accessor, v, values, 2);
                        if (Options.bFlipUVs) values[1] = 1.0f - values[1];
                        (*uvArray)[v] = Math::FVector2f(values[0], values[1]);
                    }
                }
                break;
            }
            default: break;
        }
    }
    
    // Parse indices
    if (Primitive->indices)
    {
        uint32 indexCount = static_cast<uint32>(Primitive->indices->count);
        OutPrimitive.Indices.SetNum(indexCount);
        for (uint32 i = 0; i < indexCount; ++i)
            OutPrimitive.Indices[i] = static_cast<uint32>(cgltf_accessor_read_index(Primitive->indices, i));
    }
    
    if (Options.bGenerateNormals && !OutPrimitive.HasNormals() && OutPrimitive.IsValid())
        _generateNormals(OutPrimitive);
    
    if (Options.bGenerateTangents && !OutPrimitive.HasTangents() && 
        OutPrimitive.HasNormals() && OutPrimitive.HasTexCoords())
        _generateTangents(OutPrimitive);
    
    return OutPrimitive.IsValid();
}

bool FGLTFLoader::_parseMaterials(cgltf_data* Data, FGLTFModel& OutModel)
{
    OutModel.Materials.SetNum(static_cast<int32>(Data->materials_count));
    
    for (cgltf_size i = 0; i < Data->materials_count; ++i)
    {
        cgltf_material* srcMat = &Data->materials[i];
        FGLTFMaterial& dstMat = OutModel.Materials[static_cast<int32>(i)];
        
        dstMat.Name = srcMat->name ? srcMat->name : ("Material_" + std::to_string(i));
        
        if (srcMat->has_pbr_metallic_roughness)
        {
            cgltf_pbr_metallic_roughness* pbr = &srcMat->pbr_metallic_roughness;
            dstMat.BaseColorFactor = Math::FVector4f(
                pbr->base_color_factor[0], pbr->base_color_factor[1],
                pbr->base_color_factor[2], pbr->base_color_factor[3]);
            dstMat.MetallicFactor = pbr->metallic_factor;
            dstMat.RoughnessFactor = pbr->roughness_factor;
            
            if (pbr->base_color_texture.texture)
            {
                dstMat.BaseColorTexture.TextureIndex = 
                    static_cast<int32>(pbr->base_color_texture.texture - Data->textures);
            }
            if (pbr->metallic_roughness_texture.texture)
            {
                dstMat.MetallicRoughnessTexture.TextureIndex = 
                    static_cast<int32>(pbr->metallic_roughness_texture.texture - Data->textures);
            }
        }
        
        if (srcMat->normal_texture.texture)
        {
            dstMat.NormalTexture.TextureIndex = 
                static_cast<int32>(srcMat->normal_texture.texture - Data->textures);
            dstMat.NormalTexture.Scale = srcMat->normal_texture.scale;
        }
        
        if (srcMat->occlusion_texture.texture)
        {
            dstMat.OcclusionTexture.TextureIndex = 
                static_cast<int32>(srcMat->occlusion_texture.texture - Data->textures);
            dstMat.OcclusionTexture.Strength = srcMat->occlusion_texture.scale;
        }
        
        if (srcMat->emissive_texture.texture)
        {
            dstMat.EmissiveTexture.TextureIndex = 
                static_cast<int32>(srcMat->emissive_texture.texture - Data->textures);
        }
        
        dstMat.EmissiveFactor = Math::FVector3f(
            srcMat->emissive_factor[0], srcMat->emissive_factor[1], srcMat->emissive_factor[2]);
        
        dstMat.AlphaMode = static_cast<EGLTFAlphaMode>(srcMat->alpha_mode);
        dstMat.AlphaCutoff = srcMat->alpha_cutoff;
        dstMat.bDoubleSided = srcMat->double_sided;
    }
    
    return true;
}

bool FGLTFLoader::_parseTextures(
    cgltf_data* Data,
    const String& BasePath,
    const FGLTFLoadOptions& Options,
    FGLTFModel& OutModel)
{
    OutModel.Samplers.SetNum(static_cast<int32>(Data->samplers_count));
    OutModel.Images.SetNum(static_cast<int32>(Data->images_count));
    OutModel.Textures.SetNum(static_cast<int32>(Data->textures_count));
    
    for (cgltf_size i = 0; i < Data->images_count; ++i)
    {
        cgltf_image* srcImage = &Data->images[i];
        FGLTFImage& dstImage = OutModel.Images[static_cast<int32>(i)];
        
        dstImage.Name = srcImage->name ? srcImage->name : ("Image_" + std::to_string(i));
        if (srcImage->uri) dstImage.URI = srcImage->uri;
        
        if (Options.bLoadTextures)
            _loadImageData(srcImage, BasePath, Options, dstImage);
    }
    
    for (cgltf_size i = 0; i < Data->textures_count; ++i)
    {
        cgltf_texture* srcTex = &Data->textures[i];
        FGLTFTexture& dstTex = OutModel.Textures[static_cast<int32>(i)];
        
        if (srcTex->image)
            dstTex.ImageIndex = static_cast<int32>(srcTex->image - Data->images);
        if (srcTex->sampler)
            dstTex.SamplerIndex = static_cast<int32>(srcTex->sampler - Data->samplers);
    }
    
    return true;
}

bool FGLTFLoader::_parseNodes(cgltf_data* Data, FGLTFModel& OutModel)
{
    OutModel.Nodes.SetNum(static_cast<int32>(Data->nodes_count));
    
    for (cgltf_size i = 0; i < Data->nodes_count; ++i)
    {
        cgltf_node* srcNode = &Data->nodes[i];
        FGLTFNode& dstNode = OutModel.Nodes[static_cast<int32>(i)];
        
        dstNode.Name = srcNode->name ? srcNode->name : ("Node_" + std::to_string(i));
        if (srcNode->mesh)
            dstNode.MeshIndex = static_cast<int32>(srcNode->mesh - Data->meshes);
        
        dstNode.Children.SetNum(static_cast<int32>(srcNode->children_count));
        for (cgltf_size j = 0; j < srcNode->children_count; ++j)
            dstNode.Children[static_cast<int32>(j)] = static_cast<int32>(srcNode->children[j] - Data->nodes);
        
        if (srcNode->has_translation)
            dstNode.Translation = Math::FVector3f(srcNode->translation[0], srcNode->translation[1], srcNode->translation[2]);
        if (srcNode->has_rotation)
            dstNode.Rotation = Math::FQuat4f(srcNode->rotation[0], srcNode->rotation[1], srcNode->rotation[2], srcNode->rotation[3]);
        if (srcNode->has_scale)
            dstNode.Scale = Math::FVector3f(srcNode->scale[0], srcNode->scale[1], srcNode->scale[2]);
    }
    
    OutModel.Scenes.SetNum(static_cast<int32>(Data->scenes_count));
    for (cgltf_size i = 0; i < Data->scenes_count; ++i)
    {
        cgltf_scene* srcScene = &Data->scenes[i];
        FGLTFScene& dstScene = OutModel.Scenes[static_cast<int32>(i)];
        
        dstScene.RootNodes.SetNum(static_cast<int32>(srcScene->nodes_count));
        for (cgltf_size j = 0; j < srcScene->nodes_count; ++j)
            dstScene.RootNodes[static_cast<int32>(j)] = static_cast<int32>(srcScene->nodes[j] - Data->nodes);
    }
    
    if (Data->scene)
        OutModel.DefaultSceneIndex = static_cast<int32>(Data->scene - Data->scenes);
    
    return true;
}

bool FGLTFLoader::_loadImageData(
    cgltf_image* Image,
    const String& BasePath,
    const FGLTFLoadOptions& Options,
    FGLTFImage& OutImage)
{
    if (!Image)
    {
        MR_LOG(LogGLTFLoader, Error, "Image pointer is null");
        return false;
    }
    
    int width = 0, height = 0, channels = 0;
    unsigned char* data = nullptr;
    
    if (Image->buffer_view && Image->buffer_view->buffer && Image->buffer_view->buffer->data)
    {
        const uint8* bufferData = static_cast<const uint8*>(Image->buffer_view->buffer->data);
        bufferData += Image->buffer_view->offset;
        MR_LOG(LogGLTFLoader, Verbose, "Loading image from buffer view, size: %zu", Image->buffer_view->size);
        data = stbi_load_from_memory(bufferData, static_cast<int>(Image->buffer_view->size),
                                     &width, &height, &channels, 4);
    }
    else if (Image->uri && Image->uri[0] != '\0')
    {
        String imagePath = CombinePaths(BasePath, Image->uri);
        MR_LOG(LogGLTFLoader, Verbose, "Loading image from file: %s", imagePath.c_str());
        data = stbi_load(imagePath.c_str(), &width, &height, &channels, 4);
        if (!data)
        {
            MR_LOG(LogGLTFLoader, Warning, "Failed to load image: %s", imagePath.c_str());
        }
    }
    else
    {
        MR_LOG(LogGLTFLoader, Warning, "Image has no valid buffer_view or uri");
        return false;
    }
    
    if (!data) return false;
    
    OutImage.Width = static_cast<uint32>(width);
    OutImage.Height = static_cast<uint32>(height);
    OutImage.Channels = 4;
    
    uint64 dataSize = static_cast<uint64>(width) * static_cast<uint64>(height) * 4;
    OutImage.Data.SetNum(static_cast<int32>(dataSize));
    std::memcpy(OutImage.Data.GetData(), data, dataSize);
    OutImage.bIsLoaded = true;
    
    stbi_image_free(data);
    return true;
}

void FGLTFLoader::_generateTangents(FGLTFPrimitive& Primitive)
{
    uint32 vertexCount = Primitive.GetVertexCount();
    uint32 indexCount = Primitive.GetIndexCount();
    if (vertexCount == 0 || indexCount == 0) return;
    
    Primitive.Tangents.SetNum(vertexCount);
    TArray<Math::FVector3f> tan1, tan2;
    tan1.SetNum(vertexCount);
    tan2.SetNum(vertexCount);
    
    for (uint32 i = 0; i < indexCount; i += 3)
    {
        uint32 i0 = Primitive.Indices[i], i1 = Primitive.Indices[i + 1], i2 = Primitive.Indices[i + 2];
        const Math::FVector3f& v0 = Primitive.Positions[i0];
        const Math::FVector3f& v1 = Primitive.Positions[i1];
        const Math::FVector3f& v2 = Primitive.Positions[i2];
        const Math::FVector2f& uv0 = Primitive.TexCoords0[i0];
        const Math::FVector2f& uv1 = Primitive.TexCoords0[i1];
        const Math::FVector2f& uv2 = Primitive.TexCoords0[i2];
        
        float x1 = v1.X - v0.X, x2 = v2.X - v0.X;
        float y1 = v1.Y - v0.Y, y2 = v2.Y - v0.Y;
        float z1 = v1.Z - v0.Z, z2 = v2.Z - v0.Z;
        float s1 = uv1.X - uv0.X, s2 = uv2.X - uv0.X;
        float t1 = uv1.Y - uv0.Y, t2 = uv2.Y - uv0.Y;
        float r = 1.0f / (s1 * t2 - s2 * t1 + 0.0001f);
        
        Math::FVector3f sdir((t2 * x1 - t1 * x2) * r, (t2 * y1 - t1 * y2) * r, (t2 * z1 - t1 * z2) * r);
        Math::FVector3f tdir((s1 * x2 - s2 * x1) * r, (s1 * y2 - s2 * y1) * r, (s1 * z2 - s2 * z1) * r);
        
        tan1[i0].X += sdir.X; tan1[i0].Y += sdir.Y; tan1[i0].Z += sdir.Z;
        tan1[i1].X += sdir.X; tan1[i1].Y += sdir.Y; tan1[i1].Z += sdir.Z;
        tan1[i2].X += sdir.X; tan1[i2].Y += sdir.Y; tan1[i2].Z += sdir.Z;
        tan2[i0].X += tdir.X; tan2[i0].Y += tdir.Y; tan2[i0].Z += tdir.Z;
        tan2[i1].X += tdir.X; tan2[i1].Y += tdir.Y; tan2[i1].Z += tdir.Z;
        tan2[i2].X += tdir.X; tan2[i2].Y += tdir.Y; tan2[i2].Z += tdir.Z;
    }
    
    for (uint32 i = 0; i < vertexCount; ++i)
    {
        const Math::FVector3f& n = Primitive.Normals[i];
        const Math::FVector3f& t = tan1[i];
        float dot = n.X * t.X + n.Y * t.Y + n.Z * t.Z;
        Math::FVector3f tangent(t.X - n.X * dot, t.Y - n.Y * dot, t.Z - n.Z * dot);
        float len = std::sqrt(tangent.X * tangent.X + tangent.Y * tangent.Y + tangent.Z * tangent.Z);
        if (len > 0.0001f) { tangent.X /= len; tangent.Y /= len; tangent.Z /= len; }
        
        Math::FVector3f cross(n.Y * t.Z - n.Z * t.Y, n.Z * t.X - n.X * t.Z, n.X * t.Y - n.Y * t.X);
        float handedness = (cross.X * tan2[i].X + cross.Y * tan2[i].Y + cross.Z * tan2[i].Z) < 0.0f ? -1.0f : 1.0f;
        Primitive.Tangents[i] = Math::FVector4f(tangent.X, tangent.Y, tangent.Z, handedness);
    }
}

void FGLTFLoader::_generateNormals(FGLTFPrimitive& Primitive)
{
    uint32 vertexCount = Primitive.GetVertexCount();
    uint32 indexCount = Primitive.GetIndexCount();
    if (vertexCount == 0 || indexCount == 0) return;
    
    Primitive.Normals.SetNum(vertexCount);
    for (uint32 i = 0; i < vertexCount; ++i)
        Primitive.Normals[i] = Math::FVector3f(0.0f, 0.0f, 0.0f);
    
    for (uint32 i = 0; i < indexCount; i += 3)
    {
        uint32 i0 = Primitive.Indices[i], i1 = Primitive.Indices[i + 1], i2 = Primitive.Indices[i + 2];
        const Math::FVector3f& v0 = Primitive.Positions[i0];
        const Math::FVector3f& v1 = Primitive.Positions[i1];
        const Math::FVector3f& v2 = Primitive.Positions[i2];
        
        Math::FVector3f e1(v1.X - v0.X, v1.Y - v0.Y, v1.Z - v0.Z);
        Math::FVector3f e2(v2.X - v0.X, v2.Y - v0.Y, v2.Z - v0.Z);
        Math::FVector3f normal(e1.Y * e2.Z - e1.Z * e2.Y, e1.Z * e2.X - e1.X * e2.Z, e1.X * e2.Y - e1.Y * e2.X);
        
        Primitive.Normals[i0].X += normal.X; Primitive.Normals[i0].Y += normal.Y; Primitive.Normals[i0].Z += normal.Z;
        Primitive.Normals[i1].X += normal.X; Primitive.Normals[i1].Y += normal.Y; Primitive.Normals[i1].Z += normal.Z;
        Primitive.Normals[i2].X += normal.X; Primitive.Normals[i2].Y += normal.Y; Primitive.Normals[i2].Z += normal.Z;
    }
    
    for (uint32 i = 0; i < vertexCount; ++i)
    {
        Math::FVector3f& n = Primitive.Normals[i];
        float len = std::sqrt(n.X * n.X + n.Y * n.Y + n.Z * n.Z);
        if (len > 0.0001f) { n.X /= len; n.Y /= len; n.Z /= len; }
        else n = Math::FVector3f(0.0f, 0.0f, 1.0f);
    }
}

void FGLTFLoader::_computeBounds(FGLTFModel& Model)
{
    Math::FVector3f minBounds(FLT_MAX, FLT_MAX, FLT_MAX);
    Math::FVector3f maxBounds(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    
    for (const FGLTFMesh& mesh : Model.Meshes)
    {
        for (const FGLTFPrimitive& prim : mesh.Primitives)
        {
            if (prim.BoundsMin.X < minBounds.X) minBounds.X = prim.BoundsMin.X;
            if (prim.BoundsMin.Y < minBounds.Y) minBounds.Y = prim.BoundsMin.Y;
            if (prim.BoundsMin.Z < minBounds.Z) minBounds.Z = prim.BoundsMin.Z;
            if (prim.BoundsMax.X > maxBounds.X) maxBounds.X = prim.BoundsMax.X;
            if (prim.BoundsMax.Y > maxBounds.Y) maxBounds.Y = prim.BoundsMax.Y;
            if (prim.BoundsMax.Z > maxBounds.Z) maxBounds.Z = prim.BoundsMax.Z;
        }
    }
    
    Model.Bounds = Math::FBox3f(minBounds, maxBounds);
}

void FGLTFLoader::_setError(EGLTFLoadResult Error, const String& Message)
{
    m_lastError = Error;
    m_lastErrorMessage = Message;
    MR_LOG(LogGLTFLoader, Error, "%s: %s", GLTFLoadResultToString(Error), Message.c_str());
}

} // namespace MonsterEngine
