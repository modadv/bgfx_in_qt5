#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class TerrainHeightFieldLoader
{
public:
    struct LoadRequest
    {
        std::string path;
        int width = 0;
        int height = 0;
        std::vector<uint16_t> data;
        float aspectRatio = 1.0f;
        bool success = false;
    };

    TerrainHeightFieldLoader();
    ~TerrainHeightFieldLoader();

    void loadTexture(const std::string& path);
    void stop();
    bool getLoadedTexture(LoadRequest& request);

private:
    struct LoadParams
    {
        std::string path;
    };

    void run();
    LoadRequest loadImageData(const std::string& path) const;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_thread;
    LoadParams m_pendingRequest;
    bool m_hasRequest = false;
    std::atomic<bool> m_shouldStop = false;
    std::deque<LoadRequest> m_loadedRequests;
};
