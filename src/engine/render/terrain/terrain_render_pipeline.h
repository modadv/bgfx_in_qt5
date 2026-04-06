#pragma once

#include "render/terrain/terrain_height_field_loader.h"
#include "render/terrain/terrain_uniform_set.h"
#include "render/terrain/terrain_constants.h"

#include <bgfx/bgfx.h>
#include <bx/file.h>
#include <cstdint>
#include <memory>
#include <string>
#include <limits>
#include <vector>
struct LoadTimeRecord {
    float loadTimeMs;
    char heightFieldName[64];
    char diffuseName[64];
    int64_t timestamp;
};

struct DMap {
    bx::FilePath pathToFile;
    float scale = 0.8f;
};

enum class OverlayCoordType : uint8_t {
    TopLeftPixels = 0,
    NormalizedCenter = 1,
    PixelCenter = 2
};

struct OverlayRect {
    int32_t id = -1;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float lineWidth = 1.0f;
    float dashLength = 0.0f;
    float dashGap = 0.0f;
    float blinkPeriod = 0.0f;
    float blinkDuty = 0.5f;
    float angle = 0.0f;
    OverlayCoordType coordType = OverlayCoordType::TopLeftPixels;
    float imageWidth = 0.0f;
    float imageHeight = 0.0f;
};

struct OverlayQuad {
    int32_t id = -1;
    float x = 0.0f;
    float y = 0.0f;
    float ux = 0.0f;
    float uy = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float lineWidth = 1.0f;
    float dashLength = 0.0f;
    float dashGap = 0.0f;
    float blinkPeriod = 0.0f;
    float blinkDuty = 0.5f;
};

class TerrainRenderPipeline {
public:
    struct RenderFrameContext
    {
        float deltaTime = 0.0f;
        uint8_t viewId = 0;
        bool needTextureReload = false;
        bool needSmapRegenerate = false;
    };
    struct TerrainPassContext
    {
        uint8_t viewId = 0;
        float model[16]{};
    };

    enum class DiffuseUvMode : uint8_t {
        None = 0,
        SwapUV = 1,
        RotateCW = 2,
        RotateCCW = 3
    };

    static constexpr int MAX_LOAD_HISTORY = 5;

    TerrainRenderPipeline();
    ~TerrainRenderPipeline();

    bool init(uint32_t width, uint32_t height);
    void shutdown();
    bool update(float deltaTime, const float* viewMtx, const float* projMtx);
    void setRenderTarget(bgfx::ViewId viewId, bgfx::FrameBufferHandle framebuffer);

    void resize(uint32_t w, uint32_t h);
    void setWireframe(bool enabled) { m_wireframe = enabled; }
    void setCulling(bool enabled) { m_cull = enabled; }
    void setFreeze(bool enabled) { m_freeze = enabled; }
    void setPrimitivePixelLength(float length) { m_primitivePixelLengthTarget = length; }
    void setShading(int shading) { m_shading = shading; }
    void setGpuSubdivision(int level);

    bool loadHeightFieldFromFile(const char* localPath);
    bool loadDiffuseFromFile(const char* localPath);
    void clearHeightField();
    void clearDiffuse();
    void reloadTextures();
    void setOverlayRects(const std::vector<OverlayRect>& rects);
    void clearOverlayRects();
    void setOverlayUseScreenSpace(bool enabled);
    void setOverlayPixelScale(float scale);
    void setImageTransform(float rotationDeg, float scaleX, float scaleY);

