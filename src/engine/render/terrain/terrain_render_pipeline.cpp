#include "terrain_render_pipeline.h"
#include "terrain_patch_tables.h"
#include "terrain_constants.h"
#include "logger.h"
#include "common/bgfx_utils.h"
#include "render/core/render_device_bgfx.h"
#include <bimg/decode.h>
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <bx/timer.h>
#include <bx/allocator.h>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>
#include <algorithm>
#include <deque>
#include <memory>
#include <mutex>

namespace {
uint16_t nextPow2(uint16_t value)
{
    uint16_t pow2 = 1;
    while (pow2 < value)
    {
        pow2 <<= 1;
    }
    return pow2;
}

bool screenToLocalPoint(float sx, float sy, float viewW, float viewH, float ndcNear,
                        const float* invViewProj, const float* invModel, bx::Vec3& outLocal)
{
    if (viewW <= 0.0f || viewH <= 0.0f)
    {
        return false;
    }

    const float ndcX = (sx / viewW) * 2.0f - 1.0f;
    const float ndcY = ((viewH - sy) / viewH) * 2.0f - 1.0f;

    const bx::Vec3 p0 = bx::mulH({ ndcX, ndcY, ndcNear }, invViewProj);
    const bx::Vec3 p1 = bx::mulH({ ndcX, ndcY, 1.0f }, invViewProj);

    const bx::Vec3 p0l = bx::mul(p0, invModel);
    const bx::Vec3 p1l = bx::mul(p1, invModel);
    const bx::Vec3 dir = bx::sub(p1l, p0l);

    if (std::fabs(dir.z) < 1.0e-6f)
    {
        return false;
    }

    const float t = -p0l.z / dir.z;
    outLocal = bx::add(p0l, bx::mul(dir, t));
    return true;
}

bool screenToLocalRay(float sx, float sy, float viewW, float viewH, float ndcNear,
                      const float* invViewProj, const float* invModel,
                      bx::Vec3& outOrigin, bx::Vec3& outDir)
{
    if (viewW <= 0.0f || viewH <= 0.0f)
    {
        return false;
    }

    const float ndcX = (sx / viewW) * 2.0f - 1.0f;
    const float ndcY = ((viewH - sy) / viewH) * 2.0f - 1.0f;

    const bx::Vec3 p0 = bx::mulH({ ndcX, ndcY, ndcNear }, invViewProj);
    const bx::Vec3 p1 = bx::mulH({ ndcX, ndcY, 1.0f }, invViewProj);

    outOrigin = bx::mul(p0, invModel);
    const bx::Vec3 p1l = bx::mul(p1, invModel);
    outDir = bx::sub(p1l, outOrigin);

    const float len2 = outDir.x * outDir.x + outDir.y * outDir.y + outDir.z * outDir.z;
    if (len2 <= 1.0e-8f)
    {
        return false;
    }

    return true;
}

bool pointInQuad2D(float px, float py,
                   float x0, float y0,
                   float ux, float uy,
                   float vx, float vy,
                   float& outU, float& outV)
{
    const float wx = px - x0;
    const float wy = py - y0;
    const float denom = ux * vy - uy * vx;
    if (std::fabs(denom) <= 1.0e-8f)
    {
        return false;
    }
    outU = (wx * vy - wy * vx) / denom;
    outV = (ux * wy - uy * wx) / denom;
    return outU >= 0.0f && outU <= 1.0f && outV >= 0.0f && outV <= 1.0f;
}

std::mutex g_orphanedOverlayMutex;
struct OrphanedOverlayReadback
{
    std::vector<float> data;
    uint32_t submitFrame = std::numeric_limits<uint32_t>::max();
};
std::deque<OrphanedOverlayReadback> g_orphanedOverlayReads;

void stashOverlayReadback(std::vector<float>&& data, uint32_t submitFrame)
{
    if (data.empty())
        return;
    std::lock_guard<std::mutex> lock(g_orphanedOverlayMutex);
    g_orphanedOverlayReads.push_back({ std::move(data), submitFrame });
}

void releaseOverlayReadbacks(uint32_t currentFrame)
{
    if (currentFrame == std::numeric_limits<uint32_t>::max())
        return;
    std::lock_guard<std::mutex> lock(g_orphanedOverlayMutex);
    while (!g_orphanedOverlayReads.empty()
        && g_orphanedOverlayReads.front().submitFrame <= currentFrame)
    {
        g_orphanedOverlayReads.pop_front();
    }
}

uint32_t currentFrameId()
{
    const uint32_t frameId = RenderDeviceBgfx::instance().lastFrameId();
    return frameId == 0 ? std::numeric_limits<uint32_t>::max() : frameId;
}

bool probeImageSize(const char* path, uint16_t& outW, uint16_t& outH)
{
    outW = 0;
    outH = 0;
    if (path == nullptr || *path == '\0')
        return false;

    bx::FileReader reader;
    if (!bx::open(&reader, path))
        return false;

    const uint32_t size = uint32_t(bx::getSize(&reader));
    if (size == 0)
    {
        bx::close(&reader);
        return false;
}

    std::vector<uint8_t> data(size);
    const int32_t readSize = bx::read(&reader, data.data(), int32_t(size), bx::ErrorAssert{});
    bx::close(&reader);
    if (readSize <= 0)
        return false;

    bx::DefaultAllocator allocator;
    bimg::ImageContainer* img = bimg::imageParse(&allocator, data.data(), uint32_t(readSize));
    if (!img)
        return false;

    outW = img->m_width;
    outH = img->m_height;
    bimg::imageFree(img);
    return outW > 0 && outH > 0;
}

bool needSwapUvByOrientation(uint16_t diffuseW,
                             uint16_t diffuseH,
                             uint16_t heightFieldW,
                             uint16_t heightFieldH)
{
    if (heightFieldW == 0 || heightFieldH == 0 || diffuseW == 0 || diffuseH == 0)
        return false;

    const bool hmLandscape = heightFieldW > heightFieldH;
    const bool hmPortrait = heightFieldH > heightFieldW;
    const bool dfLandscape = diffuseW > diffuseH;
    const bool dfPortrait = diffuseH > diffuseW;

    // If either image is square-like, keep current mapping and avoid unstable toggles.
    if ((!hmLandscape && !hmPortrait) || (!dfLandscape && !dfPortrait))
        return false;

    // Orientation mismatch means UV axes are opposite and should be swapped.
    return (hmLandscape && dfPortrait) || (hmPortrait && dfLandscape);
}

TerrainRenderPipeline::DiffuseUvMode chooseDiffuseUvMode(uint16_t diffuseW,
                                                     uint16_t diffuseH,
                                                     uint16_t heightFieldW,
                                                     uint16_t heightFieldH)
{
    if (heightFieldW == 0 || heightFieldH == 0 || diffuseW == 0 || diffuseH == 0)
        return TerrainRenderPipeline::DiffuseUvMode::None;

    if (needSwapUvByOrientation(diffuseW, diffuseH, heightFieldW, heightFieldH))
    {
        return TerrainRenderPipeline::DiffuseUvMode::SwapUV;
    }
    return TerrainRenderPipeline::DiffuseUvMode::None;
}
}

TerrainRenderPipeline::TerrainRenderPipeline()
    : m_width(0)
    , m_height(0)
    , m_instancedMeshVertexCount(0)
    , m_instancedMeshPrimitiveCount(0)
    , m_shading(types::PROGRAM_TERRAIN)
    , m_pingPong(0)
    , m_terrainAspectRatio(1.0f)
    , m_primitivePixelLengthTarget(1.0f)
    , m_fovy(60.0f)
    , m_restart(true)
    , m_wireframe(false)
    , m_cull(true)
    , m_freeze(false)
    , m_useGpuSmap(true)
    , m_heightFieldNeedReload(false)
    , m_diffuseNeedReload(false)
    , m_loadStartTime(0)
    , m_firstFrameRendered(false)
    , m_loadTime(0.0f)
    , m_cpuSmapGenTime(0.0f)
    , m_gpuSmapGenTime(0.0f)
    , m_loadHistoryCount(0)
{
    for (uint32_t i = 0; i < types::TEXTURE_COUNT; ++i) {
        m_textures[i] = BGFX_INVALID_HANDLE;
        m_texturesBackup[i] = BGFX_INVALID_HANDLE;
    }
    for (uint32_t i = 0; i < types::PROGRAM_COUNT; ++i) {
        m_programsCompute[i] = BGFX_INVALID_HANDLE;
    }
    for (uint32_t i = 0; i < types::SHADING_COUNT; ++i) {
        m_programsDraw[i] = BGFX_INVALID_HANDLE;
    }
    for (uint32_t i = 0; i < types::SAMPLER_COUNT; ++i) {
        m_samplers[i] = BGFX_INVALID_HANDLE;
    }

    m_bufferSubd[0] = BGFX_INVALID_HANDLE;
    m_bufferSubd[1] = BGFX_INVALID_HANDLE;
    m_bufferCulledSubd = BGFX_INVALID_HANDLE;
    m_bufferCounter = BGFX_INVALID_HANDLE;
    m_geometryIndices = BGFX_INVALID_HANDLE;
    m_geometryVertices = BGFX_INVALID_HANDLE;
    m_instancedGeometryIndices = BGFX_INVALID_HANDLE;
    m_instancedGeometryVertices = BGFX_INVALID_HANDLE;
    m_dispatchIndirect = BGFX_INVALID_HANDLE;
    m_smapParamsHandle = BGFX_INVALID_HANDLE;
    m_smapChunkParamsHandle = BGFX_INVALID_HANDLE;
    m_diffuseUvParamsHandle = BGFX_INVALID_HANDLE;
    m_programRectWire = BGFX_INVALID_HANDLE;
    m_programColor = BGFX_INVALID_HANDLE;
    m_colorLayoutReady = false;
    m_rectWireVertices = BGFX_INVALID_HANDLE;
    m_rectWireIndices = BGFX_INVALID_HANDLE;
    m_rectParamsBuffer = BGFX_INVALID_HANDLE;
    m_rectMaxTexture = BGFX_INVALID_HANDLE;
    m_rectMaxReadTexture = BGFX_INVALID_HANDLE;
    m_rectMaxSampler = BGFX_INVALID_HANDLE;
    m_rectMaxParamsHandle = BGFX_INVALID_HANDLE;
    m_rectViewParamsHandle = BGFX_INVALID_HANDLE;
    m_rectParamsHandle = BGFX_INVALID_HANDLE;
    m_rectSampleParamsHandle = BGFX_INVALID_HANDLE;
    m_rectDebugParamsHandle = BGFX_INVALID_HANDLE;

    // Initialize paths
    m_heightFieldPath[0] = '\0';
    m_diffuseTexturePath[0] = '\0';
    
    m_textureLoader = std::make_unique<TerrainHeightFieldLoader>();
}

TerrainRenderPipeline::~TerrainRenderPipeline() {
    shutdown();
}

bool TerrainRenderPipeline::init(uint32_t width, uint32_t height)
{
    m_width = width;
    m_height = height;
    m_loadStartTime = bx::getHPCounter();
    m_firstFrameRendered = false;

    // 记录当前 bgfx generation
    m_bgfxGeneration = RenderDeviceBgfx::instance().generation();

    try {
        loadPrograms();
        loadBuffers();
        createAtomicCounters();
        
        if (!bgfx::isValid(m_dummySmap)) {
            const bgfx::Memory* mem = bgfx::alloc(4 * sizeof(float));
            float* data = reinterpret_cast<float*>(mem->data);
            data[0] = 0.0f;
            data[1] = 0.0f;
            data[2] = 0.0f;
            data[3] = 0.0f;
            m_dummySmap = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA32F,
                                                BGFX_TEXTURE_NONE, mem);
        }
        m_dispatchIndirect = bgfx::createIndirectBuffer(2);

        m_resourcesValid = true;
        return true;
    }
    catch (...) {
        destroyAllResources();
        return false;
    }
}


bool TerrainRenderPipeline::ensureValidResources()
{
    uint64_t currentGen = RenderDeviceBgfx::instance().generation();

    if (currentGen != m_bgfxGeneration || !m_resourcesValid)
    {
        invalidateAllHandles();

        if (!init(m_width, m_height))
        {
            return false;
        }

        if (m_heightFieldPath[0] != '\0')
        {
            m_heightFieldNeedReload = true;
        }
        if (m_diffuseTexturePath[0] != '\0')
        {
            m_diffuseNeedReload = true;
        }
        
        m_bgfxGeneration = currentGen;
    }
    
    return true;
}

