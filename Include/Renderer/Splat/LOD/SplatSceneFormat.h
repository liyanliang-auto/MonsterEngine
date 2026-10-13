// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Renderer/Splat/LOD/SplatLODTypes.h"
namespace MonsterRender::Splat {
/** Bounded, checksummed little-endian scene/page serialization. */
class FSplatSceneFormat {
public:
    static bool LoadManifest(const std::string&, FSplatSceneManifest&, std::string& error);
    static bool SaveManifest(const std::string&, const FSplatSceneManifest&, std::string& error);
    static bool ValidateManifest(const FSplatSceneManifest&, std::string& error);
    static bool DecodePage(const FSplatSceneManifest&, uint32_t pageId, const std::vector<uint8_t>&, std::vector<FSplatGaussian>&, std::string& error);
    static bool EncodePage(const std::vector<FSplatGaussian>&, uint32_t shDegree, std::vector<uint8_t>&, std::string& error);
    static std::string SHA256(const std::vector<uint8_t>&);
    static bool SHA256File(const std::string&, std::string& hash, std::string& error);
};
}
