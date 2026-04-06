#include "render_device_bgfx.h"
#include "logger.h"
#include "common/bgfx_utils.h"
#include <bgfx/c99/bgfx.h>

#include <algorithm>
#include <limits>

namespace
{
    constexpr uint8_t kInvalidViewId = std::numeric_limits<uint8_t>::max();

    bgfx::RendererType::Enum chooseRendererType(const bgfx::PlatformData& platformData)
    {
#if defined(__linux__)
        return bgfx::RendererType::OpenGL;
#elif defined(_WIN32)
        BX_UNUSED(platformData);
        return bgfx::RendererType::Direct3D11;
#else
        return bgfx::RendererType::Count;
#endif
    }

    std::string rendererName(bgfx::RendererType::Enum type)
    {
        return std::string(getName(type).getPtr(), getName(type).getLength());
    }
}

RenderDeviceBgfx::ViewSurface::ViewSurface()
    : renderViewId(kInvalidViewId)
    , blitViewId(kInvalidViewId)
    , width(0)
    , height(0)
    , framebuffer(BGFX_INVALID_HANDLE)
    , colorTex(BGFX_INVALID_HANDLE)
    , depthTex(BGFX_INVALID_HANDLE)
    , generation(0)
{
    readbackTex.fill(BGFX_INVALID_HANDLE);
}

RenderDeviceBgfx::RenderDeviceBgfx() = default;

RenderDeviceBgfx::~RenderDeviceBgfx()
{
    // Shutdown must be explicit from the thread that owns bgfx API calls.
}

RenderDeviceBgfx& RenderDeviceBgfx::instance()
{
    static RenderDeviceBgfx inst;
    return inst;
}

bool RenderDeviceBgfx::acquire(uint32_t w, uint32_t h)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    ++m_refCount;

    if (m_initialized.load())
    {
        ensureResolutionInternal(w, h);
        return false;
    }

    return doInit(w, h);
}

bool RenderDeviceBgfx::release()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_refCount <= 0)
    {
        return false;
    }

    --m_refCount;

    if (m_refCount == 0 && m_initialized.load())
    {
        // Keep bgfx alive across QQuickFramebufferObject renderer recreation.
        // bgfx::renderFrame(-1) is one-shot in multithreaded builds; shutting down and
        // reinitializing can assert on the next pre-init renderFrame call.
        return true;
    }

    return false;
}

void RenderDeviceBgfx::setPlatformData(const bgfx::PlatformData& pd)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_platformData = pd;
}

bool RenderDeviceBgfx::ensureResolution(uint32_t minWidth, uint32_t minHeight)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized.load())
    {
        return false;
    }

    return ensureResolutionInternal(minWidth, minHeight);
}

bool RenderDeviceBgfx::createSurface(uint32_t w, uint32_t h, ViewSurface& outSurface)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized.load())
    {
        return false;
    }

    ensureResolutionInternal(w, h);
    destroySurfaceResources(outSurface, false);

    outSurface.renderViewId = allocateViewId();
    outSurface.blitViewId   = allocateViewId();

    if (outSurface.renderViewId == kInvalidViewId ||
        outSurface.blitViewId == kInvalidViewId)
    {
        destroySurfaceResources(outSurface, true);
        return false;
    }

    outSurface.width      = w;
    outSurface.height     = h;
    outSurface.generation = m_generation.load();

    if (!createSurfaceResources(w, h, outSurface))
    {
        destroySurfaceResources(outSurface, true);
        return false;
    }

    return true;
}

bool RenderDeviceBgfx::resizeSurface(uint32_t w, uint32_t h, ViewSurface& surface)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized.load())
    {
        return false;
    }

    ensureResolutionInternal(w, h);

    if (surface.width == w && surface.height == h && surface.generation == m_generation.load())
    {
        return true;
    }

    const uint8_t renderId = surface.renderViewId;
    const uint8_t blitId   = surface.blitViewId;

    destroySurfaceResources(surface, false);

    surface.renderViewId = renderId;
    surface.blitViewId   = blitId;
    surface.width        = w;
    surface.height       = h;
    surface.generation   = m_generation.load();

    if (!createSurfaceResources(w, h, surface))
    {
        destroySurfaceResources(surface, true);
        return false;
    }

    return true;
}

void RenderDeviceBgfx::destroySurface(ViewSurface& surface)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    destroySurfaceResources(surface, true);
}

void RenderDeviceBgfx::shutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized.load())
    {
        m_refCount = 0;
        return;
    }

    doShutdown();
    m_refCount = 0;
}

uint32_t RenderDeviceBgfx::endFrame()
{
    if (!m_initialized.load())
        return 0;
    const uint32_t frameId = bgfx::frame();
    m_lastFrameId.store(frameId);
    return frameId;
}