void TerrainRenderPipeline::destroyAllResources()
{
    bool shouldDestroy = RenderDeviceBgfx::instance().isInitialized() && 
                         RenderDeviceBgfx::instance().generation() == m_bgfxGeneration &&
                         m_resourcesValid;

    if (shouldDestroy)
    {
        m_uniforms.destroy();

        for (uint32_t i = 0; i < types::TEXTURE_COUNT; ++i) {
            if (bgfx::isValid(m_textures[i])) {
                bgfx::destroy(m_textures[i]);
            }
            if (bgfx::isValid(m_texturesBackup[i])) {
                bgfx::destroy(m_texturesBackup[i]);
            }
        }

        for (uint32_t i = 0; i < types::SAMPLER_COUNT; ++i) {
            if (bgfx::isValid(m_samplers[i])) {
                bgfx::destroy(m_samplers[i]);
            }
        }

        for (uint32_t i = 0; i < types::PROGRAM_COUNT; ++i) {
            if (bgfx::isValid(m_programsCompute[i])) {
                bgfx::destroy(m_programsCompute[i]);
            }
        }
        for (uint32_t i = 0; i < types::SHADING_COUNT; ++i) {
            if (bgfx::isValid(m_programsDraw[i])) {
                bgfx::destroy(m_programsDraw[i]);
            }
        }

        if (bgfx::isValid(m_dummySmap)) {
            bgfx::destroy(m_dummySmap);
        }
        if (bgfx::isValid(m_bufferCounter)) {
            bgfx::destroy(m_bufferCounter);
        }
        if (bgfx::isValid(m_bufferCulledSubd)) {
            bgfx::destroy(m_bufferCulledSubd);
        }
        for (int i = 0; i < 2; ++i) {
            if (bgfx::isValid(m_bufferSubd[i])) {
                bgfx::destroy(m_bufferSubd[i]);
            }
        }
        if (bgfx::isValid(m_dispatchIndirect)) {
            bgfx::destroy(m_dispatchIndirect);
        }
        if (bgfx::isValid(m_geometryIndices)) {
            bgfx::destroy(m_geometryIndices);
        }
        if (bgfx::isValid(m_geometryVertices)) {
            bgfx::destroy(m_geometryVertices);
        }
        if (bgfx::isValid(m_instancedGeometryIndices)) {
            bgfx::destroy(m_instancedGeometryIndices);
        }
        if (bgfx::isValid(m_instancedGeometryVertices)) {
            bgfx::destroy(m_instancedGeometryVertices);
        }
        if (bgfx::isValid(m_smapParamsHandle)) {
            bgfx::destroy(m_smapParamsHandle);
        }
        if (bgfx::isValid(m_smapChunkParamsHandle)) {
            bgfx::destroy(m_smapChunkParamsHandle);
        }
        if (bgfx::isValid(m_diffuseUvParamsHandle)) {
            bgfx::destroy(m_diffuseUvParamsHandle);
        }
        if (bgfx::isValid(m_programRectWire)) {
            bgfx::destroy(m_programRectWire);
        }
        if (bgfx::isValid(m_programColor)) {
            bgfx::destroy(m_programColor);
        }
        if (bgfx::isValid(m_rectWireVertices)) {
            bgfx::destroy(m_rectWireVertices);
        }
        if (bgfx::isValid(m_rectWireIndices)) {
            bgfx::destroy(m_rectWireIndices);
        }
        if (bgfx::isValid(m_rectParamsBuffer)) {
            bgfx::destroy(m_rectParamsBuffer);
        }
        if (bgfx::isValid(m_rectMaxTexture)) {
            bgfx::destroy(m_rectMaxTexture);
        }
        if (bgfx::isValid(m_rectMaxReadTexture)) {
            bgfx::destroy(m_rectMaxReadTexture);
        }
        if (bgfx::isValid(m_rectMaxSampler)) {
            bgfx::destroy(m_rectMaxSampler);
        }
        if (bgfx::isValid(m_rectMaxParamsHandle)) {
            bgfx::destroy(m_rectMaxParamsHandle);
        }
        if (bgfx::isValid(m_rectViewParamsHandle)) {
            bgfx::destroy(m_rectViewParamsHandle);
        }
        if (bgfx::isValid(m_rectParamsHandle)) {
            bgfx::destroy(m_rectParamsHandle);
        }
        if (bgfx::isValid(m_rectSampleParamsHandle)) {
            bgfx::destroy(m_rectSampleParamsHandle);
        }
        if (bgfx::isValid(m_rectDebugParamsHandle)) {
            bgfx::destroy(m_rectDebugParamsHandle);
        }
    }

    invalidateAllHandles();

    if (m_rectMaxReadPending)
    {
        stashOverlayReadback(std::move(m_rectMaxReadback), m_rectMaxReadSubmitFrame);
        m_rectMaxReadPending = false;
        m_rectMaxReadCancelPending = false;
        m_rectMaxReadRequested = false;
        m_rectMaxReadSubmitFrame = std::numeric_limits<uint32_t>::max();
    }
    else
    {
        m_rectMaxReadback.clear();
    }

    m_textureSwapPending = false;
    m_textureSwapDelay = 0;
}

void TerrainRenderPipeline::invalidateAllHandles()
{
    m_uniforms.invalidate();

    for (uint32_t i = 0; i < types::TEXTURE_COUNT; ++i) {
        m_textures[i] = BGFX_INVALID_HANDLE;
        m_texturesBackup[i] = BGFX_INVALID_HANDLE;
    }

    for (uint32_t i = 0; i < types::SAMPLER_COUNT; ++i) {
        m_samplers[i] = BGFX_INVALID_HANDLE;
    }

    for (uint32_t i = 0; i < types::PROGRAM_COUNT; ++i) {
        m_programsCompute[i] = BGFX_INVALID_HANDLE;
    }

    for (uint32_t i = 0; i < types::SHADING_COUNT; ++i) {
        m_programsDraw[i] = BGFX_INVALID_HANDLE;
    }

    m_dummySmap = BGFX_INVALID_HANDLE;
    m_bufferCounter = BGFX_INVALID_HANDLE;
    m_bufferCulledSubd = BGFX_INVALID_HANDLE;
    m_bufferSubd[0] = BGFX_INVALID_HANDLE;
    m_bufferSubd[1] = BGFX_INVALID_HANDLE;
    m_dispatchIndirect = BGFX_INVALID_HANDLE;
    m_geometryIndices = BGFX_INVALID_HANDLE;
    m_geometryVertices = BGFX_INVALID_HANDLE;
    m_instancedGeometryIndices = BGFX_INVALID_HANDLE;
    m_instancedGeometryVertices = BGFX_INVALID_HANDLE;
    m_smapParamsHandle = BGFX_INVALID_HANDLE;
    m_smapChunkParamsHandle = BGFX_INVALID_HANDLE;
    m_diffuseUvParamsHandle = BGFX_INVALID_HANDLE;
    m_programRectWire = BGFX_INVALID_HANDLE;
    m_programColor = BGFX_INVALID_HANDLE;
    m_colorLayoutReady = false;
    m_rectWireVertices = BGFX_INVALID_HANDLE;
    m_rectWireIndices = BGFX_INVALID_HANDLE;
    m_rectParamsBuffer = BGFX_INVALID_HANDLE;
    m_rectMaxTexture = BGFX_INVALID_HANDLE;
    m_rectMaxReadTexture = BGFX_INVALID_HANDLE;
    m_rectMaxSampler = BGFX_INVALID_HANDLE;
    m_rectMaxParamsHandle = BGFX_INVALID_HANDLE;
    m_rectViewParamsHandle = BGFX_INVALID_HANDLE;
    m_rectParamsHandle = BGFX_INVALID_HANDLE;
    m_rectSampleParamsHandle = BGFX_INVALID_HANDLE;
    m_rectDebugParamsHandle = BGFX_INVALID_HANDLE;
    m_rectBufferCapacity = 0;
    m_rectMaxTextureWidth = 0;
    m_rectMaxTextureCompute = false;
    m_rectComputeDirty = true;
    m_overlayTime = 0.0f;
    m_overlayWorldDirty = false;

    m_resourcesValid = false;
    m_restart = true;
    m_heightFieldReady = false;
    m_overlayDebugAxes = false;
}

void TerrainRenderPipeline::shutdown()
{
    if (m_textureLoader) {
        m_textureLoader->stop();
    }

    destroyAllResources();

    m_width = 0;
    m_height = 0;
    m_frameBuffer = BGFX_INVALID_HANDLE;
    m_viewId = 0;
    m_firstFrameRendered = false;
    m_heightFieldNeedReload = false;
    m_diffuseNeedReload = false;
    m_heightFieldWidth = 0;
    m_heightFieldHeight = 0;
    m_heightFieldMips = 1;
    m_heightFieldReady = false;
    m_smapNeedsRegen = false;
    m_heightFieldCpu.clear();
    m_heightFieldCpuWidth = 0;
    m_heightFieldCpuHeight = 0;
    m_overlayRectsScreen.clear();
    m_overlayRectsWorld.clear();
    m_rectComputeDirty = true;
    m_overlayWorldDirty = false;
    m_rectMaxReadback.clear();
    m_rectMaxReadPending = false;
    m_rectMaxReadRequested = false;
    m_rectMaxReadCancelPending = false;
    m_rectMaxReadSubmitFrame = std::numeric_limits<uint32_t>::max();
}

void TerrainRenderPipeline::resize(uint32_t w, uint32_t h) {
    if (w == 0 || h == 0) return;
    m_width = w;
    m_height = h;
}

bool TerrainRenderPipeline::update(float deltaTime, const float* viewMtx, const float* projMtx)
{
    RenderFrameContext frame{};
    frame.deltaTime = deltaTime;
    if (!beginFrame(frame, viewMtx, projMtx))
    {
        return false;
    }

    setupViewStage(frame, viewMtx, projMtx);
    processStreamingStage();
    processTextureStage(frame);
    processComputeStage();
    processDrawStage();
    return true;
}

bool TerrainRenderPipeline::beginFrame(RenderFrameContext& ctx, const float* viewMtx, const float* projMtx)
{
    if (m_width == 0 || m_height == 0 || viewMtx == nullptr || projMtx == nullptr)
    {
        return false;
    }

    const bool viewChanged = !m_hasViewProj
        || std::memcmp(m_viewMtx, viewMtx, sizeof(m_viewMtx)) != 0
        || std::memcmp(m_projMtx, projMtx, sizeof(m_projMtx)) != 0;
    if (viewChanged)
    {
        std::memcpy(m_viewMtx, viewMtx, sizeof(m_viewMtx));
        std::memcpy(m_projMtx, projMtx, sizeof(m_projMtx));
        m_hasViewProj = true;
    }

    if (!ensureValidResources() || !bgfx::isValid(m_frameBuffer))
    {
        return false;
    }

    ctx.viewId = m_viewId;
    ctx.needTextureReload = (m_heightFieldNeedReload || m_diffuseNeedReload || !m_firstFrameRendered);
    ctx.needSmapRegenerate = (m_smapNeedsRegen && m_useGpuSmap);
    return true;
}

void TerrainRenderPipeline::setupViewStage(const RenderFrameContext& ctx, const float* viewMtx, const float* projMtx)
{
    bgfx::setViewFrameBuffer(ctx.viewId, m_frameBuffer);
    bgfx::setViewRect(ctx.viewId, 0, 0, uint16_t(m_width), uint16_t(m_height));
    bgfx::setViewTransform(ctx.viewId, viewMtx, projMtx);
    bgfx::touch(ctx.viewId);
}

void TerrainRenderPipeline::processStreamingStage()
{
    TerrainHeightFieldLoader::LoadRequest request;
    while (m_textureLoader->getLoadedTexture(request))
    {
        uploadLoadedTexture(std::move(request));
    }

    if (m_textureSwapPending && m_textureSwapDelay > 0)
    {
        --m_textureSwapDelay;
        if (m_textureSwapDelay == 0)
        {
            for (int i = 0; i < types::TEXTURE_COUNT; ++i)
            {
                if (bgfx::isValid(m_texturesBackup[i]))
                {
                    bgfx::destroy(m_texturesBackup[i]);
                    m_texturesBackup[i] = BGFX_INVALID_HANDLE;
                }
            }
            m_textureSwapPending = false;
        }
    }
}

void TerrainRenderPipeline::processTextureStage(RenderFrameContext& ctx)
{
    releaseOverlayReadbacks(currentFrameId());
    m_overlayTime += ctx.deltaTime;

    if (ctx.needTextureReload)
    {
        const int64_t t0 = bx::getHPCounter();
        loadTextures();
        const int64_t t1 = bx::getHPCounter();

        m_loadTime = float((t1 - t0) / double(bx::getHPFrequency()) * 1000.0);
        m_firstFrameRendered = true;
        m_heightFieldNeedReload = false;
        m_diffuseNeedReload = false;

        LoadTimeRecord rec{};
        rec.loadTimeMs = m_loadTime;
        std::strncpy(rec.heightFieldName, m_heightFieldPath, sizeof(rec.heightFieldName));
        rec.heightFieldName[sizeof(rec.heightFieldName) - 1] = '\0';
        std::strncpy(rec.diffuseName, m_diffuseTexturePath, sizeof(rec.diffuseName));
        rec.diffuseName[sizeof(rec.diffuseName) - 1] = '\0';
        rec.timestamp = t1;

        if (m_loadHistoryCount < MAX_LOAD_HISTORY)
        {
            m_loadHistory[m_loadHistoryCount++] = rec;
        }
        else
        {
            for (int i = 1; i < MAX_LOAD_HISTORY; ++i)
            {
                m_loadHistory[i - 1] = m_loadHistory[i];
            }
            m_loadHistory[MAX_LOAD_HISTORY - 1] = rec;
        }
    }

    if (ctx.needSmapRegenerate)
    {
        loadSmapTextureGPU();
        m_smapNeedsRegen = false;
    }
}

void TerrainRenderPipeline::processComputeStage()
{
    if (m_deferSmapUseFrames > 0)
    {
        --m_deferSmapUseFrames;
    }
    updateOverlayGpuData();
}

