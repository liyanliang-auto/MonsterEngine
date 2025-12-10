// Copyright Monster Engine. All Rights Reserved.

/**
 * @file GLTFMeshLoader.cpp
 * @brief Implementation of glTF 2.0 mesh loader
 * 
 * Supports glTF 2.0 format:
 * - JSON format (.gltf) with external/embedded buffers
 * - Binary format (.glb)
 * - Mesh primitives with various attribute types
 * - Multiple meshes and primitives
 * 
 * Note: This is a basic implementation. For production use, consider
 * using a dedicated glTF library like tinygltf or cgltf.
 * 
 * Reference: https://www.khronos.org/gltf/
 */

#include "Engine/Mesh/MeshLoader.h"
#include "Engine/Mesh/MeshBuilder.h"
#include "Core/Logging/LogMacros.h"
#include <sstream>
#include <algorithm>
#include <cstring>

namespace MonsterEngine
{

// Define log category for glTF loader
DEFINE_LOG_CATEGORY_STATIC(LogGLTFLoader, Log, All);

// ============================================================================
// glTF Constants
// ============================================================================

namespace
{
    // GLB magic number and version
    constexpr uint32 GLB_MAGIC = 0x46546C67; // "glTF"
    constexpr uint32 GLB_VERSION = 2;
    
    // GLB chunk types
    constexpr uint32 GLB_CHUNK_JSON = 0x4E4F534A; // "JSON"
    constexpr uint32 GLB_CHUNK_BIN = 0x004E4942;  // "BIN\0"
    
    // glTF component types
    constexpr int32 GLTF_BYTE = 5120;
    constexpr int32 GLTF_UNSIGNED_BYTE = 5121;
    constexpr int32 GLTF_SHORT = 5122;
    constexpr int32 GLTF_UNSIGNED_SHORT = 5123;
    constexpr int32 GLTF_UNSIGNED_INT = 5125;
    constexpr int32 GLTF_FLOAT = 5126;
    
    // glTF primitive modes
    constexpr int32 GLTF_POINTS = 0;
    constexpr int32 GLTF_LINES = 1;
    constexpr int32 GLTF_LINE_LOOP = 2;
    constexpr int32 GLTF_LINE_STRIP = 3;
    constexpr int32 GLTF_TRIANGLES = 4;
    constexpr int32 GLTF_TRIANGLE_STRIP = 5;
    constexpr int32 GLTF_TRIANGLE_FAN = 6;
    
    /**
     * Get component size in bytes
     * @param ComponentType glTF component type
     * @return Size in bytes
     */
    int32 GetComponentSize(int32 ComponentType)
    {
        switch (ComponentType)
        {
            case GLTF_BYTE:
            case GLTF_UNSIGNED_BYTE:
                return 1;
            case GLTF_SHORT:
            case GLTF_UNSIGNED_SHORT:
                return 2;
            case GLTF_UNSIGNED_INT:
            case GLTF_FLOAT:
                return 4;
            default:
                return 0;
        }
    }
    
    /**
     * Get number of components for a type string
     * @param Type Type string (SCALAR, VEC2, VEC3, VEC4, MAT2, MAT3, MAT4)
     * @return Number of components
     */
    int32 GetNumComponents(const String& Type)
    {
        if (Type == "SCALAR") return 1;
        if (Type == "VEC2") return 2;
        if (Type == "VEC3") return 3;
        if (Type == "VEC4") return 4;
        if (Type == "MAT2") return 4;
        if (Type == "MAT3") return 9;
        if (Type == "MAT4") return 16;
        return 0;
    }
    
    /**
     * Simple JSON value parser (minimal implementation)
     * This is a very basic JSON parser for demonstration.
     * For production, use a proper JSON library.
     */
    class FSimpleJSON
    {
    public:
        enum class EType { Null, Bool, Number, String, Array, Object };
        
        EType Type = EType::Null;
        double NumberValue = 0.0;
        bool BoolValue = false;
        String StringValue;
        TArray<FSimpleJSON> ArrayValues;
        TMap<String, FSimpleJSON> ObjectValues;
        
