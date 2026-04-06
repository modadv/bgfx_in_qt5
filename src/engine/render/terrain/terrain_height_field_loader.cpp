#include "terrain_height_field_loader.h"

#include "terrain_heightmap_decoder.h"
#include "logger.h"

#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/error.h>
#include <fstream>
#include <vector>

TerrainHeightFieldLoader::TerrainHeightFieldLoader() = default;

TerrainHeightFieldLoader::~TerrainHeightFieldLoader()
{
    stop();
}

void TerrainHeightFieldLoader::loadTexture(const std::string& path)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingRequest.path = path;
        m_hasRequest = true;
    }

    if (!m_thread.joinable())
    {
        m_thread = std::thread(&TerrainHeightFieldLoader::run, this);
    }

    m_cv.notify_one();
}

void TerrainHeightFieldLoader::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_shouldStop = true;
    }

    m_cv.notify_one();
    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

bool TerrainHeightFieldLoader::getLoadedTexture(LoadRequest& request)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_loadedRequests.empty())
    {
        return false;
    }

    request = std::move(m_loadedRequests.front());
    m_loadedRequests.pop_front();
    return true;
}

void TerrainHeightFieldLoader::run()
{
    while (true)
    {
        LoadParams params;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]() { return m_shouldStop || m_hasRequest; });

            if (m_shouldStop)
            {
                return;
            }

            params = m_pendingRequest;
            m_hasRequest = false;
        }

        LoadRequest request = loadImageData(params.path);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_loadedRequests.push_back(std::move(request));
        }
    }
}

TerrainHeightFieldLoader::LoadRequest TerrainHeightFieldLoader::loadImageData(const std::string& path) const
{
    LoadRequest request;
    request.path = path;

    if (path.empty())
    {
        LOG_W("[TerrainHeightFieldLoader] Empty heightmap path");
        return request;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        LOG_W("[TerrainHeightFieldLoader] Failed to open heightmap file: {}", path);
        return request;
    }

    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size <= 0 || size > 1024LL * 1024 * 1024)
    {
        LOG_W("[TerrainHeightFieldLoader] Invalid heightmap file size: {} bytes for '{}'", size, path);
        return request;
    }

    std::vector<char> buffer(static_cast<size_t>(size));
    if (!file.read(buffer.data(), size))
    {
        LOG_W("[TerrainHeightFieldLoader] Failed to read heightmap file: {}", path);
        return request;
    }

    static thread_local bx::DefaultAllocator threadAllocator;
    bx::Error err;
    bimg::ImageContainer* image = bimg::imageParse(
        &threadAllocator,
        buffer.data(),
        uint32_t(size),
        bimg::TextureFormat::Count,
        &err
    );
    if (image == nullptr)
    {
        LOG_W("[TerrainHeightFieldLoader] Failed to parse heightmap image: {}", path);
        return request;
    }

    request.width = image->m_width;
    request.height = image->m_height;
    request.aspectRatio = image->m_height > 0
        ? float(image->m_width) / float(image->m_height)
        : 1.0f;
    request.success = terrain::decodeStandardHeightmap(*image, request.data);
    if (!request.success)
    {
        LOG_W("[TerrainHeightFieldLoader] Unsupported standard heightmap format {} for '{}'",
              int(image->m_format), path);
    }
    else
    {
        LOG_I("[TerrainHeightFieldLoader] Loaded heightmap: {} ({}x{}, aspect={:.3f}, format={})",
              path, request.width, request.height, request.aspectRatio, int(image->m_format));
    }

    bimg::imageFree(image);
    return request;
}