void TerrainRenderPipeline::processDrawStage()
{
    configureUniforms();
    renderTerrain();
    renderAxes();
    renderOverlayRects();
}

void TerrainRenderPipeline::renderAxes()
{
    if (!m_overlayDebugAxes)
    {
        return;
    }
    if (!bgfx::isValid(m_programColor) || !m_colorLayoutReady)
    {
        return;
    }

    const uint32_t vertexCount = 18;
    if (bgfx::getAvailTransientVertexBuffer(vertexCount, m_colorLayout) < vertexCount)
    {
        return;
    }
    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, vertexCount, m_colorLayout);

    struct AxisVertex
    {
        float x;
        float y;
        float z;
        uint32_t abgr;
    };

    const float axisLen = 0.35f;
    const float axisX = axisLen * m_terrainAspectRatio;
    const float axisY = axisLen;
    const float axisZ = axisLen * 0.5f;
    const float t = 0.01f;

    AxisVertex* v = reinterpret_cast<AxisVertex*>(tvb.data);
    // X axis (red) quad in Y
    v[0] = {0.0f, -t, 0.0f, 0xff0000ff};
    v[1] = {0.0f,  t, 0.0f, 0xff0000ff};
    v[2] = {axisX,  t, 0.0f, 0xff0000ff};
    v[3] = {0.0f, -t, 0.0f, 0xff0000ff};
    v[4] = {axisX,  t, 0.0f, 0xff0000ff};
    v[5] = {axisX, -t, 0.0f, 0xff0000ff};
    // Y axis (green) quad in X
    v[6] = {-t, 0.0f, 0.0f, 0xff00ff00};
    v[7] = { t, 0.0f, 0.0f, 0xff00ff00};
    v[8] = { t, axisY, 0.0f, 0xff00ff00};
    v[9] = {-t, 0.0f, 0.0f, 0xff00ff00};
    v[10]= { t, axisY, 0.0f, 0xff00ff00};
    v[11]= {-t, axisY, 0.0f, 0xff00ff00};
    // Z axis (blue) quad in X
    v[12]= {-t, 0.0f, 0.0f, 0xffff0000};
    v[13]= { t, 0.0f, 0.0f, 0xffff0000};
    v[14]= { t, 0.0f, axisZ, 0xffff0000};
    v[15]= {-t, 0.0f, 0.0f, 0xffff0000};
    v[16]= { t, 0.0f, axisZ, 0xffff0000};
    v[17]= {-t, 0.0f, axisZ, 0xffff0000};

    float model[16];
    buildModelMatrix(model);
    bgfx::setTransform(model);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setState(BGFX_STATE_WRITE_RGB
        | BGFX_STATE_WRITE_A
        | BGFX_STATE_DEPTH_TEST_LESS);
    bgfx::submit(m_viewId, m_programColor);
}

void TerrainRenderPipeline::setGpuSubdivision(int level) {
    if (level != int(m_uniforms.gpuSubd)) {
        m_restart = true;
        m_uniforms.gpuSubd = float(level);
    }
}

void TerrainRenderPipeline::reloadTextures() {
    m_heightFieldNeedReload = m_diffuseNeedReload = true;
}

void TerrainRenderPipeline::setOverlayRects(const std::vector<OverlayRect>& rects)
{
    m_overlayRectsScreen = rects;
    if (m_overlayRectsScreen.size() > std::numeric_limits<uint16_t>::max())
    {
        m_overlayRectsScreen.resize(std::numeric_limits<uint16_t>::max());
    }
    m_overlayRectsWorld.clear();
    m_overlayWorldDirty = true;
    m_rectComputeDirty = true;
    if (m_rectMaxReadPending)
    {
        m_rectMaxReadCancelPending = true;
    }
    else
    {
        m_rectMaxHeights.clear();
    }
    m_rectMaxReadRequested = true;
}

void TerrainRenderPipeline::clearOverlayRects()
{
    m_overlayRectsScreen.clear();
    m_overlayRectsWorld.clear();
    m_overlayWorldDirty = false;
    m_rectComputeDirty = true;
    if (m_rectMaxReadPending)
    {
        m_rectMaxReadCancelPending = true;
    }
    else
    {
        m_rectMaxHeights.clear();
        m_rectMaxReadRequested = false;
    }
}

void TerrainRenderPipeline::setOverlayUseScreenSpace(bool enabled)
{
    if (m_overlayUseScreenSpace == enabled)
    {
        return;
    }

    m_overlayUseScreenSpace = enabled;
    m_overlayWorldDirty = true;
    m_rectComputeDirty = true;
}

void TerrainRenderPipeline::setOverlayPixelScale(float scale)
{
    if (scale <= 0.0f)
    {
        scale = 1.0f;
    }

    if (std::fabs(m_overlayPixelScale - scale) < 0.0001f)
    {
        return;
    }

    m_overlayPixelScale = scale;
    m_overlayWorldDirty = true;
    m_rectComputeDirty = true;
}

void TerrainRenderPipeline::requestOverlayMaxReadback()
{
    if (m_overlayRectsScreen.empty())
    {
        return;
    }
    m_rectMaxReadRequested = true;
}

bool TerrainRenderPipeline::processOverlayMaxReadback(uint32_t frameId)
{
    if (!m_rectMaxReadPending)
    {
        return false;
    }

    if (frameId < m_rectMaxReadFrame)
    {
        return false;
    }

    if (m_rectMaxReadCancelPending)
    {
        m_rectMaxHeights.clear();
        m_rectMaxReadPending = false;
        m_rectMaxReadCancelPending = false;
        return true;
    }

    if (m_rectMaxReadCount == 0 || m_rectMaxReadback.empty())
    {
        m_rectMaxHeights.clear();
        m_rectMaxReadPending = false;
        return true;
    }

    const size_t count = std::min<size_t>(m_rectMaxReadCount, m_rectMaxReadback.size());
    m_rectMaxHeights.assign(m_rectMaxReadback.begin(), m_rectMaxReadback.begin() + count);
    m_rectMaxReadPending = false;
    return true;
}

bool TerrainRenderPipeline::overlayMaxReady() const
{
    const size_t rectCount = m_overlayRectsWorld.size();
    return rectCount > 0 && m_rectMaxHeights.size() >= rectCount;
}

bool TerrainRenderPipeline::getOverlayRectWorldBounds(int rectId,
                                                  float& outCenterX,
                                                  float& outCenterY,
                                                  float& outCenterZ,
                                                  float& outWidth,
                                                  float& outHeight,
                                                  float& outNormalX,
                                                  float& outNormalY,
                                                  float& outNormalZ) const
{
    const size_t worldCount = m_overlayRectsWorld.size();
    const size_t screenCount = m_overlayRectsScreen.size();
    for (const auto& rect : m_overlayRectsWorld)
    {
        if (rect.id != rectId)
            continue;

        const float ux = rect.ux;
        const float uy = rect.uy;
        const float vx = rect.vx;
        const float vy = rect.vy;
        outWidth = std::sqrt(ux * ux + uy * uy);
        outHeight = std::sqrt(vx * vx + vy * vy);
        outCenterX = rect.x + 0.5f * (ux + vx);
        outCenterY = rect.y + 0.5f * (uy + vy);
        outCenterZ = 0.0f;

        const float nx = uy * 0.0f - 0.0f * vy;
        const float ny = 0.0f * vx - ux * 0.0f;
        const float nz = ux * vy - uy * vx;
        const float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (nlen > 1.0e-6f)
        {
            outNormalX = nx / nlen;
            outNormalY = ny / nlen;
            outNormalZ = nz / nlen;
        }
        else
        {
            outNormalX = 0.0f;
            outNormalY = 0.0f;
            outNormalZ = 1.0f;
        }
        return true;
    }

    if (m_overlayUseScreenSpace)
    {
        LOG_D("[TerrainRenderPipeline] Focus rect id={} not found (screen space, world={}, screen={})",
              rectId, worldCount, screenCount);
        return false;
    }
    if (m_heightFieldWidth == 0 || m_heightFieldHeight == 0)
    {
        LOG_D("[TerrainRenderPipeline] Focus rect id={} blocked (heightField not ready)", rectId);
        return false;
    }

    const float pixelW = float(m_heightFieldWidth);
    const float pixelH = float(m_heightFieldHeight);
    const float invW = 1.0f / pixelW;
    const float invH = 1.0f / pixelH;

    float model[16];
    buildModelMatrix(model);
    auto transformNormal = [&](float x, float y, float z, float& ox, float& oy, float& oz) {
        ox = model[0] * x + model[4] * y + model[8] * z;
        oy = model[1] * x + model[5] * y + model[9] * z;
        oz = model[2] * x + model[6] * y + model[10] * z;
    };

    auto transformPoint = [&](float x, float y, float z, float& ox, float& oy, float& oz) {
        ox = model[0] * x + model[4] * y + model[8] * z + model[12];
        oy = model[1] * x + model[5] * y + model[9] * z + model[13];
        oz = model[2] * x + model[6] * y + model[10] * z + model[14];
    };

    for (const auto& rect : m_overlayRectsScreen)
    {
        if (rect.id != rectId)
            continue;

        float x0 = rect.x;
        float y0 = rect.y;
        float x1 = rect.x + rect.width;
        float y1 = rect.y + rect.height;

        if (rect.coordType == OverlayCoordType::NormalizedCenter)
        {
            const float centerX = rect.x * pixelW;
            const float centerY = rect.y * pixelH;
            const float rectW = rect.width * pixelW;
            const float rectH = rect.height * pixelH;
            x0 = centerX - rectW * 0.5f;
            y0 = centerY - rectH * 0.5f;
            x1 = centerX + rectW * 0.5f;
            y1 = centerY + rectH * 0.5f;
        }
        else if (rect.coordType == OverlayCoordType::PixelCenter)
        {
            const float baseW = rect.imageWidth > 0.0f ? rect.imageWidth : pixelW;
            const float baseH = rect.imageHeight > 0.0f ? rect.imageHeight : pixelH;
            if (baseW > 0.0f && baseH > 0.0f)
            {
                const float centerX = (rect.x / baseW + 0.5f) * pixelW;
                const float centerY = (rect.y / baseH + 0.5f) * pixelH;
                const float rectW = (rect.width / baseW) * pixelW;
                const float rectH = (rect.height / baseH) * pixelH;
                x0 = centerX - rectW * 0.5f;
                y0 = centerY - rectH * 0.5f;
                x1 = centerX + rectW * 0.5f;
                y1 = centerY + rectH * 0.5f;
            }
        }

        const float centerX = (x0 + x1) * 0.5f;
        const float centerY = (y0 + y1) * 0.5f;
        const float rectW = std::fabs(x1 - x0);
        const float rectH = std::fabs(y1 - y0);
        if (rectW <= 1.0e-6f || rectH <= 1.0e-6f)
            return false;

        const float centerU = centerX * invW;
        const float centerV = centerY * invH;
        const float localX = (centerU * 2.0f - 1.0f) * m_terrainAspectRatio;
        const float localY = (centerV * 2.0f - 1.0f);
        const float localW = rectW * invW * 2.0f * m_terrainAspectRatio;
        const float localH = rectH * invH * 2.0f;

        float cx = 0.0f;
        float cy = 0.0f;
        float cz = 0.0f;
        transformPoint(localX, localY, 0.0f, cx, cy, cz);

        float wx1 = 0.0f;
        float wy1 = 0.0f;
        float wz1 = 0.0f;
        float wx2 = 0.0f;
        float wy2 = 0.0f;
        float wz2 = 0.0f;
        float hx1 = 0.0f;
        float hy1 = 0.0f;
        float hz1 = 0.0f;
        float hx2 = 0.0f;
        float hy2 = 0.0f;
        float hz2 = 0.0f;

        transformPoint(localX + localW * 0.5f, localY, 0.0f, wx1, wy1, wz1);
        transformPoint(localX - localW * 0.5f, localY, 0.0f, wx2, wy2, wz2);
        transformPoint(localX, localY + localH * 0.5f, 0.0f, hx1, hy1, hz1);
        transformPoint(localX, localY - localH * 0.5f, 0.0f, hx2, hy2, hz2);

        const float wdx = wx1 - wx2;
        const float wdy = wy1 - wy2;
        const float wdz = wz1 - wz2;
        const float hdx = hx1 - hx2;
        const float hdy = hy1 - hy2;
        const float hdz = hz1 - hz2;

        float nnx = 0.0f;
        float nny = 0.0f;
        float nnz = 0.0f;
        transformNormal(0.0f, 0.0f, 1.0f, nnx, nny, nnz);
        const float nlen = std::sqrt(nnx * nnx + nny * nny + nnz * nnz);
        if (nlen > 1.0e-6f)
        {
            outNormalX = nnx / nlen;
            outNormalY = nny / nlen;
            outNormalZ = nnz / nlen;
        }
        else
        {
            outNormalX = 0.0f;
            outNormalY = 0.0f;
            outNormalZ = 1.0f;
        }

        outCenterX = cx;
        outCenterY = cy;
        outCenterZ = cz;
        outWidth = std::sqrt(wdx * wdx + wdy * wdy + wdz * wdz);
        outHeight = std::sqrt(hdx * hdx + hdy * hdy + hdz * hdz);
        return true;
    }

    LOG_D("[TerrainRenderPipeline] Focus rect id={} not found (world={}, screen={})",
          rectId, worldCount, screenCount);
    return false;
}

