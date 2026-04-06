#pragma once

#include <bimg/bimg.h>
#include <cstdint>
#include <vector>

namespace terrain
{
bool decodeStandardHeightmap(const bimg::ImageContainer& image, std::vector<uint16_t>& output);
}
