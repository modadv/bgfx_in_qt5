#include "quick/terrain_scene.h"
#include "render/core/render_device_bgfx.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{
struct BenchmarkConfig
{
    QString terrainPath;
    QString diffusePath;
    QString outputPath;
    int width = 1280;
    int height = 720;
    int targetReadyFrames = 180;
    int timeoutFrames = 3600;
};

void appendTrace(const QString& outputPath, const QString& line)
{
    if (outputPath.isEmpty())
    {
        return;
    }

    QFile file(outputPath + QStringLiteral(".trace"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        return;
    }

    file.write(line.toUtf8());
    file.write("\n");
    file.close();
}

int writeReport(const BenchmarkConfig& config,
                bool terrainReady,
                uint64_t totalFrames,
                uint64_t readyFrames,
                double lastFrameMs,
                double avgReadyFrameMs,
                double maxReadyFrameMs,
                double readyElapsedMs,
                const TerrainScene& scene)
{
    if (config.outputPath.isEmpty())
    {
        return terrainReady ? 0 : 1;
    }

    const QFileInfo terrainInfo(config.terrainPath);
    QJsonObject report;
    report.insert("terrain_path", QDir::toNativeSeparators(config.terrainPath));
    report.insert("diffuse_path", QDir::toNativeSeparators(config.diffusePath));
    report.insert("terrain_file_bytes", terrainInfo.exists() ? qint64(terrainInfo.size()) : qint64(-1));
    report.insert("terrain_ready", terrainReady);
    report.insert("viewport_width", config.width);
    report.insert("viewport_height", config.height);
    report.insert("terrain_width", int(scene.terrainWidth()));
    report.insert("terrain_height", int(scene.terrainHeight()));
    report.insert("total_frame_count", qint64(totalFrames));
    report.insert("ready_frame_count", qint64(readyFrames));
    report.insert("last_frame_ms", lastFrameMs);
    report.insert("avg_ready_frame_ms", avgReadyFrameMs);
    report.insert("max_ready_frame_ms", maxReadyFrameMs);
    report.insert("ready_elapsed_ms", readyElapsedMs);
    report.insert("load_time_ms", scene.loadTimeMs());
    report.insert("cpu_smap_time_ms", scene.cpuSmapTimeMs());
    report.insert("gpu_smap_time_ms", scene.gpuSmapTimeMs());
    report.insert("avg_ready_fps", avgReadyFrameMs > 0.0 ? 1000.0 / avgReadyFrameMs : 0.0);
    report.insert("target_ready_frames", config.targetReadyFrames);
    report.insert("timeout_frames", config.timeoutFrames);
    report.insert("finished_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QFile file(config.outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return 3;
    }

    file.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    file.close();
    return terrainReady ? 0 : 1;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("terrain_headless_benchmark"));
    QDir::setCurrent(QCoreApplication::applicationDirPath());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Headless offscreen terrain benchmark"));
    parser.addHelpOption();

    QCommandLineOption terrainOption(
        QStringList() << "heightmap",
        QStringLiteral("Input terrain heightmap path."),
        QStringLiteral("path"));
    QCommandLineOption diffuseOption(
        QStringList() << "diffuse",
        QStringLiteral("Optional diffuse texture path."),
        QStringLiteral("path"));
    QCommandLineOption outputOption(
        QStringList() << "output",
        QStringLiteral("Benchmark report JSON output path."),
        QStringLiteral("path"));
    QCommandLineOption widthOption(
        QStringList() << "width",
        QStringLiteral("Offscreen surface width."),
        QStringLiteral("pixels"),
        QStringLiteral("1280"));
    QCommandLineOption heightOption(
        QStringList() << "height",
        QStringLiteral("Offscreen surface height."),
        QStringLiteral("pixels"),
        QStringLiteral("720"));
    QCommandLineOption readyFramesOption(
        QStringList() << "ready-frames",
        QStringLiteral("Number of ready frames to accumulate before exit."),
        QStringLiteral("count"),
        QStringLiteral("180"));
    QCommandLineOption timeoutFramesOption(
        QStringList() << "timeout-frames",
        QStringLiteral("Maximum frame budget before failing."),
        QStringLiteral("count"),
        QStringLiteral("3600"));

    parser.addOption(terrainOption);
    parser.addOption(diffuseOption);
    parser.addOption(outputOption);
    parser.addOption(widthOption);
    parser.addOption(heightOption);
    parser.addOption(readyFramesOption);
    parser.addOption(timeoutFramesOption);
    parser.process(app);

    BenchmarkConfig config;
    config.terrainPath = QFileInfo(parser.value(terrainOption)).absoluteFilePath();
    config.diffusePath = parser.value(diffuseOption).isEmpty()
        ? QString()
        : QFileInfo(parser.value(diffuseOption)).absoluteFilePath();
    config.outputPath = parser.value(outputOption).isEmpty()
        ? QString()
        : QFileInfo(parser.value(outputOption)).absoluteFilePath();
    config.width = parser.value(widthOption).toInt();
    config.height = parser.value(heightOption).toInt();
    config.targetReadyFrames = parser.value(readyFramesOption).toInt();
    config.timeoutFrames = parser.value(timeoutFramesOption).toInt();

    if (config.terrainPath.isEmpty() || !QFileInfo::exists(config.terrainPath))
    {
        return 2;
    }

    appendTrace(config.outputPath, QStringLiteral("start"));
    RenderDeviceBgfx& device = RenderDeviceBgfx::instance();
    appendTrace(config.outputPath, QStringLiteral("before_acquire"));
    device.acquire(uint32_t(config.width), uint32_t(config.height));
    appendTrace(config.outputPath, QStringLiteral("after_acquire"));
    if (!device.isInitialized())
    {
        appendTrace(config.outputPath, QStringLiteral("device_not_initialized"));
        return 4;
    }

    RenderDeviceBgfx::ViewSurface surface;
    appendTrace(config.outputPath, QStringLiteral("before_create_surface"));
    if (!device.createSurface(uint32_t(config.width), uint32_t(config.height), surface))
    {
        appendTrace(config.outputPath, QStringLiteral("create_surface_failed"));
        device.shutdown();
        return 5;
    }
    appendTrace(config.outputPath, QStringLiteral("after_create_surface"));

    int exitCode = 1;
    {
        TerrainScene scene;
        appendTrace(config.outputPath, QStringLiteral("scene_created"));
        scene.setRenderTarget(surface.renderViewId, surface.framebuffer);
        scene.resize(uint32_t(config.width), uint32_t(config.height));
        scene.loadTerrainData(config.terrainPath);
        if (!config.diffusePath.isEmpty() && QFileInfo::exists(config.diffusePath))
        {
            scene.loadDiffuse(config.diffusePath);
        }
        appendTrace(config.outputPath, QStringLiteral("scene_loaded"));

        QElapsedTimer wallTimer;
        wallTimer.start();

        uint64_t totalFrames = 0;
        uint64_t readyFrames = 0;
        double lastFrameMs = 0.0;
        double readyFrameSumMs = 0.0;
        double readyFrameMaxMs = 0.0;
        double readyElapsedMs = 0.0;
        bool readySeen = false;

        for (int frame = 0; frame < config.timeoutFrames; ++frame)
        {
            QElapsedTimer frameTimer;
            frameTimer.start();
            scene.update(1.0f / 60.0f);
            device.endFrame();
            lastFrameMs = double(frameTimer.nsecsElapsed()) / 1000000.0;
            ++totalFrames;

            if (!scene.isTerrainDataReady())
            {
                continue;
            }

            if (!readySeen)
            {
                readySeen = true;
                readyElapsedMs = double(wallTimer.elapsed());
            }

            ++readyFrames;
            readyFrameSumMs += lastFrameMs;
            readyFrameMaxMs = qMax(readyFrameMaxMs, lastFrameMs);

            if (readyFrames >= uint64_t(config.targetReadyFrames))
            {
                appendTrace(config.outputPath, QStringLiteral("ready_complete"));
                exitCode = writeReport(
                    config,
                    true,
                    totalFrames,
                    readyFrames,
                    lastFrameMs,
                    readyFrameSumMs / double(readyFrames),
                    readyFrameMaxMs,
                    readyElapsedMs,
                    scene);
                break;
            }
        }

        if (readyFrames < uint64_t(config.targetReadyFrames))
        {
            appendTrace(config.outputPath, QStringLiteral("timeout_or_not_ready"));
            exitCode = writeReport(
                config,
                readySeen,
                totalFrames,
                readyFrames,
                lastFrameMs,
                readyFrames > 0 ? readyFrameSumMs / double(readyFrames) : 0.0,
                readyFrameMaxMs,
                readyElapsedMs,
                scene);
        }
    }

    appendTrace(config.outputPath, QStringLiteral("before_shutdown"));
    device.destroySurface(surface);
    device.shutdown();
    appendTrace(config.outputPath, QStringLiteral("done"));
    return exitCode;
}