bool TerrainRenderPipeline::getOverlayRectNearestEdgeTargetYaw(int rectId, float& outYawDeg) const
{
    const float pixelW = float(m_heightFieldWidth);
    const float pixelH = float(m_heightFieldHeight);
    if (pixelW <= 0.0f || pixelH <= 0.0f)
    {
        return false;
    }

    for (const auto& rect : m_overlayRectsScreen)
    {
        if (rect.id != rectId)
        {
            continue;
        }

        float x0 = rect.x;
        float y0 = rect.y;
        float x1 = rect.x + rect.width;
        float y1 = rect.y + rect.height;

        if (rect.coordType == OverlayCoordType::NormalizedCenter)
        {
            const float centerX = rect.x * pixelW;
            const float centerY = rect.y * pixelH;
            const float rectW = rect.width * pixelW;
            const float rectH = rect.height * pixelH;
            x0 = centerX - rectW * 0.5f;
            y0 = centerY - rectH * 0.5f;
            x1 = centerX + rectW * 0.5f;
            y1 = centerY + rectH * 0.5f;
        }
        else if (rect.coordType == OverlayCoordType::PixelCenter)
        {
            const float baseW = rect.imageWidth > 0.0f ? rect.imageWidth : pixelW;
            const float baseH = rect.imageHeight > 0.0f ? rect.imageHeight : pixelH;
            if (baseW > 0.0f && baseH > 0.0f)
            {
                const float centerX = (rect.x / baseW + 0.5f) * pixelW;
                const float centerY = (rect.y / baseH + 0.5f) * pixelH;
                const float rectW = (rect.width / baseW) * pixelW;
                const float rectH = (rect.height / baseH) * pixelH;
                x0 = centerX - rectW * 0.5f;
                y0 = centerY - rectH * 0.5f;
                x1 = centerX + rectW * 0.5f;
                y1 = centerY + rectH * 0.5f;
            }
        }

        const float minX = std::min(x0, x1);
        const float maxX = std::max(x0, x1);
        const float minY = std::min(y0, y1);
        const float maxY = std::max(y0, y1);
        const float distLeft = minX;
        const float distRight = pixelW - maxX;
        const float distTop = minY;
        const float distBottom = pixelH - maxY;

        float localNx = -1.0f;
        float localNy = 0.0f;
        float minDist = distLeft;

        if (distRight < minDist)
        {
            minDist = distRight;
            localNx = 1.0f;
            localNy = 0.0f;
        }
        if (distTop < minDist)
        {
            minDist = distTop;
            localNx = 0.0f;
            localNy = -1.0f;
        }
        if (distBottom < minDist)
        {
            localNx = 0.0f;
            localNy = 1.0f;
        }

        const float rotRad = bx::toRad(m_imageRotation);
        const float c = std::cos(rotRad);
        const float s = std::sin(rotRad);

        const float nxRot = c * localNx - s * localNy;
        const float nyRot = s * localNx + c * localNy;

        // Local XY plane becomes world XZ plane after the renderer's -90deg X tilt.
        const float normalWorldX = nxRot;
        const float normalWorldZ = -nyRot;

        // Camera forward should point opposite to the outward side normal.
        const float desiredForwardX = -normalWorldX;
        const float desiredForwardZ = -normalWorldZ;
        outYawDeg = bx::toDeg(std::atan2(desiredForwardX, desiredForwardZ));
        return true;
    }

    return false;
}

bool TerrainRenderPipeline::getAlgorithmDenseSideTargetYaw(float& outYawDeg, int& outRectId) const
{
    outYawDeg = 0.0f;
    outRectId = -1;

    const float pixelW = float(m_heightFieldWidth);
    const float pixelH = float(m_heightFieldHeight);
    if (pixelW <= 0.0f || pixelH <= 0.0f)
    {
        return false;
    }

    enum Side : int { Left = 0, Right = 1, Top = 2, Bottom = 3, SideCount = 4 };
    float sideScore[SideCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float sideBestDist[SideCount] = { FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX };
    int sideBestRectId[SideCount] = { -1, -1, -1, -1 };

    int algorithmRectCount = 0;
    for (const auto& rect : m_overlayRectsScreen)
    {
        // Algorithm rect IDs are stable negative values in NGViewModel.
        if (rect.id >= 0)
        {
            continue;
        }

        float x0 = rect.x;
        float y0 = rect.y;
        float x1 = rect.x + rect.width;
        float y1 = rect.y + rect.height;

        if (rect.coordType == OverlayCoordType::NormalizedCenter)
        {
            const float centerX = rect.x * pixelW;
            const float centerY = rect.y * pixelH;
            const float rectW = rect.width * pixelW;
            const float rectH = rect.height * pixelH;
            x0 = centerX - rectW * 0.5f;
            y0 = centerY - rectH * 0.5f;
            x1 = centerX + rectW * 0.5f;
            y1 = centerY + rectH * 0.5f;
        }
        else if (rect.coordType == OverlayCoordType::PixelCenter)
        {
            const float baseW = rect.imageWidth > 0.0f ? rect.imageWidth : pixelW;
            const float baseH = rect.imageHeight > 0.0f ? rect.imageHeight : pixelH;
            if (baseW > 0.0f && baseH > 0.0f)
            {
                const float centerX = (rect.x / baseW + 0.5f) * pixelW;
                const float centerY = (rect.y / baseH + 0.5f) * pixelH;
                const float rectW = (rect.width / baseW) * pixelW;
                const float rectH = (rect.height / baseH) * pixelH;
                x0 = centerX - rectW * 0.5f;
                y0 = centerY - rectH * 0.5f;
                x1 = centerX + rectW * 0.5f;
                y1 = centerY + rectH * 0.5f;
            }
        }

        const float minX = std::min(x0, x1);
        const float maxX = std::max(x0, x1);
        const float minY = std::min(y0, y1);
        const float maxY = std::max(y0, y1);
        const float rectW = std::fabs(maxX - minX);
        const float rectH = std::fabs(maxY - minY);
        if (rectW <= 1.0e-6f || rectH <= 1.0e-6f)
        {
            continue;
        }

        ++algorithmRectCount;

        const float distLeft = minX;
        const float distRight = pixelW - maxX;
        const float distTop = minY;
        const float distBottom = pixelH - maxY;

        Side nearestSide = Left;
        float nearestDist = distLeft;
        if (distRight < nearestDist) { nearestDist = distRight; nearestSide = Right; }
        if (distTop < nearestDist) { nearestDist = distTop; nearestSide = Top; }
        if (distBottom < nearestDist) { nearestDist = distBottom; nearestSide = Bottom; }

        // Density weight: closer to model edge => larger contribution.
        const float closeness = 1.0f / (nearestDist + 1.0f);

        // Orientation weight: favor rectangles whose edge direction follows the side tangent.
        const float angleRad = bx::toRad(rect.angle);
        const float ux = std::cos(angleRad);
        const float uy = std::sin(angleRad);
        const float vx = -std::sin(angleRad);
        const float vy = std::cos(angleRad);

        float tangentAlign = 1.0f;
        if (nearestSide == Left || nearestSide == Right)
        {
            tangentAlign = std::max(std::fabs(uy), std::fabs(vy)); // vertical tangent
        }
        else
        {
            tangentAlign = std::max(std::fabs(ux), std::fabs(vx)); // horizontal tangent
        }

        const float score = closeness * (0.7f + 0.3f * tangentAlign);
        sideScore[int(nearestSide)] += score;

        if (nearestDist < sideBestDist[int(nearestSide)])
        {
            sideBestDist[int(nearestSide)] = nearestDist;
            sideBestRectId[int(nearestSide)] = rect.id;
        }
    }

    if (algorithmRectCount <= 0)
    {
        return false;
    }

    Side bestSide = Left;
    float bestScore = sideScore[int(bestSide)];
    for (int i = 1; i < SideCount; ++i)
    {
        if (sideScore[i] > bestScore)
        {
            bestScore = sideScore[i];
            bestSide = Side(i);
        }
    }

    if (bestScore <= 0.0f)
    {
        return false;
    }

    float localNx = -1.0f;
    float localNy = 0.0f;
    switch (bestSide)
    {
    case Left:   localNx = -1.0f; localNy =  0.0f; break;
    case Right:  localNx =  1.0f; localNy =  0.0f; break;
    case Top:    localNx =  0.0f; localNy = -1.0f; break;
    case Bottom: localNx =  0.0f; localNy =  1.0f; break;
    default: break;
    }

    const float rotRad = bx::toRad(m_imageRotation);
    const float c = std::cos(rotRad);
    const float s = std::sin(rotRad);

    const float nxRot = c * localNx - s * localNy;
    const float nyRot = s * localNx + c * localNy;

    // Local XY plane becomes world XZ plane after renderer tilt.
    const float normalWorldX = nxRot;
    const float normalWorldZ = -nyRot;
    const float desiredForwardX = -normalWorldX;
    const float desiredForwardZ = -normalWorldZ;

    outYawDeg = bx::toDeg(std::atan2(desiredForwardX, desiredForwardZ));
    outRectId = sideBestRectId[int(bestSide)];

    LOG_I("[TerrainRenderPipeline] Dense algorithm side target: side={}, score={:.5f}, rectId={}, yaw={:.3f}",
          int(bestSide), bestScore, outRectId, outYawDeg);
    return true;
}

bool TerrainRenderPipeline::hasOverlayRects() const
{
    return !m_overlayRectsScreen.empty();
}

int TerrainRenderPipeline::pickOverlayRect(float sx, float sy) const
{
    if (m_overlayUseScreenSpace)
    {
        return -1;
    }

    if (!m_hasViewProj || m_width == 0 || m_height == 0)
    {
        return -1;
    }

    if (m_overlayRectsWorld.empty() || m_rectMaxHeights.size() < m_overlayRectsWorld.size())
    {
        return -1;
    }

    const bgfx::Caps* caps = bgfx::getCaps();
    const float ndcNear = (caps && caps->homogeneousDepth) ? 0.0f : -1.0f;

    float viewProj[16];
    float invViewProj[16];
    float model[16];
    float invModel[16];
    bx::mtxMul(viewProj, m_viewMtx, m_projMtx);
    bx::mtxInverse(invViewProj, viewProj);
    buildModelMatrix(model);
    bx::mtxInverse(invModel, model);

    bx::Vec3 origin = { 0.0f, 0.0f, 0.0f };
    bx::Vec3 dir = { 0.0f, 0.0f, 0.0f };
    if (!screenToLocalRay(sx, sy, float(m_width), float(m_height), ndcNear, invViewProj, invModel, origin, dir))
    {
        return -1;
    }

    const float dirZ = dir.z;
    if (std::fabs(dirZ) <= 1.0e-6f)
    {
        return -1;
    }

    float bestT = std::numeric_limits<float>::max();
    int bestId = -1;

    const size_t rectCount = m_overlayRectsWorld.size();
    for (size_t i = 0; i < rectCount; ++i)
    {
        const OverlayQuad& rect = m_overlayRectsWorld[i];
        const float height = m_rectMaxHeights[i];
        if (!std::isfinite(height))
        {
            continue;
        }

        const float t = (height - origin.z) / dirZ;
        if (t < 0.0f || t >= bestT)
        {
            continue;
        }

        const bx::Vec3 hit = bx::add(origin, bx::mul(dir, t));
        float u = 0.0f;
        float v = 0.0f;
        if (!pointInQuad2D(hit.x, hit.y, rect.x, rect.y, rect.ux, rect.uy, rect.vx, rect.vy, u, v))
        {
            continue;
        }

        bestT = t;
        bestId = rect.id;
    }

    return bestId;
}

void TerrainRenderPipeline::setImageTransform(float rotationDeg, float scaleX, float scaleY)
{
    if (std::fabs(scaleX) < 1.0e-6f)
    {
        scaleX = 1.0f;
    }
    if (std::fabs(scaleY) < 1.0e-6f)
    {
        scaleY = 1.0f;
    }

    const bool sameRotation = std::fabs(m_imageRotation - rotationDeg) < 0.001f;
    const bool sameScaleX = std::fabs(m_imageScaleX - scaleX) < 0.001f;
    const bool sameScaleY = std::fabs(m_imageScaleY - scaleY) < 0.001f;
    if (sameRotation && sameScaleX && sameScaleY)
    {
        return;
    }

    m_imageRotation = rotationDeg;
    m_imageScaleX = scaleX;
    m_imageScaleY = scaleY;
    m_overlayWorldDirty = true;
    m_rectComputeDirty = true;
}

void TerrainRenderPipeline::buildModelMatrix(float* out) const
{
    float scale[16];
    float rotZ[16];
    float rotX[16];
    float temp[16];

    bx::mtxScale(scale, m_imageScaleX, m_imageScaleY, 1.0f);
    bx::mtxRotateZ(rotZ, bx::toRad(m_imageRotation));
    bx::mtxRotateX(rotX, bx::toRad(-90.0f));

    bx::mtxMul(temp, scale, rotZ);
    bx::mtxMul(out, temp, rotX);
}

void TerrainRenderPipeline::loadPrograms() {
    m_samplers[types::TERRAIN_DMAP_SAMPLER] = bgfx::createUniform("u_DmapSampler", bgfx::UniformType::Sampler);
    m_samplers[types::TERRAIN_SMAP_SAMPLER] = bgfx::createUniform("u_SmapSampler", bgfx::UniformType::Sampler);
    m_samplers[types::TERRAIN_DIFFUSE_SAMPLER] = bgfx::createUniform("u_DiffuseSampler", bgfx::UniformType::Sampler);

    m_uniforms.init();

    m_smapParamsHandle = bgfx::createUniform("u_smapParams", bgfx::UniformType::Vec4);
    m_smapChunkParamsHandle = bgfx::createUniform("u_smapChunkParams", bgfx::UniformType::Vec4);
    m_diffuseUvParamsHandle = bgfx::createUniform("u_diffuseUvParams", bgfx::UniformType::Vec4);

    m_rectMaxSampler = bgfx::createUniform("u_rectMaxSampler", bgfx::UniformType::Sampler);
    m_rectMaxParamsHandle = bgfx::createUniform("u_rectMaxParams", bgfx::UniformType::Vec4);
    m_rectViewParamsHandle = bgfx::createUniform("u_rectViewParams", bgfx::UniformType::Vec4);
    m_rectParamsHandle = bgfx::createUniform("u_rectParams", bgfx::UniformType::Vec4);
    m_rectSampleParamsHandle = bgfx::createUniform("u_rectSampleParams", bgfx::UniformType::Vec4);
    m_rectDebugParamsHandle = bgfx::createUniform("u_rectDebugParams", bgfx::UniformType::Vec4);

    m_programsDraw[types::PROGRAM_TERRAIN] = loadProgram("vs_terrain_render", "fs_terrain_render");
    m_programsDraw[types::PROGRAM_TERRAIN_NORMAL] = loadProgram("vs_terrain_render", "fs_terrain_render_normal");

    m_programsCompute[types::PROGRAM_SUBD_CS_LOD] = bgfx::createProgram(loadShader("cs_terrain_lod"), true);
    m_programsCompute[types::PROGRAM_UPDATE_INDIRECT] = bgfx::createProgram(loadShader("cs_terrain_update_indirect"), true);
    m_programsCompute[types::PROGRAM_UPDATE_DRAW] = bgfx::createProgram(loadShader("cs_terrain_update_draw"), true);
    m_programsCompute[types::PROGRAM_INIT_INDIRECT] = bgfx::createProgram(loadShader("cs_terrain_init"), true);
    m_programsCompute[types::PROGRAM_GENERATE_SMAP] = bgfx::createProgram(loadShader("cs_generate_smap"), true);
    m_programsCompute[types::PROGRAM_RECT_MAX] = bgfx::createProgram(loadShader("cs_rect_max_height"), true);
    
    m_programRectWire = loadProgram("vs_rect_wire", "fs_rect_wire");
    m_programColor = loadProgram("vs_color", "fs_color");

    m_colorLayout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true, true)
        .end();
    m_colorLayoutReady = true;
}