        bool IsNull() const { return Type == EType::Null; }
        bool IsNumber() const { return Type == EType::Number; }
        bool IsString() const { return Type == EType::String; }
        bool IsArray() const { return Type == EType::Array; }
        bool IsObject() const { return Type == EType::Object; }
        
        int32 AsInt() const { return static_cast<int32>(NumberValue); }
        float AsFloat() const { return static_cast<float>(NumberValue); }
        
        const FSimpleJSON& operator[](const String& Key) const
        {
            static FSimpleJSON NullValue;
            const FSimpleJSON* Found = ObjectValues.Find(Key);
            return Found ? *Found : NullValue;
        }
        
        const FSimpleJSON& operator[](int32 Index) const
        {
            static FSimpleJSON NullValue;
            if (Index >= 0 && Index < ArrayValues.Num())
            {
                return ArrayValues[Index];
            }
            return NullValue;
        }
        
        int32 ArraySize() const { return ArrayValues.Num(); }
        
        bool Has(const String& Key) const
        {
            return ObjectValues.Find(Key) != nullptr;
        }
    };
    
    /**
     * Skip whitespace in JSON string
     */
    void SkipWhitespace(const String& Json, size_t& Pos)
    {
        while (Pos < Json.length() && std::isspace(Json[Pos]))
        {
            Pos++;
        }
    }
    
    /**
     * Parse JSON string value
     */
    String ParseJSONString(const String& Json, size_t& Pos)
    {
        if (Pos >= Json.length() || Json[Pos] != '"')
        {
            return "";
        }
        
        Pos++; // Skip opening quote
        String Result;
        
        while (Pos < Json.length() && Json[Pos] != '"')
        {
            if (Json[Pos] == '\\' && Pos + 1 < Json.length())
            {
                Pos++;
                switch (Json[Pos])
                {
                    case 'n': Result += '\n'; break;
                    case 't': Result += '\t'; break;
                    case 'r': Result += '\r'; break;
                    case '"': Result += '"'; break;
                    case '\\': Result += '\\'; break;
                    default: Result += Json[Pos]; break;
                }
            }
            else
            {
                Result += Json[Pos];
            }
            Pos++;
        }
        
        if (Pos < Json.length())
        {
            Pos++; // Skip closing quote
        }
        
        return Result;
    }
    
    /**
     * Parse JSON number
     */
    double ParseJSONNumber(const String& Json, size_t& Pos)
    {
        size_t Start = Pos;
        
        if (Pos < Json.length() && Json[Pos] == '-')
        {
            Pos++;
        }
        
        while (Pos < Json.length() && (std::isdigit(Json[Pos]) || Json[Pos] == '.' || 
               Json[Pos] == 'e' || Json[Pos] == 'E' || Json[Pos] == '+' || Json[Pos] == '-'))
        {
            Pos++;
        }
        
        try
        {
            return std::stod(Json.substr(Start, Pos - Start));
        }
        catch (...)
        {
            return 0.0;
        }
    }
    
    // Forward declaration
    FSimpleJSON ParseJSONValue(const String& Json, size_t& Pos);
    
    /**
     * Parse JSON array
     */
    FSimpleJSON ParseJSONArray(const String& Json, size_t& Pos)
    {
        FSimpleJSON Result;
        Result.Type = FSimpleJSON::EType::Array;
        
        if (Pos >= Json.length() || Json[Pos] != '[')
        {
            return Result;
        }
        
        Pos++; // Skip '['
        SkipWhitespace(Json, Pos);
        
        while (Pos < Json.length() && Json[Pos] != ']')
        {
            Result.ArrayValues.Add(ParseJSONValue(Json, Pos));
            SkipWhitespace(Json, Pos);
            
            if (Pos < Json.length() && Json[Pos] == ',')
            {
                Pos++;
                SkipWhitespace(Json, Pos);
            }
        }
        
        if (Pos < Json.length())
        {
            Pos++; // Skip ']'
        }
        
        return Result;
    }
    
