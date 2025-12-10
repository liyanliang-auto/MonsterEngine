// Copyright Monster Engine. All Rights Reserved.

/**
 * @file OBJMeshLoader.cpp
 * @brief Implementation of Wavefront OBJ mesh loader
 * 
 * Supports the Wavefront OBJ file format:
 * - Vertex positions (v x y z)
 * - Texture coordinates (vt u v)
 * - Vertex normals (vn x y z)
 * - Faces (f v/vt/vn v/vt/vn v/vt/vn ...)
 * - Material groups (usemtl name)
 * - Object/group names (o name, g name)
 * 
 * Reference: https://en.wikipedia.org/wiki/Wavefront_.obj_file
 */

#include "Engine/Mesh/MeshLoader.h"
#include "Engine/Mesh/MeshBuilder.h"
#include "Core/Logging/LogMacros.h"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace MonsterEngine
{

// Define log category for OBJ loader
DEFINE_LOG_CATEGORY_STATIC(LogOBJLoader, Log, All);

// ============================================================================
// Helper Functions
// ============================================================================

namespace
{
    /**
     * Trim whitespace from both ends of a string
     * @param Str String to trim
     * @return Trimmed string
     */
    String TrimString(const String& Str)
    {
        size_t Start = Str.find_first_not_of(" \t\r\n");
        if (Start == String::npos)
        {
            return "";
        }
        
        size_t End = Str.find_last_not_of(" \t\r\n");
        return Str.substr(Start, End - Start + 1);
    }
    
    /**
     * Split string by delimiter
     * @param Str String to split
     * @param Delimiter Delimiter character
     * @return Array of tokens
     */
    TArray<String> SplitString(const String& Str, char Delimiter)
    {
        TArray<String> Tokens;
        std::stringstream SS(Str);
        String Token;
        
        while (std::getline(SS, Token, Delimiter))
        {
            if (!Token.empty())
            {
                Tokens.Add(Token);
            }
        }
        
        return Tokens;
    }
    
    /**
     * Split string by whitespace
     * @param Str String to split
     * @return Array of tokens
     */
    TArray<String> SplitByWhitespace(const String& Str)
    {
        TArray<String> Tokens;
        std::stringstream SS(Str);
        String Token;
        
        while (SS >> Token)
        {
            Tokens.Add(Token);
        }
        
        return Tokens;
    }
    
    /**
     * Parse float from string
     * @param Str String to parse
     * @param OutValue Output value
     * @return true if successful
     */
    bool ParseFloat(const String& Str, float& OutValue)
    {
        try
        {
            OutValue = std::stof(Str);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
    
    /**
     * Parse int from string
     * @param Str String to parse
     * @param OutValue Output value
     * @return true if successful
     */
    bool ParseInt(const String& Str, int32& OutValue)
    {
        try
        {
            OutValue = std::stoi(Str);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
}

// ============================================================================
// FOBJMeshLoader Implementation
// ============================================================================

/**
 * Get supported extensions
 */
TArray<String> FOBJMeshLoader::GetSupportedExtensions() const
{
    TArray<String> Extensions;
    Extensions.Add("obj");
    return Extensions;
}

/**
 * Load OBJ file from path
 */
EMeshLoadResult FOBJMeshLoader::Load(
    const String& FilePath,
    FMeshBuilder& OutBuilder,
    const FMeshLoadOptions& Options)
{
    MR_LOG(LogOBJLoader, Log, "Loading OBJ file: %s", FilePath.c_str());
    
    // Read file content
    String Content;
    if (!ReadTextFile(FilePath, Content))
    {
        return EMeshLoadResult::FileNotFound;
    }
    
    // Parse the content
    return ParseOBJ(Content, OutBuilder, Options);
}

/**
 * Load OBJ from memory
 */
EMeshLoadResult FOBJMeshLoader::LoadFromMemory(
    const void* Data,
    size_t DataSize,
    FMeshBuilder& OutBuilder,
    const FMeshLoadOptions& Options)
{
    if (!Data || DataSize == 0)
    {
        return EMeshLoadResult::InvalidData;
    }
    
    // Convert to string
    String Content(static_cast<const char*>(Data), DataSize);
    
    // Parse the content
    return ParseOBJ(Content, OutBuilder, Options);
}

/**
 * Parse OBJ file content
 */
EMeshLoadResult FOBJMeshLoader::ParseOBJ(
    const String& Content,
    FMeshBuilder& OutBuilder,
    const FMeshLoadOptions& Options)
{
    // Temporary storage for OBJ data
    // OBJ indices are 1-based and can be negative (relative)
    TArray<Math::FVector3f> Positions;
    TArray<Math::FVector2f> TexCoords;
    TArray<Math::FVector3f> Normals;
    
    // Material tracking
    TMap<String, int32> MaterialMap;
    int32 CurrentMaterial = 0;
    
    // Parse line by line
    std::istringstream Stream(Content);
    String Line;
    int32 LineNumber = 0;
    
    while (std::getline(Stream, Line))
    {
        LineNumber++;
        
        // Trim and skip empty lines and comments
        Line = TrimString(Line);
        if (Line.empty() || Line[0] == '#')
        {
            continue;
        }
        
        // Split line into tokens
        TArray<String> Tokens = SplitByWhitespace(Line);
        if (Tokens.Num() == 0)
        {
            continue;
        }
        
        const String& Command = Tokens[0];
        
        // Vertex position: v x y z [w]
        if (Command == "v")
        {
            if (Tokens.Num() >= 4)
            {
                float X, Y, Z;
                if (ParseFloat(Tokens[1], X) && ParseFloat(Tokens[2], Y) && ParseFloat(Tokens[3], Z))
                {
                    // Apply scale
                    X *= Options.Scale;
                    Y *= Options.Scale;
                    Z *= Options.Scale;
                    
                    Positions.Add(Math::FVector3f(X, Y, Z));
                }
                else
                {
                    MR_LOG(LogOBJLoader, Warning, "Invalid vertex at line %d", LineNumber);
                }
            }
        }
        // Texture coordinate: vt u v [w]
        else if (Command == "vt")
        {
            if (Tokens.Num() >= 3)
            {
                float U, V;
                if (ParseFloat(Tokens[1], U) && ParseFloat(Tokens[2], V))
                {
                    // Flip V if requested (OBJ uses bottom-left origin)
                    if (Options.bFlipUVs)
                    {
                        V = 1.0f - V;
                    }
                    
                    TexCoords.Add(Math::FVector2f(U, V));
                }
            }
        }
        // Vertex normal: vn x y z
        else if (Command == "vn")
        {
            if (Tokens.Num() >= 4)
            {
                float X, Y, Z;
                if (ParseFloat(Tokens[1], X) && ParseFloat(Tokens[2], Y) && ParseFloat(Tokens[3], Z))
                {
                    // Normalize
                    float Length = std::sqrt(X * X + Y * Y + Z * Z);
                    if (Length > 0.0001f)
                    {
                        X /= Length;
                        Y /= Length;
                        Z /= Length;
                    }
                    
                    Normals.Add(Math::FVector3f(X, Y, Z));
                }
            }
        }
        // Face: f v/vt/vn v/vt/vn v/vt/vn ...
        else if (Command == "f")
        {
            if (Tokens.Num() >= 4)
            {
                // Reconstruct face line without the 'f' command
                String FaceLine;
                for (int32 i = 1; i < Tokens.Num(); ++i)
                {
                    if (i > 1) FaceLine += " ";
                    FaceLine += Tokens[i];
                }
                
                ParseFace(FaceLine, Positions, TexCoords, Normals, 
                         OutBuilder, CurrentMaterial, Options);
            }
        }
        // Material: usemtl name
        else if (Command == "usemtl")
        {
            if (Tokens.Num() >= 2)
            {
                String MatName = Tokens[1];
                
                // Find or create material
                int32* Found = MaterialMap.Find(MatName);
                if (Found)
                {
                    CurrentMaterial = *Found;
                }
                else
                {
                    CurrentMaterial = OutBuilder.GetNumMaterials();
                    OutBuilder.SetNumMaterials(CurrentMaterial + 1);
                    OutBuilder.SetMaterialName(CurrentMaterial, MatName);
                    MaterialMap.Add(MatName, CurrentMaterial);
                    
                    MR_LOG(LogOBJLoader, Verbose, "Added material '%s' at index %d",
                           MatName.c_str(), CurrentMaterial);
                }
            }
        }
        // Object name: o name
        else if (Command == "o" || Command == "g")
        {
            // Object/group names - currently just logged
            if (Tokens.Num() >= 2)
            {
                MR_LOG(LogOBJLoader, Verbose, "Object/Group: %s", Tokens[1].c_str());
            }
        }
        // Material library: mtllib filename
        else if (Command == "mtllib")
        {
            // MTL files not currently supported
            MR_LOG(LogOBJLoader, Verbose, "Material library referenced (not loaded): %s",
                   Tokens.Num() >= 2 ? Tokens[1].c_str() : "unknown");
        }
        // Smoothing group: s on/off/number
        else if (Command == "s")
        {
            // Smoothing groups - affects normal computation
            // Currently handled by global smooth normal setting
        }
    }
    
    // Log statistics
    MR_LOG(LogOBJLoader, Log, "OBJ parsed: %d positions, %d texcoords, %d normals",
           Positions.Num(), TexCoords.Num(), Normals.Num());
    MR_LOG(LogOBJLoader, Log, "Built: %d vertices, %d triangles, %d materials",
           OutBuilder.GetNumVertices(), OutBuilder.GetNumTriangles(), 
           OutBuilder.GetNumMaterials());
    
    // Validate
    if (OutBuilder.GetNumVertices() == 0)
    {
        MR_LOG(LogOBJLoader, Error, "No vertices loaded from OBJ file");
        return EMeshLoadResult::InvalidData;
    }
    
    return EMeshLoadResult::Success;
}

/**
 * Parse a face line
 */
void FOBJMeshLoader::ParseFace(
    const String& Line,
    const TArray<Math::FVector3f>& Positions,
    const TArray<Math::FVector2f>& TexCoords,
    const TArray<Math::FVector3f>& Normals,
    FMeshBuilder& OutBuilder,
    int32 CurrentMaterial,
    const FMeshLoadOptions& Options)
{
    // Split face into vertex specifications
    TArray<String> VertexSpecs = SplitByWhitespace(Line);
    
    if (VertexSpecs.Num() < 3)
    {
        return;
    }
    
    // Parse each vertex specification
    TArray<int32> VertexIndices;
    
    for (const String& Spec : VertexSpecs)
    {
        // Parse v/vt/vn format
        // Can be: v, v/vt, v//vn, v/vt/vn
        TArray<String> Parts = SplitString(Spec, '/');
        
        int32 PosIndex = 0, TexIndex = 0, NormIndex = 0;
        bool HasTex = false, HasNorm = false;
        
        // Position index (required)
        if (Parts.Num() >= 1 && !Parts[0].empty())
        {
            if (!ParseInt(Parts[0], PosIndex))
            {
                continue;
            }
        }
        else
        {
            continue;
        }
        
        // Texture coordinate index (optional)
        if (Parts.Num() >= 2 && !Parts[1].empty())
        {
            if (ParseInt(Parts[1], TexIndex))
            {
                HasTex = true;
            }
        }
        
        // Normal index (optional)
        if (Parts.Num() >= 3 && !Parts[2].empty())
        {
            if (ParseInt(Parts[2], NormIndex))
            {
                HasNorm = true;
            }
        }
        
        // Convert OBJ indices (1-based, can be negative) to 0-based
        // Negative indices are relative to current end of array
        if (PosIndex < 0)
        {
            PosIndex = Positions.Num() + PosIndex + 1;
        }
        PosIndex--; // Convert to 0-based
        
        if (HasTex)
        {
            if (TexIndex < 0)
            {
                TexIndex = TexCoords.Num() + TexIndex + 1;
            }
            TexIndex--;
        }
        
        if (HasNorm)
        {
            if (NormIndex < 0)
            {
                NormIndex = Normals.Num() + NormIndex + 1;
            }
            NormIndex--;
        }
        
        // Validate indices
        if (PosIndex < 0 || PosIndex >= Positions.Num())
        {
            MR_LOG(LogOBJLoader, Warning, "Invalid position index: %d", PosIndex + 1);
            continue;
        }
        
        // Create vertex
        FStaticMeshBuildVertex Vertex;
        Vertex.Position = Positions[PosIndex];
        
        if (HasTex && TexIndex >= 0 && TexIndex < TexCoords.Num())
        {
            Vertex.UVs[0] = TexCoords[TexIndex];
        }
        
        if (HasNorm && NormIndex >= 0 && NormIndex < Normals.Num())
        {
            Vertex.TangentZ = Normals[NormIndex];
            Vertex.SetTangentBasisFromNormal(Vertex.TangentZ);
        }
        
        // Add vertex and store index
        int32 VertIndex = OutBuilder.AddVertex(Vertex);
        VertexIndices.Add(VertIndex);
    }
    
    // Triangulate face (fan triangulation for convex polygons)
    // For a polygon with N vertices: N-2 triangles
    if (VertexIndices.Num() >= 3)
    {
        for (int32 i = 1; i < VertexIndices.Num() - 1; ++i)
        {
            int32 V0 = VertexIndices[0];
            int32 V1 = VertexIndices[i];
            int32 V2 = VertexIndices[i + 1];
            
            // Flip winding if requested
            if (Options.bFlipWindingOrder)
            {
                OutBuilder.AddTriangle(V0, V2, V1, CurrentMaterial);
            }
            else
            {
                OutBuilder.AddTriangle(V0, V1, V2, CurrentMaterial);
            }
        }
    }
}

} // namespace MonsterEngine