void TerrainRenderPipeline::loadTextures() {
    loadDmapTexture();
    if (m_useGpuSmap) {
        loadSmapTextureGPU();
    } else {
        loadSmapTexture();
    }
    loadDiffuseTexture();
}

void TerrainRenderPipeline::loadBuffers() {
    loadSubdivisionBuffers();
    loadGeometryBuffers();
    loadInstancedGeometryBuffers();
    loadOverlayBuffers();
}

void TerrainRenderPipeline::createAtomicCounters() {
    m_bufferCounter = bgfx::createDynamicIndexBuffer(3, BGFX_BUFFER_INDEX32 | BGFX_BUFFER_COMPUTE_READ_WRITE);
}

void TerrainRenderPipeline::loadSmapTexture() {
    int64_t startTime = bx::getHPCounter();

    const bgfx::Memory* mem = bgfx::alloc(4 * sizeof(float));
    float* defaultSlopeData = (float*)mem->data;
    defaultSlopeData[0] = 0.0f;
    defaultSlopeData[1] = 0.0f;
    defaultSlopeData[2] = 0.0f;
    defaultSlopeData[3] = 0.0f;

    m_textures[types::TEXTURE_SMAP] = bgfx::createTexture2D(
        1, 1, false, 1, bgfx::TextureFormat::RGBA32F,
        BGFX_TEXTURE_NONE, mem
    );

    int64_t endTime = bx::getHPCounter();
    m_cpuSmapGenTime = float((endTime - startTime) / double(bx::getHPFrequency()) * 1000.0);
    printf("CPU SMap generation time: %.2f ms\n", m_cpuSmapGenTime);
}

void TerrainRenderPipeline::loadSmapTextureGPU() {
    int64_t startTime = bx::getHPCounter();
    
    if (!bgfx::isValid(m_textures[types::TEXTURE_DMAP]) || m_heightFieldWidth == 0 || m_heightFieldHeight == 0) {
        const bgfx::Memory* mem = bgfx::alloc(4 * sizeof(float));
        float* defaultSlopeData = (float*)mem->data;
        defaultSlopeData[0] = 0.0f;
        defaultSlopeData[1] = 0.0f;
        defaultSlopeData[2] = 0.0f;
        defaultSlopeData[3] = 0.0f;

        bgfx::TextureHandle newSmapTexture = bgfx::createTexture2D(
            1, 1, false, 1, bgfx::TextureFormat::RGBA32F,
            BGFX_TEXTURE_NONE, mem
        );

        if (bgfx::isValid(m_textures[types::TEXTURE_SMAP])) {
            m_texturesBackup[types::TEXTURE_SMAP] = m_textures[types::TEXTURE_SMAP];
            m_textureSwapPending = true;
            m_textureSwapDelay = 5;
        }
        
        m_textures[types::TEXTURE_SMAP] = newSmapTexture;
        m_gpuSmapGenTime = 0.0f;
        return;
    }

    uint16_t w = m_heightFieldWidth;
    uint16_t h = m_heightFieldHeight;
    int mipcnt = m_heightFieldMips;

    LOG_I("[TerrainRenderPipeline] GPU SMap generation start ({}x{})", w, h);
    LOG_I("[TerrainRenderPipeline] GPU SMap params: dmapFactor={:.4f}, smapFormat=RGBA32F", m_dmapConfig.scale);

    bgfx::TextureHandle newSmapTexture = bgfx::createTexture2D(
        w, h, mipcnt > 1, 1, bgfx::TextureFormat::RGBA32F,
        BGFX_TEXTURE_COMPUTE_WRITE
    );

    float smapParams[4] = { (float)w, (float)h, m_terrainAspectRatio, 0.0f };
    bgfx::setUniform(m_smapParamsHandle, smapParams);
    if (bgfx::isValid(m_smapChunkParamsHandle)) {
        float smapChunkParams[4] = { 0.0f, 0.0f, float(w), float(h) };
        bgfx::setUniform(m_smapChunkParamsHandle, smapChunkParams);
    }

    bgfx::setTexture(0, m_samplers[types::TERRAIN_DMAP_SAMPLER], 
        m_textures[types::TEXTURE_DMAP], 
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);

    if (bgfx::isValid(m_dummySmap)) {
        bgfx::setTexture(1, m_samplers[types::TERRAIN_SMAP_SAMPLER], 
            m_dummySmap, 
            BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC);
    }

    bgfx::setImage(1, newSmapTexture, 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA32F);

    uint16_t groupsX = (w + 15) / 16;
    uint16_t groupsY = (h + 15) / 16;

    const uint8_t viewId = m_viewId;
    bgfx::dispatch(viewId, m_programsCompute[types::PROGRAM_GENERATE_SMAP], groupsX, groupsY, 1);

    if (bgfx::isValid(m_textures[types::TEXTURE_SMAP])) {
        m_texturesBackup[types::TEXTURE_SMAP] = m_textures[types::TEXTURE_SMAP];
    }
    
    m_textures[types::TEXTURE_SMAP] = newSmapTexture;
    
    m_textureSwapPending = true;
    m_textureSwapDelay = 5;
    m_deferSmapUseFrames = 3;

    int64_t endTime = bx::getHPCounter();
    m_gpuSmapGenTime = float((endTime - startTime) / double(bx::getHPFrequency()) * 1000.0);
    LOG_I("[TerrainRenderPipeline] GPU SMap generation done in {:.2f} ms", m_gpuSmapGenTime);
}

void TerrainRenderPipeline::loadDiffuseTexture() {
    const char* filePath = m_diffuseTexturePath;
    
    if (!filePath || filePath[0] == '\0') {
        const bgfx::Memory* mem = bgfx::alloc(4);
        uint8_t* data = mem->data;
        data[0] = data[1] = data[2] = 128;
        data[3] = 255;

        bgfx::TextureHandle newDiffuseTexture = bgfx::createTexture2D(
            1, 1, false, 1, bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_NONE, mem
        );
        
        if (bgfx::isValid(m_textures[types::TEXTURE_DIFFUSE])) {
            m_texturesBackup[types::TEXTURE_DIFFUSE] = m_textures[types::TEXTURE_DIFFUSE];
            m_textureSwapPending = true;
            m_textureSwapDelay = 5;
        }
        
        m_textures[types::TEXTURE_DIFFUSE] = newDiffuseTexture;
        m_diffuseUvMode = DiffuseUvMode::None;
        return;
    }
    
    uint64_t textureFlags = BGFX_TEXTURE_NONE 
        | BGFX_SAMPLER_UVW_BORDER 
        | BGFX_SAMPLER_MIN_ANISOTROPIC 
        | BGFX_SAMPLER_MAG_ANISOTROPIC 
        | BGFX_SAMPLER_MIP_SHIFT;

    bgfx::TextureHandle newDiffuseTexture = loadTexture(filePath, textureFlags);

    if (!bgfx::isValid(newDiffuseTexture)) {
        BX_TRACE("Failed to load diffuse texture: %s, using default texture", filePath);

        const bgfx::Memory* mem = bgfx::alloc(4);
        uint8_t* data = mem->data;
        data[0] = data[1] = data[2] = 128;
        data[3] = 255;

        newDiffuseTexture = bgfx::createTexture2D(
            1, 1, false, 1, bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_NONE, mem
        );
    }

    if (bgfx::isValid(m_textures[types::TEXTURE_DIFFUSE])) {
        m_texturesBackup[types::TEXTURE_DIFFUSE] = m_textures[types::TEXTURE_DIFFUSE];
        m_textureSwapPending = true;
        m_textureSwapDelay = 5;
    }
    
    m_textures[types::TEXTURE_DIFFUSE] = newDiffuseTexture;

    uint16_t diffuseW = 0;
    uint16_t diffuseH = 0;
    m_diffuseUvMode = DiffuseUvMode::None;
    if (probeImageSize(filePath, diffuseW, diffuseH)
        && m_heightFieldWidth > 0 && m_heightFieldHeight > 0)
    {
        m_diffuseUvMode = chooseDiffuseUvMode(
            diffuseW, diffuseH, m_heightFieldWidth, m_heightFieldHeight);
        LOG_I("[TerrainRenderPipeline] Diffuse/heightField size check diffuse={}x{}, heightField={}x{}, uvMode={}",
              diffuseW, diffuseH, m_heightFieldWidth, m_heightFieldHeight, int(m_diffuseUvMode));
}
}

void TerrainRenderPipeline::loadGeometryBuffers() {
    const float halfWidth = m_terrainAspectRatio;
    const float halfHeight = 1.0f;

    const float vertices[] = {
        -halfWidth, -halfHeight, 0.0f, 1.0f,
        +halfWidth, -halfHeight, 0.0f, 1.0f,
        +halfWidth, +halfHeight, 0.0f, 1.0f,
        -halfWidth, +halfHeight, 0.0f, 1.0f,
    };

    const uint32_t indices[] = { 0, 1, 3, 2, 3, 1 };

    m_geometryLayout.begin().add(bgfx::Attrib::Position, 4, bgfx::AttribType::Float).end();

    m_geometryVertices = bgfx::createVertexBuffer(
        bgfx::copy(vertices, sizeof(vertices)),
        m_geometryLayout,
        BGFX_BUFFER_COMPUTE_READ
    );
    
    m_geometryIndices = bgfx::createIndexBuffer(
        bgfx::copy(indices, sizeof(indices)),
        BGFX_BUFFER_COMPUTE_READ | BGFX_BUFFER_INDEX32
    );
}

void TerrainRenderPipeline::loadInstancedGeometryBuffers() {
    const float* vertices;
    const uint32_t* indexes;

    switch (int32_t(m_uniforms.gpuSubd)) {
    case 0:
        m_instancedMeshVertexCount = 3;
        m_instancedMeshPrimitiveCount = 1;
        vertices = tables::s_verticesL0;
        indexes = tables::s_indexesL0;
        break;
    case 1:
        m_instancedMeshVertexCount = 6;
        m_instancedMeshPrimitiveCount = 4;
        vertices = tables::s_verticesL1;
        indexes = tables::s_indexesL1;
        break;
    case 2:
        m_instancedMeshVertexCount = 15;
        m_instancedMeshPrimitiveCount = 16;
        vertices = tables::s_verticesL2;
        indexes = tables::s_indexesL2;
        break;
    default:
        m_instancedMeshVertexCount = 45;
        m_instancedMeshPrimitiveCount = 64;
        vertices = tables::s_verticesL3;
        indexes = tables::s_indexesL3;
        break;
    }

    m_instancedGeometryLayout
        .begin()
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();

    m_instancedGeometryVertices = bgfx::createVertexBuffer(
        bgfx::makeRef(vertices, sizeof(float) * 2 * m_instancedMeshVertexCount),
        m_instancedGeometryLayout
    );

    m_instancedGeometryIndices = bgfx::createIndexBuffer(
        bgfx::makeRef(indexes, sizeof(uint32_t) * m_instancedMeshPrimitiveCount * 3),
        BGFX_BUFFER_INDEX32
    );
}