bool RenderDeviceBgfx::doInit(uint32_t w, uint32_t h)
{
    if (m_initialized.load())
        return false;

    const bgfx_render_frame_t preInitFrame = bgfx_render_frame(0);
    LOG_I("[RenderDeviceBgfx] pre-init renderFrame result={}", int(preInitFrame));

    m_backbufferWidth  = std::max<uint32_t>(1, w);
    m_backbufferHeight = std::max<uint32_t>(1, h);
    m_nextViewId       = 0;
    m_usedViewIds.clear();

    bgfx::Init init{};
    init.type = chooseRendererType(m_platformData);
    init.resolution.width  = m_backbufferWidth;
    init.resolution.height = m_backbufferHeight;
    init.resolution.reset  = BGFX_RESET_NONE;

    init.platformData = m_platformData;

    LOG_I("[RenderDeviceBgfx] init request renderer={} size={}x{} nwh={} context={}",
          rendererName(init.type),
          init.resolution.width,
          init.resolution.height,
          init.platformData.nwh,
          init.platformData.context);

    if (!bgfx::init(init))
    {
        LOG_E("[RenderDeviceBgfx] bgfx::init failed renderer={} size={}x{}", rendererName(init.type), init.resolution.width, init.resolution.height);
        return false;
    }

    m_initialized.store(true);
    ++m_generation;
    LOG_I("[RenderDeviceBgfx] initialized renderer={} generation={}",
          rendererName(bgfx::getRendererType()),
          m_generation.load());

    return true;
}

void RenderDeviceBgfx::doShutdown()
{
    if (!m_initialized.load())
        return;

    m_usedViewIds.clear();
    m_nextViewId = 0;

    bgfx::shutdown();

    m_initialized.store(false);
    m_backbufferWidth  = 0;
    m_backbufferHeight = 0;
}

bool RenderDeviceBgfx::ensureResolutionInternal(uint32_t w, uint32_t h)
{
    uint32_t newW = std::max<uint32_t>(1, std::max(w, m_backbufferWidth));
    uint32_t newH = std::max<uint32_t>(1, std::max(h, m_backbufferHeight));

    if (newW == m_backbufferWidth && newH == m_backbufferHeight)
    {
        return false;
    }

    m_backbufferWidth  = newW;
    m_backbufferHeight = newH;
    bgfx::reset(uint16_t(newW), uint16_t(newH), BGFX_RESET_NONE);
    return true;
}

uint8_t RenderDeviceBgfx::allocateViewId()
{
    for (uint16_t i = 0; i < 256; ++i)
    {
        uint8_t candidate = uint8_t((m_nextViewId + i) % 256);
        if (m_usedViewIds.find(candidate) == m_usedViewIds.end())
        {
            m_usedViewIds.insert(candidate);
            m_nextViewId = uint8_t(candidate + 1);
            return candidate;
        }
    }

    return kInvalidViewId;
}

void RenderDeviceBgfx::freeViewId(uint8_t viewId)
{
    if (viewId == kInvalidViewId)
        return;
    m_usedViewIds.erase(viewId);
}

void RenderDeviceBgfx::destroySurfaceResources(ViewSurface& surface, bool releaseViewIds)
{
    if (bgfx::isValid(surface.framebuffer))
    {
        bgfx::destroy(surface.framebuffer);
        surface.framebuffer = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(surface.colorTex))
    {
        bgfx::destroy(surface.colorTex);
        surface.colorTex = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(surface.depthTex))
    {
        bgfx::destroy(surface.depthTex);
        surface.depthTex = BGFX_INVALID_HANDLE;
    }

    for (auto& rb : surface.readbackTex)
    {
        if (bgfx::isValid(rb))
        {
            bgfx::destroy(rb);
        }
        rb = BGFX_INVALID_HANDLE;
    }

    if (releaseViewIds)
    {
        freeViewId(surface.renderViewId);
        freeViewId(surface.blitViewId);
        surface.renderViewId = kInvalidViewId;
        surface.blitViewId   = kInvalidViewId;
    }

    surface.width      = 0;
    surface.height     = 0;
    surface.generation = 0;
}

bool RenderDeviceBgfx::createSurfaceResources(uint32_t w, uint32_t h, ViewSurface& surface)
{
    const uint64_t colorFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_UVW_CLAMP;

    surface.colorTex = bgfx::createTexture2D(
        uint16_t(w), uint16_t(h),
        false, 1,
        bgfx::TextureFormat::RGBA8,
        colorFlags
    );

    surface.depthTex = bgfx::createTexture2D(
        uint16_t(w), uint16_t(h),
        false, 1,
        bgfx::TextureFormat::D24S8,
        BGFX_TEXTURE_RT_WRITE_ONLY
    );

    if (!bgfx::isValid(surface.colorTex) || !bgfx::isValid(surface.depthTex))
    {
        destroySurfaceResources(surface, false);
        return false;
    }

    bgfx::TextureHandle tex[2] = { surface.colorTex, surface.depthTex };
    surface.framebuffer = bgfx::createFrameBuffer(2, tex, true);

    if (!bgfx::isValid(surface.framebuffer))
    {
        destroySurfaceResources(surface, false);
        return false;
    }

    const uint64_t readbackFlags = BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK | BGFX_SAMPLER_UVW_CLAMP;

    for (auto& rb : surface.readbackTex)
    {
        rb = bgfx::createTexture2D(
            uint16_t(w), uint16_t(h),
            false, 1,
            bgfx::TextureFormat::RGBA8,
            readbackFlags
        );
        if (!bgfx::isValid(rb))
        {
            destroySurfaceResources(surface, false);
            return false;
        }
    }

    bgfx::setViewFrameBuffer(surface.renderViewId, surface.framebuffer);
    bgfx::setViewClear(surface.renderViewId, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
    bgfx::setViewRect(surface.renderViewId, 0, 0, uint16_t(w), uint16_t(h));

    bgfx::setViewFrameBuffer(surface.blitViewId, BGFX_INVALID_HANDLE);
    bgfx::setViewRect(surface.blitViewId, 0, 0, uint16_t(w), uint16_t(h));

    return true;
}