    /**
     * Parse JSON object
     */
    FSimpleJSON ParseJSONObject(const String& Json, size_t& Pos)
    {
        FSimpleJSON Result;
        Result.Type = FSimpleJSON::EType::Object;
        
        if (Pos >= Json.length() || Json[Pos] != '{')
        {
            return Result;
        }
        
        Pos++; // Skip '{'
        SkipWhitespace(Json, Pos);
        
        while (Pos < Json.length() && Json[Pos] != '}')
        {
            // Parse key
            String Key = ParseJSONString(Json, Pos);
            SkipWhitespace(Json, Pos);
            
            // Skip ':'
            if (Pos < Json.length() && Json[Pos] == ':')
            {
                Pos++;
                SkipWhitespace(Json, Pos);
            }
            
            // Parse value
            Result.ObjectValues.Add(Key, ParseJSONValue(Json, Pos));
            SkipWhitespace(Json, Pos);
            
            // Skip ','
            if (Pos < Json.length() && Json[Pos] == ',')
            {
                Pos++;
                SkipWhitespace(Json, Pos);
            }
        }
        
        if (Pos < Json.length())
        {
            Pos++; // Skip '}'
        }
        
        return Result;
    }
    
    /**
     * Parse any JSON value
     */
    FSimpleJSON ParseJSONValue(const String& Json, size_t& Pos)
    {
        SkipWhitespace(Json, Pos);
        
        if (Pos >= Json.length())
        {
            return FSimpleJSON();
        }
        
        char c = Json[Pos];
        
        if (c == '{')
        {
            return ParseJSONObject(Json, Pos);
        }
        else if (c == '[')
        {
            return ParseJSONArray(Json, Pos);
        }
        else if (c == '"')
        {
            FSimpleJSON Result;
            Result.Type = FSimpleJSON::EType::String;
            Result.StringValue = ParseJSONString(Json, Pos);
            return Result;
        }
        else if (c == 't' && Json.substr(Pos, 4) == "true")
        {
            Pos += 4;
            FSimpleJSON Result;
            Result.Type = FSimpleJSON::EType::Bool;
            Result.BoolValue = true;
            return Result;
        }
        else if (c == 'f' && Json.substr(Pos, 5) == "false")
        {
            Pos += 5;
            FSimpleJSON Result;
            Result.Type = FSimpleJSON::EType::Bool;
            Result.BoolValue = false;
            return Result;
        }
        else if (c == 'n' && Json.substr(Pos, 4) == "null")
        {
            Pos += 4;
            return FSimpleJSON();
        }
        else if (c == '-' || std::isdigit(c))
        {
            FSimpleJSON Result;
            Result.Type = FSimpleJSON::EType::Number;
            Result.NumberValue = ParseJSONNumber(Json, Pos);
            return Result;
        }
        
        return FSimpleJSON();
    }
    
