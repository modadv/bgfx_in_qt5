#include "terrain_heightmap_decoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace terrain
{
namespace
{
uint16_t expand8To16(uint8_t value)
{
    return uint16_t(value) * 257u;
}

uint16_t grayscaleToUint16(uint8_t r, uint8_t g, uint8_t b)
{
    const uint32_t luminance = 54u * uint32_t(r) + 183u * uint32_t(g) + 19u * uint32_t(b);
    return expand8To16(uint8_t((luminance + 128u) >> 8));
}

bool normalizeFloatSamples(const float* input, size_t pixelCount, std::vector<uint16_t>& output)
{
    if (input == nullptr || pixelCount == 0)
    {
        return false;
    }

    float minValue = std::numeric_limits<float>::infinity();
    float maxValue = -std::numeric_limits<float>::infinity();
    for (size_t i = 0; i < pixelCount; ++i)
    {
        const float value = input[i];
        if (!std::isfinite(value))
        {
            continue;
        }

        minValue = std::min(minValue, value);
        maxValue = std::max(maxValue, value);
    }

    output.assign(pixelCount, 0);
    if (!std::isfinite(minValue) || !std::isfinite(maxValue) || maxValue <= minValue)
    {
        return true;
    }

    const float scale = 65535.0f / (maxValue - minValue);
    for (size_t i = 0; i < pixelCount; ++i)
    {
        const float value = input[i];
        if (!std::isfinite(value))
        {
            continue;
        }

        const float normalized = (value - minValue) * scale;
        const float clamped = std::max(0.0f, std::min(65535.0f, normalized));
        output[i] = uint16_t(clamped + 0.5f);
    }

    return true;
}
}

bool decodeStandardHeightmap(const bimg::ImageContainer& image, std::vector<uint16_t>& output)
{
    if (image.m_data == nullptr || image.m_width == 0 || image.m_height == 0)
    {
        return false;
    }

    const size_t pixelCount = size_t(image.m_width) * size_t(image.m_height);
    const uint8_t* imageData = static_cast<const uint8_t*>(image.m_data);

    switch (image.m_format)
    {
    case bimg::TextureFormat::R16:
    {
        const size_t byteCount = pixelCount * sizeof(uint16_t);
        if (image.m_size < byteCount)
        {
            return false;
        }

        output.resize(pixelCount);
        std::memcpy(output.data(), imageData, byteCount);
        return true;
    }

    case bimg::TextureFormat::R8:
    {
        if (image.m_size < pixelCount)
        {
            return false;
        }

        output.resize(pixelCount);
        for (size_t i = 0; i < pixelCount; ++i)
        {
            output[i] = expand8To16(imageData[i]);
        }
        return true;
    }

    case bimg::TextureFormat::RGB8:
    {
        const size_t byteCount = pixelCount * 3u;
        if (image.m_size < byteCount)
        {
            return false;
        }

        output.resize(pixelCount);
        for (size_t i = 0; i < pixelCount; ++i)
        {
            const size_t base = i * 3u;
            output[i] = grayscaleToUint16(imageData[base + 0], imageData[base + 1], imageData[base + 2]);
        }
        return true;
    }

    case bimg::TextureFormat::RGBA8:
    {
        const size_t byteCount = pixelCount * 4u;
        if (image.m_size < byteCount)
        {
            return false;
        }

        output.resize(pixelCount);
        for (size_t i = 0; i < pixelCount; ++i)
        {
            const size_t base = i * 4u;
            output[i] = grayscaleToUint16(imageData[base + 0], imageData[base + 1], imageData[base + 2]);
        }
        return true;
    }

    case bimg::TextureFormat::BGRA8:
    {
        const size_t byteCount = pixelCount * 4u;
        if (image.m_size < byteCount)
        {
            return false;
        }

        output.resize(pixelCount);
        for (size_t i = 0; i < pixelCount; ++i)
        {
            const size_t base = i * 4u;
            output[i] = grayscaleToUint16(imageData[base + 2], imageData[base + 1], imageData[base + 0]);
        }
        return true;
    }

    case bimg::TextureFormat::R32F:
        return normalizeFloatSamples(reinterpret_cast<const float*>(imageData), pixelCount, output);

    default:
        return false;
    }
}
}
