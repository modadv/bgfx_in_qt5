#include "render/terrain/terrain_heightmap_decoder.h"

#include <bimg/bimg.h>

#include <cstdint>
#include <iostream>
#include <vector>

namespace
{
bimg::ImageContainer makeImage(const void* data,
                               uint32_t size,
                               uint32_t width,
                               uint32_t height,
                               bimg::TextureFormat::Enum format)
{
    bimg::ImageContainer image{};
    image.m_data = const_cast<void*>(data);
    image.m_size = size;
    image.m_width = width;
    image.m_height = height;
    image.m_depth = 1;
    image.m_numLayers = 1;
    image.m_numMips = 1;
    image.m_format = format;
    return image;
}

bool expectEqual(const std::vector<uint16_t>& actual,
                 const std::vector<uint16_t>& expected,
                 const char* label)
{
    if (actual == expected)
    {
        return true;
    }

    std::cerr << label << " failed" << std::endl;
    std::cerr << "expected:";
    for (uint16_t value : expected)
    {
        std::cerr << ' ' << value;
    }
    std::cerr << std::endl;
    std::cerr << "actual:";
    for (uint16_t value : actual)
    {
        std::cerr << ' ' << value;
    }
    std::cerr << std::endl;
    return false;
}
}

int main()
{
    {
        const std::vector<uint16_t> pixels = { 0u, 32768u, 65535u };
        const bimg::ImageContainer image = makeImage(
            pixels.data(),
            uint32_t(pixels.size() * sizeof(uint16_t)),
            3,
            1,
            bimg::TextureFormat::R16
        );

        std::vector<uint16_t> decoded;
        if (!terrain::decodeStandardHeightmap(image, decoded) || !expectEqual(decoded, pixels, "R16 copy"))
        {
            return 1;
        }
    }

    {
        const std::vector<uint8_t> pixels = { 0u, 128u, 255u };
        const bimg::ImageContainer image = makeImage(
            pixels.data(),
            uint32_t(pixels.size()),
            3,
            1,
            bimg::TextureFormat::R8
        );

        const std::vector<uint16_t> expected = { 0u, 32896u, 65535u };
        std::vector<uint16_t> decoded;
        if (!terrain::decodeStandardHeightmap(image, decoded) || !expectEqual(decoded, expected, "R8 expand"))
        {
            return 1;
        }
    }

    {
        const std::vector<uint8_t> pixels = {
            255u, 0u, 0u,
            0u, 255u, 0u
        };
        const bimg::ImageContainer image = makeImage(
            pixels.data(),
            uint32_t(pixels.size()),
            2,
            1,
            bimg::TextureFormat::RGB8
        );

        const std::vector<uint16_t> expected = { 13878u, 46774u };
        std::vector<uint16_t> decoded;
        if (!terrain::decodeStandardHeightmap(image, decoded) || !expectEqual(decoded, expected, "RGB8 grayscale"))
        {
            return 1;
        }
    }

    {
        const std::vector<float> pixels = { 10.0f, 20.0f, 15.0f };
        const bimg::ImageContainer image = makeImage(
            pixels.data(),
            uint32_t(pixels.size() * sizeof(float)),
            3,
            1,
            bimg::TextureFormat::R32F
        );

        const std::vector<uint16_t> expected = { 0u, 65535u, 32768u };
        std::vector<uint16_t> decoded;
        if (!terrain::decodeStandardHeightmap(image, decoded) || !expectEqual(decoded, expected, "R32F normalize"))
        {
            return 1;
        }
    }

    return 0;
}