void TerrainRenderPipeline::loadSubdivisionBuffers() {
    const uint32_t bufferCapacity = 1 << 27;

    m_bufferSubd[types::BUFFER_SUBD] = bgfx::createDynamicIndexBuffer(
        bufferCapacity,
        BGFX_BUFFER_COMPUTE_READ_WRITE | BGFX_BUFFER_INDEX32
    );

    m_bufferSubd[types::BUFFER_SUBD + 1] = bgfx::createDynamicIndexBuffer(
        bufferCapacity,
        BGFX_BUFFER_COMPUTE_READ_WRITE | BGFX_BUFFER_INDEX32
    );

    m_bufferCulledSubd = bgfx::createDynamicIndexBuffer(
        bufferCapacity,
        BGFX_BUFFER_COMPUTE_READ_WRITE | BGFX_BUFFER_INDEX32
    );
}

void TerrainRenderPipeline::loadOverlayBuffers()
{
    if (bgfx::isValid(m_rectWireVertices) && bgfx::isValid(m_rectWireIndices))
    {
        return;
    }

    m_rectWireLayout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .end();

    m_rectParamLayout.begin()
        .add(bgfx::Attrib::Position, 4, bgfx::AttribType::Float)
        .end();

    struct RectWireVertex
    {
        float edgeId;
        float along;
        float side;
    };

    RectWireVertex vertices[12 * 4];
    uint16_t indices[12 * 6];

    for (uint16_t edge = 0; edge < 12; ++edge)
    {
        const uint16_t base = edge * 4;
        vertices[base + 0] = { float(edge), 0.0f, -1.0f };
        vertices[base + 1] = { float(edge), 0.0f,  1.0f };
        vertices[base + 2] = { float(edge), 1.0f, -1.0f };
        vertices[base + 3] = { float(edge), 1.0f,  1.0f };

        const uint16_t i = edge * 6;
        indices[i + 0] = base + 0;
        indices[i + 1] = base + 1;
        indices[i + 2] = base + 2;
        indices[i + 3] = base + 1;
        indices[i + 4] = base + 3;
        indices[i + 5] = base + 2;
    }

    m_rectWireVertices = bgfx::createVertexBuffer(
        bgfx::copy(vertices, sizeof(vertices)),
        m_rectWireLayout
    );

    m_rectWireIndices = bgfx::createIndexBuffer(
        bgfx::copy(indices, sizeof(indices))
    );
}

void TerrainRenderPipeline::configureUniforms() {
    float lodFactor = 2.0f * bx::tan(bx::toRad(m_fovy) / 2.0f)
        / m_width * (1 << int(m_uniforms.gpuSubd))
        * m_primitivePixelLengthTarget;

    m_uniforms.lodFactor = lodFactor;
    m_uniforms.dmapFactor = m_dmapConfig.scale;
    // Disable terrain frustum clipping to avoid missing edge patches on some devices.
    m_uniforms.cull = 0.0f;
    m_uniforms.freeze = m_freeze ? 1.0f : 0.0f;
    
    m_uniforms.terrainHalfWidth = m_terrainAspectRatio;
    m_uniforms.terrainHalfHeight = 1.0f;
    
}

void TerrainRenderPipeline::renderTerrain()
{
    TerrainPassContext ctx{};
    ctx.viewId = m_viewId;
    buildModelMatrix(ctx.model);
    bgfx::touch(ctx.viewId);
    m_uniforms.submit();
    runTerrainIndirectPass(ctx);
    runTerrainLodPass(ctx);
    runTerrainDrawCommandPass(ctx);
    submitTerrainMainPass(ctx);
    m_pingPong = 1 - m_pingPong;
}

void TerrainRenderPipeline::runTerrainIndirectPass(const TerrainPassContext& ctx)
{
    if (m_restart)
    {
        m_pingPong = 1;

        if (bgfx::isValid(m_instancedGeometryVertices))
        {
            bgfx::destroy(m_instancedGeometryVertices);
            m_instancedGeometryVertices = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_instancedGeometryIndices))
        {
            bgfx::destroy(m_instancedGeometryIndices);
            m_instancedGeometryIndices = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_bufferSubd[types::BUFFER_SUBD]))
        {
            bgfx::destroy(m_bufferSubd[types::BUFFER_SUBD]);
            m_bufferSubd[types::BUFFER_SUBD] = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_bufferSubd[types::BUFFER_SUBD + 1]))
        {
            bgfx::destroy(m_bufferSubd[types::BUFFER_SUBD + 1]);
            m_bufferSubd[types::BUFFER_SUBD + 1] = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_bufferCulledSubd))
        {
            bgfx::destroy(m_bufferCulledSubd);
            m_bufferCulledSubd = BGFX_INVALID_HANDLE;
        }

        loadInstancedGeometryBuffers();
        loadSubdivisionBuffers();

        bgfx::setBuffer(1, m_bufferSubd[m_pingPong],   bgfx::Access::ReadWrite);
        bgfx::setBuffer(2, m_bufferCulledSubd,         bgfx::Access::ReadWrite);
        bgfx::setBuffer(3, m_dispatchIndirect,         bgfx::Access::ReadWrite);
        bgfx::setBuffer(4, m_bufferCounter,            bgfx::Access::ReadWrite);
        bgfx::setBuffer(8, m_bufferSubd[1 - m_pingPong], bgfx::Access::ReadWrite);
        bgfx::dispatch(ctx.viewId, m_programsCompute[types::PROGRAM_INIT_INDIRECT],
                       1, 1, 1);

        m_restart = false;
    }
    else
    {
        bgfx::setBuffer(3, m_dispatchIndirect, bgfx::Access::ReadWrite);
        bgfx::setBuffer(4, m_bufferCounter,    bgfx::Access::ReadWrite);
        bgfx::dispatch(ctx.viewId, m_programsCompute[types::PROGRAM_UPDATE_INDIRECT],
                       1, 1, 1);
    }
}

void TerrainRenderPipeline::runTerrainLodPass(const TerrainPassContext& ctx)
{
    bgfx::setBuffer(1, m_bufferSubd[m_pingPong],        bgfx::Access::ReadWrite);
    bgfx::setBuffer(2, m_bufferCulledSubd,              bgfx::Access::ReadWrite);
    bgfx::setBuffer(4, m_bufferCounter,                 bgfx::Access::ReadWrite);
    bgfx::setBuffer(6, m_geometryVertices,              bgfx::Access::Read);
    bgfx::setBuffer(7, m_geometryIndices,               bgfx::Access::Read);
    bgfx::setBuffer(8, m_bufferSubd[1 - m_pingPong],    bgfx::Access::Read);
    bgfx::setTransform(ctx.model);

    bgfx::setTexture(0,
                     m_samplers[types::TERRAIN_DMAP_SAMPLER],
                     m_textures[types::TEXTURE_DMAP],
                     BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);

    m_uniforms.submit();

    bgfx::dispatch(ctx.viewId,
                   m_programsCompute[types::PROGRAM_SUBD_CS_LOD],
                   m_dispatchIndirect,
                   1);
}

void TerrainRenderPipeline::runTerrainDrawCommandPass(const TerrainPassContext& ctx)
{
    bgfx::setBuffer(3, m_dispatchIndirect, bgfx::Access::ReadWrite);
    bgfx::setBuffer(4, m_bufferCounter,    bgfx::Access::ReadWrite);
    m_uniforms.submit();
    bgfx::dispatch(ctx.viewId,
                   m_programsCompute[types::PROGRAM_UPDATE_DRAW],
                   1, 1, 1);
}

void TerrainRenderPipeline::bindTerrainMaterialTextures()
{
    bgfx::setTexture(0,
                     m_samplers[types::TERRAIN_DMAP_SAMPLER],
                     m_textures[types::TEXTURE_DMAP],
                     BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);

    if (m_deferSmapUseFrames > 0 && bgfx::isValid(m_dummySmap))
    {
        bgfx::setTexture(1,
                         m_samplers[types::TERRAIN_SMAP_SAMPLER],
                         m_dummySmap,
                         BGFX_SAMPLER_MIN_ANISOTROPIC |
                         BGFX_SAMPLER_MAG_ANISOTROPIC);
    }
    else
    {
        bgfx::setTexture(1,
                         m_samplers[types::TERRAIN_SMAP_SAMPLER],
                         m_textures[types::TEXTURE_SMAP],
                         BGFX_SAMPLER_MIN_ANISOTROPIC |
                         BGFX_SAMPLER_MAG_ANISOTROPIC);
    }

    if (bgfx::isValid(m_textures[types::TEXTURE_DIFFUSE]))
    {
        uint32_t diffuseSamplerFlags = BGFX_SAMPLER_UVW_MIRROR
            | BGFX_SAMPLER_MIN_ANISOTROPIC
            | BGFX_SAMPLER_MAG_ANISOTROPIC
            | BGFX_SAMPLER_MIP_POINT;

        bgfx::setTexture(5,
                         m_samplers[types::TERRAIN_DIFFUSE_SAMPLER],
                         m_textures[types::TEXTURE_DIFFUSE],
                         diffuseSamplerFlags);
    }

    if (bgfx::isValid(m_diffuseUvParamsHandle))
    {
        const float uvParams[4] = { float(m_diffuseUvMode), 0.0f, 0.0f, 0.0f };
        bgfx::setUniform(m_diffuseUvParamsHandle, uvParams);
    }
}

void TerrainRenderPipeline::submitTerrainMainPass(const TerrainPassContext& ctx)
{
    bindTerrainMaterialTextures();
    bgfx::setTransform(ctx.model);
    bgfx::setVertexBuffer(0, m_instancedGeometryVertices);
    bgfx::setIndexBuffer(m_instancedGeometryIndices);

    bgfx::setBuffer(2, m_bufferCulledSubd,   bgfx::Access::Read);
    bgfx::setBuffer(3, m_geometryVertices,   bgfx::Access::Read);
    bgfx::setBuffer(4, m_geometryIndices,    bgfx::Access::Read);

    bgfx::setState(BGFX_STATE_WRITE_RGB
                   | BGFX_STATE_WRITE_Z
                   | BGFX_STATE_DEPTH_TEST_LESS);

    m_uniforms.submit();

    bgfx::submit(ctx.viewId,
                 m_programsDraw[m_shading],
                 m_dispatchIndirect);
}

