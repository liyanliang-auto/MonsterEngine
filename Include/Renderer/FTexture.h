// MonsterEngine - CPU texture streaming descriptor shared by manager and clients.
#pragma once

#include "Core/CoreTypes.h"
#include <algorithm>

namespace MonsterRender {
class FTexture {
public:
    FTexture() = default;
    FTexture(const String& name, uint32 width, uint32 height, uint32 mipLevels)
        : Width(width), Height(height), TotalMipLevels(mipLevels), ResidentMips(1),
          FilePath("Textures/" + name + ".dds"), Name(name) {
        for (uint32 i = 0; i < TotalMipLevels && i < 16; ++i) {
            const uint32 mipWidth = std::max(1u, Width >> i);
            const uint32 mipHeight = std::max(1u, Height >> i);
            SizePerMip[i] = SIZE_T(mipWidth) * mipHeight * 4;
        }
    }

    uint32 Width = 0;
    uint32 Height = 0;
    uint32 TotalMipLevels = 0;
    uint32 ResidentMips = 0;
    SIZE_T SizePerMip[16] = {};
    void* MipData[16] = {};
    String FilePath;
    String Name;

    SIZE_T GetTotalSize() const {
        SIZE_T total = 0;
        for (uint32 i = 0; i < ResidentMips && i < 16; ++i) total += SizePerMip[i];
        return total;
    }
};
}