    float getLoadTime() const { return m_loadTime; }
    float getCpuSmapTime() const { return m_cpuSmapGenTime; }
    float getGpuSmapTime() const { return m_gpuSmapGenTime; }
    uint16_t heightFieldWidth() const { return m_heightFieldWidth; }
    uint16_t heightFieldHeight() const { return m_heightFieldHeight; }
    void setOverlayDebugAxes(bool enabled) { m_overlayDebugAxes = enabled; }
    void requestOverlayMaxReadback();
    bool processOverlayMaxReadback(uint32_t frameId);
    bool overlayMaxReady() const;
    bool getOverlayRectWorldBounds(int rectId,
                                   float& outCenterX,
                                   float& outCenterY,
                                   float& outCenterZ,
                                   float& outWidth,
                                   float& outHeight,
                                   float& outNormalX,
                                   float& outNormalY,
                                   float& outNormalZ) const;
    bool getOverlayRectNearestEdgeTargetYaw(int rectId, float& outYawDeg) const;
    bool getAlgorithmDenseSideTargetYaw(float& outYawDeg, int& outRectId) const;
    bool hasOverlayRects() const;
    int pickOverlayRect(float sx, float sy) const;
    bool isTerrainDataReady() const { return m_heightFieldReady; }
    float terrainAspectRatio() const { return m_terrainAspectRatio; }
    float dmapScale() const { return m_dmapConfig.scale; }
    float imageScaleX() const { return m_imageScaleX; }
    float imageScaleY() const { return m_imageScaleY; }

    void loadPrograms();
    void loadTextures();
    void loadBuffers();
    void createAtomicCounters();

    void loadDmapTexture();
    void loadSmapTexture();
    void loadSmapTextureGPU();
    void loadDiffuseTexture();

    void loadGeometryBuffers();
    void loadInstancedGeometryBuffers();
    void loadSubdivisionBuffers();

    void configureUniforms();
    void renderTerrain();
    void runTerrainIndirectPass(const TerrainPassContext& ctx);
    void runTerrainLodPass(const TerrainPassContext& ctx);
    void runTerrainDrawCommandPass(const TerrainPassContext& ctx);
    void bindTerrainMaterialTextures();
    void submitTerrainMainPass(const TerrainPassContext& ctx);
    void renderOverlayRects();
    void renderAxes();

    bool ensureValidResources();
    bool beginFrame(RenderFrameContext& ctx, const float* viewMtx, const float* projMtx);
    void setupViewStage(const RenderFrameContext& ctx, const float* viewMtx, const float* projMtx);
    void processStreamingStage();
    void processTextureStage(RenderFrameContext& ctx);
    void processComputeStage();
    void processDrawStage();

    void destroyAllResources();
    // --- Members (unchanged) ---
    TerrainUniformSet m_uniforms;

    bgfx::ProgramHandle m_programsCompute[types::PROGRAM_COUNT];
    bgfx::ProgramHandle m_programsDraw[types::SHADING_COUNT];
    bgfx::TextureHandle m_textures[types::TEXTURE_COUNT];
    bgfx::UniformHandle m_samplers[types::SAMPLER_COUNT];
    bgfx::UniformHandle m_smapParamsHandle;
    bgfx::UniformHandle m_smapChunkParamsHandle;
    bgfx::UniformHandle m_diffuseUvParamsHandle = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_texturesBackup[types::TEXTURE_COUNT];

    bool m_textureSwapPending = false;
    int  m_textureSwapDelay = 0;

    bgfx::DynamicIndexBufferHandle m_bufferSubd[2];
    bgfx::DynamicIndexBufferHandle m_bufferCulledSubd;
    bgfx::DynamicIndexBufferHandle m_bufferCounter;
    bgfx::IndexBufferHandle        m_geometryIndices;
    bgfx::VertexBufferHandle       m_geometryVertices;
    bgfx::VertexLayout             m_geometryLayout;
    bgfx::IndexBufferHandle        m_instancedGeometryIndices;
    bgfx::VertexBufferHandle       m_instancedGeometryVertices;
    bgfx::VertexLayout             m_instancedGeometryLayout;
    bgfx::IndirectBufferHandle     m_dispatchIndirect;
    bgfx::TextureHandle            m_dummySmap = BGFX_INVALID_HANDLE;
    int                            m_deferSmapUseFrames = 0;