void TerrainRenderPipeline::updateOverlayGpuData()
{
    const bgfx::Caps* rectCaps = bgfx::getCaps();
    bool rectComputeAvailable = rectCaps
        && (rectCaps->supported & BGFX_CAPS_COMPUTE) != 0
        && bgfx::isValid(m_programsCompute[types::PROGRAM_RECT_MAX])
        && bgfx::isValid(m_rectParamsHandle)
        && bgfx::isValid(m_rectSampleParamsHandle);

    if (rectComputeAvailable
        && !bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::R32F, BGFX_TEXTURE_COMPUTE_WRITE))
    {
        rectComputeAvailable = false;
    }

    const bool wantReadback = rectComputeAvailable
        && m_rectMaxReadRequested
        && !m_rectMaxReadPending;
    if (m_rectMaxReadPending)
    {
        return;
    }
    if (!m_rectComputeDirty && !m_overlayWorldDirty)
    {
        if (wantReadback && !m_overlayRectsWorld.empty()
            && bgfx::isValid(m_rectMaxTexture) && bgfx::isValid(m_rectMaxReadTexture)
            && m_rectMaxTextureWidth > 0)
        {
            m_rectMaxReadback.resize(m_rectMaxTextureWidth);
            bgfx::blit(m_viewId, m_rectMaxReadTexture, 0, 0, m_rectMaxTexture);
            const uint32_t frameId = bgfx::readTexture(
                m_rectMaxReadTexture,
                m_rectMaxReadback.data());
            if (frameId != std::numeric_limits<uint32_t>::max())
            {
                m_rectMaxReadFrame = frameId;
                m_rectMaxReadCount = uint16_t(m_overlayRectsWorld.size());
                m_rectMaxReadPending = true;
                m_rectMaxReadRequested = false;
                m_rectMaxReadSubmitFrame = currentFrameId();
            }
        }
        return;
    }

    if (m_overlayRectsScreen.empty())
    {
        m_overlayRectsWorld.clear();
        m_overlayWorldDirty = false;
        m_rectComputeDirty = false;
        if (!m_rectMaxReadPending)
        {
            m_rectMaxHeights.clear();
            m_rectMaxReadRequested = false;
        }
        return;
    }

    if (!m_heightFieldReady)
    {
        return;
    }

    if (!bgfx::isValid(m_textures[types::TEXTURE_DMAP]))
    {
        return;
    }

    if (m_overlayWorldDirty)
    {
        float viewW = 0.0f;
        float viewH = 0.0f;
        float invViewProj[16];
        float invModel[16];
        float ndcNear = 0.0f;

        if (m_overlayUseScreenSpace)
        {
            if (!m_hasViewProj)
            {
                return;
            }

            viewW = float(m_width);
            viewH = float(m_height);
            if (viewW <= 0.0f || viewH <= 0.0f)
            {
                return;
            }

            float viewProj[16];
            bx::mtxMul(viewProj, m_viewMtx, m_projMtx);
            bx::mtxInverse(invViewProj, viewProj);

            float model[16];
            buildModelMatrix(model);
            bx::mtxInverse(invModel, model);

            const bgfx::Caps* caps = bgfx::getCaps();
            ndcNear = (caps && caps->homogeneousDepth) ? 0.0f : -1.0f;
        }

        m_overlayRectsWorld.clear();
        m_overlayRectsWorld.reserve(m_overlayRectsScreen.size());

        for (const OverlayRect& rect : m_overlayRectsScreen)
        {
            bx::Vec3 p00 = { 0.0f, 0.0f, 0.0f };
            bx::Vec3 p10 = { 0.0f, 0.0f, 0.0f };
            bx::Vec3 p01 = { 0.0f, 0.0f, 0.0f };

            if (m_overlayUseScreenSpace)
            {
                const float sx0 = rect.x * m_overlayPixelScale;
                const float sy0 = rect.y * m_overlayPixelScale;
                const float sx1 = (rect.x + rect.width) * m_overlayPixelScale;
                const float sy1 = (rect.y + rect.height) * m_overlayPixelScale;

                const float minSx = std::min(sx0, sx1);
                const float maxSx = std::max(sx0, sx1);
                const float minSy = std::min(sy0, sy1);
                const float maxSy = std::max(sy0, sy1);

                bx::Vec3 p11 = { 0.0f, 0.0f, 0.0f };
                if (!screenToLocalPoint(minSx, minSy, viewW, viewH, ndcNear, invViewProj, invModel, p00) ||
                    !screenToLocalPoint(maxSx, minSy, viewW, viewH, ndcNear, invViewProj, invModel, p10) ||
                    !screenToLocalPoint(maxSx, maxSy, viewW, viewH, ndcNear, invViewProj, invModel, p11) ||
                    !screenToLocalPoint(minSx, maxSy, viewW, viewH, ndcNear, invViewProj, invModel, p01))
                {
                    continue;
                }

                const float minX = std::min(std::min(p00.x, p10.x), std::min(p11.x, p01.x));
                const float maxX = std::max(std::max(p00.x, p10.x), std::max(p11.x, p01.x));
                const float minY = std::min(std::min(p00.y, p10.y), std::min(p11.y, p01.y));
                const float maxY = std::max(std::max(p00.y, p10.y), std::max(p11.y, p01.y));

                if (maxX <= minX || maxY <= minY)
                {
                    continue;
                }

                p00 = { minX, minY, 0.0f };
                p10 = { maxX, minY, 0.0f };
                p01 = { minX, maxY, 0.0f };
            }
            else
            {
                if (m_heightFieldWidth == 0 || m_heightFieldHeight == 0)
                {
                    continue;
                }

                const float pixelW = float(m_heightFieldWidth);
                const float pixelH = float(m_heightFieldHeight);
                const float invW = 1.0f / pixelW;
                const float invH = 1.0f / pixelH;

                float x0 = rect.x;
                float y0 = rect.y;
                float x1 = rect.x + rect.width;
                float y1 = rect.y + rect.height;

                if (rect.coordType == OverlayCoordType::NormalizedCenter)
                {
                    const float centerX = rect.x * pixelW;
                    const float centerY = rect.y * pixelH;
                    const float rectW = rect.width * pixelW;
                    const float rectH = rect.height * pixelH;
                    x0 = centerX - rectW * 0.5f;
                    y0 = centerY - rectH * 0.5f;
                    x1 = centerX + rectW * 0.5f;
                    y1 = centerY + rectH * 0.5f;
                }
                else if (rect.coordType == OverlayCoordType::PixelCenter)
                {
                    const float baseW = rect.imageWidth > 0.0f ? rect.imageWidth : pixelW;
                    const float baseH = rect.imageHeight > 0.0f ? rect.imageHeight : pixelH;
                    if (baseW > 0.0f && baseH > 0.0f)
                    {
                        const float centerX = (rect.x / baseW + 0.5f) * pixelW;
                        const float centerY = (rect.y / baseH + 0.5f) * pixelH;
                        const float rectW = (rect.width / baseW) * pixelW;
                        const float rectH = (rect.height / baseH) * pixelH;
                        x0 = centerX - rectW * 0.5f;
                        y0 = centerY - rectH * 0.5f;
                        x1 = centerX + rectW * 0.5f;
                        y1 = centerY + rectH * 0.5f;
                    }
                }

                const float centerX = (x0 + x1) * 0.5f;
                const float centerY = (y0 + y1) * 0.5f;
                const float rectW = std::fabs(x1 - x0);
                const float rectH = std::fabs(y1 - y0);
                if (rectW <= 1.0e-6f || rectH <= 1.0e-6f)
                {
                    continue;
                }

                const float centerU = centerX * invW;
                const float centerV = centerY * invH;
                const float worldCx = (centerU * 2.0f - 1.0f) * m_terrainAspectRatio;
                const float worldCy = (centerV * 2.0f - 1.0f);

                const float worldW = rectW * invW * 2.0f * m_terrainAspectRatio;
                const float worldH = rectH * invH * 2.0f;

                float uVecX = worldW;
                float uVecY = 0.0f;
                float vVecX = 0.0f;
                float vVecY = worldH;

                if (std::fabs(rect.angle) > 0.0001f)
                {
                    const float angleRad = bx::toRad(rect.angle);
                    const float c = std::cos(angleRad);
                    const float s = std::sin(angleRad);
                    const float ruX = uVecX * c - uVecY * s;
                    const float ruY = uVecX * s + uVecY * c;
                    const float rvX = vVecX * c - vVecY * s;
                    const float rvY = vVecX * s + vVecY * c;
                    uVecX = ruX;
                    uVecY = ruY;
                    vVecX = rvX;
                    vVecY = rvY;
                }

                const float halfUx = uVecX * 0.5f;
                const float halfUy = uVecY * 0.5f;
                const float halfVx = vVecX * 0.5f;
                const float halfVy = vVecY * 0.5f;

                p00 = { worldCx - halfUx - halfVx, worldCy - halfUy - halfVy, 0.0f };
                p10 = { p00.x + uVecX, p00.y + uVecY, 0.0f };
                p01 = { p00.x + vVecX, p00.y + vVecY, 0.0f };
            }

            const bx::Vec3 uVec = bx::sub(p10, p00);
            const bx::Vec3 vVec = bx::sub(p01, p00);
            const float uLen2 = uVec.x * uVec.x + uVec.y * uVec.y;
            const float vLen2 = vVec.x * vVec.x + vVec.y * vVec.y;
            if (uLen2 <= 1.0e-6f || vLen2 <= 1.0e-6f)
            {
                continue;
            }

            OverlayQuad worldRect{};
            worldRect.id = rect.id;
            worldRect.x = p00.x;
            worldRect.y = p00.y;
            worldRect.ux = uVec.x;
            worldRect.uy = uVec.y;
            worldRect.vx = vVec.x;
            worldRect.vy = vVec.y;
            worldRect.color[0] = rect.color[0];
            worldRect.color[1] = rect.color[1];
            worldRect.color[2] = rect.color[2];
            worldRect.color[3] = rect.color[3];
            worldRect.lineWidth = rect.lineWidth;
            worldRect.dashLength = rect.dashLength;
            worldRect.dashGap = rect.dashGap;
            worldRect.blinkPeriod = rect.blinkPeriod;
            worldRect.blinkDuty = rect.blinkDuty;
            m_overlayRectsWorld.push_back(worldRect);
        }

        m_overlayWorldDirty = false;
        m_rectComputeDirty = true;
    }

    if (m_overlayRectsWorld.empty())
    {
        m_rectComputeDirty = false;
        return;
    }

    if (!m_rectComputeDirty)
    {
        return;
    }

    const uint16_t rectCount = uint16_t(m_overlayRectsWorld.size());
    if (!ensureOverlayMaxTexture(rectCount, rectComputeAvailable, rectComputeAvailable))
    {
        if (rectComputeAvailable)
        {
            rectComputeAvailable = false;
            if (!ensureOverlayMaxTexture(rectCount, false, false))
            {
                return;
            }
        }
        else
        {
            return;
        }
    }

    if (!rectComputeAvailable)
    {
        m_rectMaxHeights.assign(m_rectMaxTextureWidth, 0.0f);
        if (!m_heightFieldCpu.empty()
            && m_heightFieldCpuWidth == m_heightFieldWidth
            && m_heightFieldCpuHeight == m_heightFieldHeight
            && m_heightFieldCpuWidth > 0
            && m_heightFieldCpuHeight > 0)
        {
            constexpr int kSampleGrid = 16;
            const float invScale = 1.0f / 65535.0f;
            const float halfW = m_terrainAspectRatio;
            const float halfH = 1.0f;
            for (uint16_t i = 0; i < rectCount; ++i)
            {
                const OverlayQuad& rect = m_overlayRectsWorld[i];
                float maxHeight = 0.0f;
                for (int gy = 0; gy < kSampleGrid; ++gy)
                {
                    for (int gx = 0; gx < kSampleGrid; ++gx)
                    {
                        const float tx = (float(gx) + 0.5f) / float(kSampleGrid);
                        const float ty = (float(gy) + 0.5f) / float(kSampleGrid);
                        const float posX = rect.x + rect.ux * tx + rect.vx * ty;
                        const float posY = rect.y + rect.uy * tx + rect.vy * ty;
                        float u = (posX + halfW) / (2.0f * halfW);
                        float v = (posY + halfH) / (2.0f * halfH);
                        u = std::min(1.0f, std::max(0.0f, u));
                        v = std::min(1.0f, std::max(0.0f, v));
                        const int ix = std::min<int>(int(u * m_heightFieldCpuWidth), m_heightFieldCpuWidth - 1);
                        const int iy = std::min<int>(int(v * m_heightFieldCpuHeight), m_heightFieldCpuHeight - 1);
                        const size_t idx = size_t(iy) * m_heightFieldCpuWidth + size_t(ix);
                        const float h = float(m_heightFieldCpu[idx]) * invScale * m_dmapConfig.scale;
                        if (h > maxHeight)
                        {
                            maxHeight = h;
                        }
                    }
                }
                m_rectMaxHeights[i] = maxHeight;
            }
        }
        const bgfx::Memory* rectMaxMem = bgfx::copy(
            m_rectMaxHeights.data(),
            uint32_t(m_rectMaxTextureWidth * sizeof(float))
        );
        bgfx::updateTexture2D(m_rectMaxTexture, 0, 0, 0, 0,
                              m_rectMaxTextureWidth, 1, rectMaxMem);
        m_rectComputeDirty = false;
        m_rectMaxReadRequested = false;
        m_rectMaxReadPending = false;
        return;
    }

    if (!ensureOverlayRectBuffers(rectCount))
    {
        return;
    }

    struct RectGpu
    {
        float p0x;
        float p0y;
        float ux;
        float uy;
        float vx;
        float vy;
        float pad0;
        float pad1;
    };

    std::vector<RectGpu> rects(rectCount);
    for (uint16_t i = 0; i < rectCount; ++i)
    {
        rects[i].p0x = m_overlayRectsWorld[i].x;
        rects[i].p0y = m_overlayRectsWorld[i].y;
        rects[i].ux = m_overlayRectsWorld[i].ux;
        rects[i].uy = m_overlayRectsWorld[i].uy;
        rects[i].vx = m_overlayRectsWorld[i].vx;
        rects[i].vy = m_overlayRectsWorld[i].vy;
        rects[i].pad0 = 0.0f;
        rects[i].pad1 = 0.0f;
    }

    const bgfx::Memory* rectMem = bgfx::copy(rects.data(), uint32_t(rects.size() * sizeof(RectGpu)));
    bgfx::update(m_rectParamsBuffer, 0, rectMem);

    const float rectParams[4] = { float(rectCount), 0.0f, 0.0f, 0.0f };
    const float sampleParams[4] = { m_terrainAspectRatio, 1.0f, m_dmapConfig.scale, 0.0f };

    bgfx::setUniform(m_rectParamsHandle, rectParams);
    bgfx::setUniform(m_rectSampleParamsHandle, sampleParams);
    bgfx::setBuffer(0, m_rectParamsBuffer, bgfx::Access::Read);
    bgfx::setTexture(1, m_samplers[types::TERRAIN_DMAP_SAMPLER],
        m_textures[types::TEXTURE_DMAP],
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
    bgfx::setImage(2, m_rectMaxTexture, 0, bgfx::Access::Write, bgfx::TextureFormat::R32F);
    bgfx::dispatch(m_viewId, m_programsCompute[types::PROGRAM_RECT_MAX], rectCount, 1, 1);

    m_rectComputeDirty = false;

    if (wantReadback && bgfx::isValid(m_rectMaxTexture) && bgfx::isValid(m_rectMaxReadTexture)
        && m_rectMaxTextureWidth > 0)
    {
        m_rectMaxReadback.resize(m_rectMaxTextureWidth);
        bgfx::blit(m_viewId, m_rectMaxReadTexture, 0, 0, m_rectMaxTexture);
        const uint32_t frameId = bgfx::readTexture(
            m_rectMaxReadTexture,
            m_rectMaxReadback.data());
        if (frameId != std::numeric_limits<uint32_t>::max())
        {
            m_rectMaxReadFrame = frameId;
            m_rectMaxReadCount = rectCount;
            m_rectMaxReadPending = true;
            m_rectMaxReadRequested = false;
            m_rectMaxReadSubmitFrame = currentFrameId();
        }
    }
}