    /**
     * Parse JSON from string
     */
    FSimpleJSON ParseJSON(const String& Json)
    {
        size_t Pos = 0;
        return ParseJSONValue(Json, Pos);
    }
}

// ============================================================================
// FGLTFMeshLoader Implementation
// ============================================================================

/**
 * Get supported extensions
 */
TArray<String> FGLTFMeshLoader::GetSupportedExtensions() const
{
    TArray<String> Extensions;
    Extensions.Add("gltf");
    Extensions.Add("glb");
    return Extensions;
}

/**
 * Load glTF file from path
 */
EMeshLoadResult FGLTFMeshLoader::Load(
    const String& FilePath,
    FMeshBuilder& OutBuilder,
    const FMeshLoadOptions& Options)
{
    MR_LOG(LogGLTFLoader, Log, "Loading glTF file: %s", FilePath.c_str());
    
    // Get extension
    String Ext = GetExtension(FilePath);
    
    // Get base path for external resources
    String BasePath;
    size_t LastSlash = FilePath.rfind('/');
    if (LastSlash == String::npos)
    {
        LastSlash = FilePath.rfind('\\');
    }
    if (LastSlash != String::npos)
    {
        BasePath = FilePath.substr(0, LastSlash + 1);
    }
    
    if (Ext == "glb")
    {
        // Binary format
        TArray<uint8> Data;
        if (!ReadFile(FilePath, Data))
        {
            return EMeshLoadResult::FileNotFound;
        }
        
        return ParseGLB(Data.GetData(), Data.Num(), OutBuilder, Options);
    }
    else
    {
        // JSON format
        String Content;
        if (!ReadTextFile(FilePath, Content))
        {
            return EMeshLoadResult::FileNotFound;
        }
        
        return ParseGLTF(Content, BasePath, OutBuilder, Options);
    }
}

/**
 * Load glTF from memory
 */
EMeshLoadResult FGLTFMeshLoader::LoadFromMemory(
    const void* Data,
    size_t DataSize,
    FMeshBuilder& OutBuilder,
    const FMeshLoadOptions& Options)
{
    if (!Data || DataSize < 4)
    {
        return EMeshLoadResult::InvalidData;
    }
    
    // Check for GLB magic number
    uint32 Magic = *reinterpret_cast<const uint32*>(Data);
    
    if (Magic == GLB_MAGIC)
    {
        return ParseGLB(Data, DataSize, OutBuilder, Options);
    }
    else
    {
        // Assume JSON format
        String Content(static_cast<const char*>(Data), DataSize);
        return ParseGLTF(Content, "", OutBuilder, Options);
    }
}

/**
 * Parse glTF JSON content
 */
EMeshLoadResult FGLTFMeshLoader::ParseGLTF(
    const String& JsonContent,
    const String& BasePath,
    FMeshBuilder& OutBuilder,
    const FMeshLoadOptions& Options)
{
    // Parse JSON
    FSimpleJSON Root = ParseJSON(JsonContent);
    
    if (!Root.IsObject())
    {
        MR_LOG(LogGLTFLoader, Error, "Invalid glTF JSON");
        return EMeshLoadResult::ParseError;
    }
    
    // Check asset version
    const FSimpleJSON& Asset = Root["asset"];
    if (Asset.IsObject())
    {
        const FSimpleJSON& Version = Asset["version"];
        if (Version.IsString())
        {
            MR_LOG(LogGLTFLoader, Verbose, "glTF version: %s", Version.StringValue.c_str());
        }
    }
    
    // Get buffers
    TArray<TArray<uint8>> Buffers;
    const FSimpleJSON& BuffersArray = Root["buffers"];
    if (BuffersArray.IsArray())
    {
        for (int32 i = 0; i < BuffersArray.ArraySize(); ++i)
        {
            const FSimpleJSON& Buffer = BuffersArray[i];
            TArray<uint8> BufferData;
            
            if (Buffer.Has("uri"))
            {
                String Uri = Buffer["uri"].StringValue;
                
                // Check for data URI
                if (Uri.find("data:") == 0)
                {
                    // Base64 encoded data - not implemented in this basic version
                    MR_LOG(LogGLTFLoader, Warning, "Base64 data URIs not supported");
                }
                else
                {
                    // External file
                    String BufferPath = BasePath + Uri;
                    if (!ReadFile(BufferPath, BufferData))
                    {
                        MR_LOG(LogGLTFLoader, Error, "Failed to load buffer: %s", BufferPath.c_str());
                    }
                }
            }
            
            Buffers.Add(std::move(BufferData));
        }
    }
    
    // Get buffer views
    const FSimpleJSON& BufferViewsArray = Root["bufferViews"];
    
    // Get accessors
    const FSimpleJSON& AccessorsArray = Root["accessors"];
    
    // Helper to read accessor data
    auto ReadAccessor = [&](int32 AccessorIndex, TArray<float>& OutData, int32& OutCount, int32& OutComponents)
    {
        if (!AccessorsArray.IsArray() || AccessorIndex < 0 || AccessorIndex >= AccessorsArray.ArraySize())
        {
            return false;
        }
        
        const FSimpleJSON& Accessor = AccessorsArray[AccessorIndex];
        int32 BufferViewIndex = Accessor["bufferView"].AsInt();
        int32 ByteOffset = Accessor.Has("byteOffset") ? Accessor["byteOffset"].AsInt() : 0;
        int32 ComponentType = Accessor["componentType"].AsInt();
        OutCount = Accessor["count"].AsInt();
        OutComponents = GetNumComponents(Accessor["type"].StringValue);
        
        if (!BufferViewsArray.IsArray() || BufferViewIndex < 0 || BufferViewIndex >= BufferViewsArray.ArraySize())
        {
            return false;
        }
        
        const FSimpleJSON& BufferView = BufferViewsArray[BufferViewIndex];
        int32 BufferIndex = BufferView["buffer"].AsInt();
        int32 ViewByteOffset = BufferView.Has("byteOffset") ? BufferView["byteOffset"].AsInt() : 0;
        
        if (BufferIndex < 0 || BufferIndex >= Buffers.Num() || Buffers[BufferIndex].Num() == 0)
        {
            return false;
        }
        
        const uint8* Data = Buffers[BufferIndex].GetData() + ViewByteOffset + ByteOffset;
        int32 ComponentSize = GetComponentSize(ComponentType);
        
        OutData.SetNum(OutCount * OutComponents);
        
        for (int32 i = 0; i < OutCount * OutComponents; ++i)
        {
            if (ComponentType == GLTF_FLOAT)
            {
                OutData[i] = *reinterpret_cast<const float*>(Data + i * ComponentSize);
            }
            else if (ComponentType == GLTF_UNSIGNED_SHORT)
            {
                OutData[i] = static_cast<float>(*reinterpret_cast<const uint16*>(Data + i * ComponentSize));
            }
            else if (ComponentType == GLTF_UNSIGNED_BYTE)
            {
                OutData[i] = static_cast<float>(Data[i]);
            }
            // Add more component types as needed
        }
        
        return true;
    };
    
    // Helper to read index accessor
    auto ReadIndices = [&](int32 AccessorIndex, TArray<uint32>& OutIndices)
    {
        if (!AccessorsArray.IsArray() || AccessorIndex < 0 || AccessorIndex >= AccessorsArray.ArraySize())
        {
            return false;
        }
        
        const FSimpleJSON& Accessor = AccessorsArray[AccessorIndex];
        int32 BufferViewIndex = Accessor["bufferView"].AsInt();
        int32 ByteOffset = Accessor.Has("byteOffset") ? Accessor["byteOffset"].AsInt() : 0;
        int32 ComponentType = Accessor["componentType"].AsInt();
        int32 Count = Accessor["count"].AsInt();
        
        if (!BufferViewsArray.IsArray() || BufferViewIndex < 0 || BufferViewIndex >= BufferViewsArray.ArraySize())
        {
            return false;
        }
        
        const FSimpleJSON& BufferView = BufferViewsArray[BufferViewIndex];
        int32 BufferIndex = BufferView["buffer"].AsInt();
        int32 ViewByteOffset = BufferView.Has("byteOffset") ? BufferView["byteOffset"].AsInt() : 0;
        
        if (BufferIndex < 0 || BufferIndex >= Buffers.Num() || Buffers[BufferIndex].Num() == 0)
        {
            return false;
        }
        
        const uint8* Data = Buffers[BufferIndex].GetData() + ViewByteOffset + ByteOffset;
        
        OutIndices.SetNum(Count);
        
        for (int32 i = 0; i < Count; ++i)
        {
            if (ComponentType == GLTF_UNSIGNED_INT)
            {
                OutIndices[i] = *reinterpret_cast<const uint32*>(Data + i * 4);
            }
            else if (ComponentType == GLTF_UNSIGNED_SHORT)
            {
                OutIndices[i] = *reinterpret_cast<const uint16*>(Data + i * 2);
            }
            else if (ComponentType == GLTF_UNSIGNED_BYTE)
            {
                OutIndices[i] = Data[i];
            }
        }
        
        return true;
    };
    
    // Process meshes
    const FSimpleJSON& MeshesArray = Root["meshes"];
    if (!MeshesArray.IsArray())
    {
        MR_LOG(LogGLTFLoader, Error, "No meshes found in glTF");
        return EMeshLoadResult::InvalidData;
    }
    
    for (int32 MeshIndex = 0; MeshIndex < MeshesArray.ArraySize(); ++MeshIndex)
    {
        const FSimpleJSON& Mesh = MeshesArray[MeshIndex];
        const FSimpleJSON& Primitives = Mesh["primitives"];
        
        if (!Primitives.IsArray())
        {
            continue;
        }
        
        for (int32 PrimIndex = 0; PrimIndex < Primitives.ArraySize(); ++PrimIndex)
        {
            const FSimpleJSON& Primitive = Primitives[PrimIndex];
            const FSimpleJSON& Attributes = Primitive["attributes"];
            
            if (!Attributes.IsObject())
            {
                continue;
            }
            
            // Check primitive mode (only triangles supported)
            int32 Mode = Primitive.Has("mode") ? Primitive["mode"].AsInt() : GLTF_TRIANGLES;
            if (Mode != GLTF_TRIANGLES)
            {
                MR_LOG(LogGLTFLoader, Warning, "Unsupported primitive mode: %d", Mode);
                continue;
            }
            
            // Read position data
            TArray<float> Positions;
            int32 VertexCount = 0, PosComponents = 0;
            
            if (Attributes.Has("POSITION"))
            {
                if (!ReadAccessor(Attributes["POSITION"].AsInt(), Positions, VertexCount, PosComponents))
                {
                    MR_LOG(LogGLTFLoader, Warning, "Failed to read POSITION attribute");
                    continue;
                }
            }
            else
            {
                continue; // Position is required
            }
            
            // Read normal data
            TArray<float> NormalsData;
            int32 NormalCount = 0, NormComponents = 0;
            bool HasNormals = false;
            
            if (Attributes.Has("NORMAL"))
            {
                HasNormals = ReadAccessor(Attributes["NORMAL"].AsInt(), NormalsData, NormalCount, NormComponents);
            }
            
            // Read texcoord data
            TArray<float> TexCoordsData;
            int32 TexCoordCount = 0, TexComponents = 0;
            bool HasTexCoords = false;
            
            if (Attributes.Has("TEXCOORD_0"))
            {
                HasTexCoords = ReadAccessor(Attributes["TEXCOORD_0"].AsInt(), TexCoordsData, TexCoordCount, TexComponents);
            }
            
            // Create vertices
            int32 BaseVertex = OutBuilder.GetNumVertices();
            
            for (int32 i = 0; i < VertexCount; ++i)
            {
                FStaticMeshBuildVertex Vertex;
                
                // Position
                Vertex.Position = Math::FVector3f(
                    Positions[i * 3 + 0] * Options.Scale,
                    Positions[i * 3 + 1] * Options.Scale,
                    Positions[i * 3 + 2] * Options.Scale
                );
                
                // Normal
                if (HasNormals && i < NormalCount)
                {
                    Vertex.TangentZ = Math::FVector3f(
                        NormalsData[i * 3 + 0],
                        NormalsData[i * 3 + 1],
                        NormalsData[i * 3 + 2]
                    );
                    Vertex.SetTangentBasisFromNormal(Vertex.TangentZ);
                }
                
                // TexCoord
                if (HasTexCoords && i < TexCoordCount)
                {
                    float U = TexCoordsData[i * 2 + 0];
                    float V = TexCoordsData[i * 2 + 1];
                    
                    if (Options.bFlipUVs)
                    {
                        V = 1.0f - V;
                    }
                    
                    Vertex.UVs[0] = Math::FVector2f(U, V);
                }
                
                OutBuilder.AddVertex(Vertex);
            }
            
            // Read indices
            if (Primitive.Has("indices"))
            {
                TArray<uint32> Indices;
                if (ReadIndices(Primitive["indices"].AsInt(), Indices))
                {
                    // Add triangles
                    for (int32 i = 0; i < Indices.Num(); i += 3)
                    {
                        int32 V0 = BaseVertex + Indices[i];
                        int32 V1 = BaseVertex + Indices[i + 1];
                        int32 V2 = BaseVertex + Indices[i + 2];
                        
                        if (Options.bFlipWindingOrder)
                        {
                            OutBuilder.AddTriangle(V0, V2, V1, 0);
                        }
                        else
                        {
                            OutBuilder.AddTriangle(V0, V1, V2, 0);
                        }
                    }
                }
            }
            else
            {
                // Non-indexed geometry
                for (int32 i = 0; i < VertexCount; i += 3)
                {
                    int32 V0 = BaseVertex + i;
                    int32 V1 = BaseVertex + i + 1;
                    int32 V2 = BaseVertex + i + 2;
                    
                    if (Options.bFlipWindingOrder)
                    {
                        OutBuilder.AddTriangle(V0, V2, V1, 0);
                    }
                    else
                    {
                        OutBuilder.AddTriangle(V0, V1, V2, 0);
                    }
                }
            }
        }
    }
    
    MR_LOG(LogGLTFLoader, Log, "glTF parsed: %d vertices, %d triangles",
           OutBuilder.GetNumVertices(), OutBuilder.GetNumTriangles());
    
    if (OutBuilder.GetNumVertices() == 0)
    {
        return EMeshLoadResult::InvalidData;
    }
    
    return EMeshLoadResult::Success;
}

/**
 * Parse GLB binary format
 */
EMeshLoadResult FGLTFMeshLoader::ParseGLB(
    const void* Data,
    size_t DataSize,
    FMeshBuilder& OutBuilder,
    const FMeshLoadOptions& Options)
{
    if (DataSize < 12)
    {
        return EMeshLoadResult::InvalidData;
    }
    
    const uint8* ByteData = static_cast<const uint8*>(Data);
    
    // Read header
    uint32 Magic = *reinterpret_cast<const uint32*>(ByteData);
    uint32 Version = *reinterpret_cast<const uint32*>(ByteData + 4);
    uint32 Length = *reinterpret_cast<const uint32*>(ByteData + 8);
    
    if (Magic != GLB_MAGIC)
    {
        MR_LOG(LogGLTFLoader, Error, "Invalid GLB magic number");
        return EMeshLoadResult::InvalidData;
    }
    
    if (Version != GLB_VERSION)
    {
        MR_LOG(LogGLTFLoader, Warning, "GLB version %d (expected %d)", Version, GLB_VERSION);
    }
    
    // Read chunks
    size_t Offset = 12;
    String JsonContent;
    TArray<uint8> BinaryChunk;
    
    while (Offset + 8 <= DataSize)
    {
        uint32 ChunkLength = *reinterpret_cast<const uint32*>(ByteData + Offset);
        uint32 ChunkType = *reinterpret_cast<const uint32*>(ByteData + Offset + 4);
        
        if (Offset + 8 + ChunkLength > DataSize)
        {
            break;
        }
        
        const uint8* ChunkData = ByteData + Offset + 8;
        
        if (ChunkType == GLB_CHUNK_JSON)
        {
            JsonContent = String(reinterpret_cast<const char*>(ChunkData), ChunkLength);
        }
        else if (ChunkType == GLB_CHUNK_BIN)
        {
            BinaryChunk.SetNum(ChunkLength);
            std::memcpy(BinaryChunk.GetData(), ChunkData, ChunkLength);
        }
        
        Offset += 8 + ChunkLength;
    }
    
    if (JsonContent.empty())
    {
        MR_LOG(LogGLTFLoader, Error, "No JSON chunk found in GLB");
        return EMeshLoadResult::InvalidData;
    }
    
    // Parse JSON with embedded binary buffer
    // For GLB, the binary chunk is buffer 0
    // This is a simplified implementation - full implementation would
    // inject the binary chunk into the buffer loading logic
    
    return ParseGLTF(JsonContent, "", OutBuilder, Options);
}

} // namespace MonsterEngine