    DMap      m_dmapConfig;
    uint32_t  m_width  = 0;
    uint32_t  m_height = 0;
    bgfx::ViewId            m_viewId = 0;
    bgfx::FrameBufferHandle m_frameBuffer = BGFX_INVALID_HANDLE;
    uint32_t  m_instancedMeshVertexCount = 0;
    uint32_t  m_instancedMeshPrimitiveCount = 0;
    int       m_shading  = 0;
    int       m_pingPong = 0;
    float     m_terrainAspectRatio          = 1.0f;
    float     m_primitivePixelLengthTarget  = 1.0f;
    float     m_fovy                        = 60.0f;
    bool      m_restart                     = true;
    bool      m_wireframe                   = false;
    bool      m_cull                        = true;
    bool      m_freeze                      = false;
    bool      m_useGpuSmap                  = true;
    bool      m_heightFieldNeedReload         = false;
    bool      m_diffuseNeedReload           = false;
    bool      m_smapNeedsRegen              = false;

    int64_t   m_loadStartTime = 0;
    bool      m_firstFrameRendered = false;
    float     m_loadTime          = 0.0f;
    float     m_cpuSmapGenTime    = 0.0f;
    float     m_gpuSmapGenTime    = 0.0f;
    uint64_t m_bgfxGeneration = 0;
    bool m_resourcesValid = false;

    void invalidateAllHandles();
    LoadTimeRecord m_loadHistory[MAX_LOAD_HISTORY];
    int            m_loadHistoryCount = 0;

    char m_heightFieldPath[256];
    char m_diffuseTexturePath[256];
    std::unique_ptr<TerrainHeightFieldLoader> m_textureLoader;
    void uploadLoadedTexture(TerrainHeightFieldLoader::LoadRequest&& request);
    void updateOverlayGpuData();
    void loadOverlayBuffers();
    bool ensureOverlayRectBuffers(uint16_t rectCount);
    bool ensureOverlayMaxTexture(uint16_t rectCount, bool useCompute, bool needReadback);
    void buildModelMatrix(float* out) const;

    uint16_t m_heightFieldWidth = 0;
    uint16_t m_heightFieldHeight = 0;
    uint8_t m_heightFieldMips = 1;
    bool m_heightFieldReady = false;
    std::vector<uint16_t> m_heightFieldCpu;
    uint16_t m_heightFieldCpuWidth = 0;
    uint16_t m_heightFieldCpuHeight = 0;

    bgfx::ProgramHandle m_programRectWire = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_programColor = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_rectWireVertices = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle m_rectWireIndices = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle m_rectParamsBuffer = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_rectMaxTexture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_rectMaxReadTexture = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_rectMaxSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_rectMaxParamsHandle = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_rectViewParamsHandle = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_rectParamsHandle = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_rectSampleParamsHandle = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_rectDebugParamsHandle = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout m_rectParamLayout;
    bgfx::VertexLayout m_rectWireLayout;
    bgfx::VertexLayout m_colorLayout;
    bool m_colorLayoutReady = false;
    std::vector<OverlayRect> m_overlayRectsScreen;
    std::vector<OverlayQuad> m_overlayRectsWorld;
    uint16_t m_rectBufferCapacity = 0;
    uint16_t m_rectMaxTextureWidth = 0;
    bool m_rectMaxTextureCompute = false;
    bool m_rectComputeDirty = false;
    float m_overlayTime = 0.0f;
    float m_viewMtx[16]{};
    float m_projMtx[16]{};
    bool m_hasViewProj = false;
    bool m_overlayDebugAxes = false;
    bool m_overlayWorldDirty = false;
    bool m_overlayUseScreenSpace = true;
    float m_overlayPixelScale = 1.0f;
    std::vector<float> m_rectMaxHeights;
    std::vector<float> m_rectMaxReadback;
    uint32_t m_rectMaxReadFrame = std::numeric_limits<uint32_t>::max();
    uint16_t m_rectMaxReadCount = 0;
    bool m_rectMaxReadPending = false;
    bool m_rectMaxReadRequested = false;
    bool m_rectMaxReadCancelPending = false;
    uint32_t m_rectMaxReadSubmitFrame = std::numeric_limits<uint32_t>::max();
    float m_imageRotation = 0.0f;
    float m_imageScaleX = 1.0f;
    float m_imageScaleY = 1.0f;
    DiffuseUvMode m_diffuseUvMode = DiffuseUvMode::None;
};