bool TerrainRenderPipeline::ensureOverlayRectBuffers(uint16_t rectCount)
{
    if (rectCount == 0)
    {
        return false;
    }

    const uint16_t requiredEntries = uint16_t(rectCount * 2);
    if (!bgfx::isValid(m_rectParamsBuffer) || m_rectBufferCapacity < requiredEntries)
    {
        if (bgfx::isValid(m_rectParamsBuffer))
        {
            bgfx::destroy(m_rectParamsBuffer);
        }

        m_rectBufferCapacity = nextPow2(requiredEntries);
        m_rectParamsBuffer = bgfx::createDynamicVertexBuffer(
            m_rectBufferCapacity,
            m_rectParamLayout,
            BGFX_BUFFER_COMPUTE_READ
        );
    }

    return bgfx::isValid(m_rectParamsBuffer);
}

bool TerrainRenderPipeline::ensureOverlayMaxTexture(uint16_t rectCount, bool useCompute, bool needReadback)
{
    if (rectCount == 0)
    {
        return false;
    }

    const bool needRecreate = !bgfx::isValid(m_rectMaxTexture)
        || m_rectMaxTextureWidth < rectCount
        || m_rectMaxTextureCompute != useCompute;

    if (needRecreate)
    {
        if (bgfx::isValid(m_rectMaxTexture))
        {
            bgfx::destroy(m_rectMaxTexture);
        }

        m_rectMaxTextureWidth = nextPow2(rectCount);
        uint64_t texFlags = BGFX_TEXTURE_NONE
            | BGFX_SAMPLER_U_CLAMP
            | BGFX_SAMPLER_V_CLAMP
            | BGFX_SAMPLER_MIN_POINT
            | BGFX_SAMPLER_MAG_POINT;
        if (useCompute)
        {
            texFlags |= BGFX_TEXTURE_COMPUTE_WRITE;
        }

        m_rectMaxTexture = bgfx::createTexture2D(
            m_rectMaxTextureWidth,
            1,
            false,
            1,
            bgfx::TextureFormat::R32F,
            texFlags
        );
        m_rectMaxTextureCompute = useCompute;
    }

    if (needReadback && m_rectMaxTextureWidth > 0)
    {
        if (!bgfx::isValid(m_rectMaxReadTexture) || m_rectMaxTextureWidth < rectCount)
        {
            if (bgfx::isValid(m_rectMaxReadTexture))
            {
                bgfx::destroy(m_rectMaxReadTexture);
            }
            const uint64_t readFlags = BGFX_TEXTURE_READ_BACK | BGFX_TEXTURE_BLIT_DST;
            m_rectMaxReadTexture = bgfx::createTexture2D(
                m_rectMaxTextureWidth,
                1,
                false,
                1,
                bgfx::TextureFormat::R32F,
                readFlags
            );
        }
    }

    if (needReadback)
    {
        return bgfx::isValid(m_rectMaxTexture) && bgfx::isValid(m_rectMaxReadTexture);
    }
    return bgfx::isValid(m_rectMaxTexture);
}

void TerrainRenderPipeline::renderOverlayRects()
{
    if (!m_heightFieldReady)
    {
        return;
    }

    if (m_overlayRectsWorld.empty())
    {
        return;
    }

    if (!bgfx::isValid(m_programRectWire) ||
        !bgfx::isValid(m_rectWireVertices) ||
        !bgfx::isValid(m_rectWireIndices) ||
        !bgfx::isValid(m_rectMaxTexture) ||
        !bgfx::isValid(m_rectMaxSampler) ||
        !bgfx::isValid(m_rectMaxParamsHandle) ||
        !bgfx::isValid(m_rectViewParamsHandle) ||
        !bgfx::isValid(m_rectDebugParamsHandle))
    {
        return;
    }

    const uint32_t rectCount = uint32_t(m_overlayRectsWorld.size());
    const uint16_t instanceStride = sizeof(float) * 16;
    const uint32_t avail = bgfx::getAvailInstanceDataBuffer(rectCount, instanceStride);
    if (avail == 0)
    {
        return;
    }

    const uint32_t drawCount = std::min(rectCount, avail);
    bgfx::InstanceDataBuffer idb;
    bgfx::allocInstanceDataBuffer(&idb, drawCount, instanceStride);

    uint8_t* data = idb.data;
    for (uint32_t i = 0; i < drawCount; ++i)
    {
        const OverlayQuad& rect = m_overlayRectsWorld[i];
        float* dst = reinterpret_cast<float*>(data);

        dst[0] = rect.x;
        dst[1] = rect.y;
        dst[2] = rect.ux;
        dst[3] = rect.uy;

        dst[4] = rect.vx;
        dst[5] = rect.vy;
        dst[6] = rect.lineWidth;
        dst[7] = rect.dashLength;

        dst[8] = rect.dashGap;
        dst[9] = rect.blinkPeriod;
        dst[10] = rect.blinkDuty;
        dst[11] = 0.0f;

        dst[12] = rect.color[0];
        dst[13] = rect.color[1];
        dst[14] = rect.color[2];
        dst[15] = rect.color[3];

        data += instanceStride;
    }

    float rectMaxParams[4] = { float(m_rectMaxTextureWidth), 0.0f, 0.0f, 0.0f };
    if (m_rectMaxTextureWidth > 0)
    {
        rectMaxParams[1] = 1.0f / float(m_rectMaxTextureWidth);
    }

    const float overlayZLift = std::max(0.003f, m_dmapConfig.scale * 0.03f);
    const float rectViewParams[4] = { float(m_width), float(m_height), m_overlayTime, overlayZLift };
    const float rectDebugParams[4] = {
        m_overlayDebugAxes ? 1.0f : 0.0f,
        6.0f,
        10.0f,
        14.0f
    };

    bgfx::setUniform(m_rectMaxParamsHandle, rectMaxParams);
    bgfx::setUniform(m_rectViewParamsHandle, rectViewParams);
    bgfx::setUniform(m_rectDebugParamsHandle, rectDebugParams);
    bgfx::setTexture(2, m_rectMaxSampler, m_rectMaxTexture,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);

    float model[16];
    buildModelMatrix(model);
    bgfx::setTransform(model);

    bgfx::setVertexBuffer(0, m_rectWireVertices);
    bgfx::setIndexBuffer(m_rectWireIndices);
    bgfx::setInstanceDataBuffer(&idb);

    bgfx::setState(BGFX_STATE_WRITE_RGB
        | BGFX_STATE_WRITE_A
        | BGFX_STATE_DEPTH_TEST_LESS
        | BGFX_STATE_BLEND_ALPHA
        | BGFX_STATE_MSAA);

    bgfx::submit(m_viewId, m_programRectWire);
}

void TerrainRenderPipeline::setRenderTarget(bgfx::ViewId viewId, bgfx::FrameBufferHandle framebuffer)
{
    m_viewId = viewId;
    m_frameBuffer = framebuffer;
    bgfx::setViewMode(m_viewId, bgfx::ViewMode::Sequential);
}

bool TerrainRenderPipeline::loadHeightFieldFromFile(const char* localPath)
{
    if (localPath == nullptr || *localPath == '\0')
    {
        clearHeightField();
        return true;
    }

    bx::strCopy(m_heightFieldPath, sizeof(m_heightFieldPath), localPath);

    m_dmapConfig.pathToFile = bx::FilePath(localPath);

    m_heightFieldNeedReload = true;
    m_heightFieldReady = false;
    m_rectComputeDirty = true;
    return true;
}


bool TerrainRenderPipeline::loadDiffuseFromFile(const char* localPath)
{
    if (localPath == nullptr || *localPath == '\0') {
        clearDiffuse();
        return true;
    }
    bx::strCopy(m_diffuseTexturePath, BX_COUNTOF(m_diffuseTexturePath), localPath);

    m_diffuseNeedReload = true;
    return true;
}

void TerrainRenderPipeline::clearHeightField()
{
    m_heightFieldPath[0] = '\0';
    m_dmapConfig.pathToFile = bx::FilePath("");
    m_heightFieldNeedReload = true;
    m_heightFieldReady = false;
    m_heightFieldWidth = 0;
    m_heightFieldHeight = 0;
    m_heightFieldMips = 1;
    m_terrainAspectRatio = 1.0f;
    m_heightFieldCpu.clear();
    m_heightFieldCpuWidth = 0;
    m_heightFieldCpuHeight = 0;
    m_overlayWorldDirty = true;
    m_rectComputeDirty = true;
    if (m_rectMaxReadPending)
    {
        m_rectMaxReadCancelPending = true;
    }
    m_rectMaxReadRequested = false;
    m_rectMaxHeights.clear();
    m_diffuseUvMode = DiffuseUvMode::None;
}

void TerrainRenderPipeline::clearDiffuse()
{
    m_diffuseTexturePath[0] = '\0';
    m_diffuseNeedReload = true;
    m_diffuseUvMode = DiffuseUvMode::None;
}

void TerrainRenderPipeline::loadDmapTexture()
{
    const char* path = m_dmapConfig.pathToFile.getCPtr();
    std::string pathStr = path ? path : "";
    if (pathStr.empty() || pathStr == ".")
    {
        const bgfx::Memory* mem = bgfx::alloc(sizeof(uint16_t));
        std::memset(mem->data, 0, sizeof(uint16_t));

        bgfx::TextureHandle newDmapTexture = bgfx::createTexture2D(
            1, 1, false, 1, bgfx::TextureFormat::R16,
            BGFX_TEXTURE_NONE, mem
        );

        if (bgfx::isValid(m_textures[types::TEXTURE_DMAP]))
        {
            m_texturesBackup[types::TEXTURE_DMAP] = m_textures[types::TEXTURE_DMAP];
            m_textureSwapPending = true;
            m_textureSwapDelay = 5;
        }

        m_textures[types::TEXTURE_DMAP] = newDmapTexture;
        return;
    }

    m_textureLoader->loadTexture(pathStr);
}

void TerrainRenderPipeline::uploadLoadedTexture(TerrainHeightFieldLoader::LoadRequest&& request)
{
    if (!request.success) {
        return;
    }

    if (request.data.empty())
    {
        LOG_W("[TerrainRenderPipeline] Missing CPU heightField data for '{}', skipping upload", request.path);
        return;
    }

    m_heightFieldCpu = std::move(request.data);
    m_heightFieldCpuWidth = uint16_t(request.width);
    m_heightFieldCpuHeight = uint16_t(request.height);

    const bgfx::Memory* mem = bgfx::copy(
        m_heightFieldCpu.data(),
        uint32_t(m_heightFieldCpu.size() * sizeof(uint16_t))
    );
    
    bgfx::TextureHandle newDmapTexture = bgfx::createTexture2D(
        uint16_t(request.width), uint16_t(request.height),
        false, 1, bgfx::TextureFormat::R16,
        BGFX_TEXTURE_NONE, mem
    );

    if (bgfx::isValid(m_textures[types::TEXTURE_DMAP])) {
        m_texturesBackup[types::TEXTURE_DMAP] = m_textures[types::TEXTURE_DMAP];
        m_textureSwapPending = true;
        m_textureSwapDelay = 60;
    }
    
    m_textures[types::TEXTURE_DMAP] = newDmapTexture;
    
    const uint16_t prevWidth = m_heightFieldWidth;
    const uint16_t prevHeight = m_heightFieldHeight;
    const float prevAspect = m_terrainAspectRatio;

    m_terrainAspectRatio = request.aspectRatio;
    m_heightFieldWidth = uint16_t(request.width);
    m_heightFieldHeight = uint16_t(request.height);
    m_heightFieldMips = 1;
    m_heightFieldReady = (m_heightFieldPath[0] != '\0');
    m_smapNeedsRegen = true;
    m_rectComputeDirty = true;
    if (m_rectMaxReadPending)
    {
        m_rectMaxReadCancelPending = true;
    }
    m_rectMaxReadRequested = true;
    if (prevWidth != m_heightFieldWidth
        || prevHeight != m_heightFieldHeight
        || std::fabs(prevAspect - m_terrainAspectRatio) > 0.0001f)
    {
        m_overlayWorldDirty = true;
    }
    LOG_D("[uploadLoadedTexture]request.path.c_str()={}, m_terrainAspectRatio={}", request.path.c_str(), m_terrainAspectRatio);

    if (m_diffuseTexturePath[0] != '\0')
    {
        uint16_t diffuseW = 0;
        uint16_t diffuseH = 0;
        if (probeImageSize(m_diffuseTexturePath, diffuseW, diffuseH))
        {
            m_diffuseUvMode = chooseDiffuseUvMode(
                diffuseW, diffuseH, m_heightFieldWidth, m_heightFieldHeight);
            LOG_I("[TerrainRenderPipeline] Recompute uvMode after CPU heightField upload: diffuse={}x{}, heightField={}x{}, uvMode={}",
                  diffuseW, diffuseH, m_heightFieldWidth, m_heightFieldHeight, int(m_diffuseUvMode));
        }
    }

    BX_TRACE("Uploaded heightField texture: %s (%dx%d), aspect ratio: %.2f", 
             request.path.c_str(), request.width, request.height, m_terrainAspectRatio);
}
