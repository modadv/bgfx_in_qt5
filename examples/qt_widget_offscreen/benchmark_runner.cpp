#include "benchmark_runner.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

BenchmarkRunner::BenchmarkRunner(QObject* parent, TerrainViewportItem* viewport, const Config& config)
    : QObject(parent)
    , m_viewport(viewport)
    , m_config(config)
{
}

void BenchmarkRunner::start()
{
    m_startedAtMs = QDateTime::currentMSecsSinceEpoch();
    QTimer::singleShot(250, this, [this]() { poll(); });
}

void BenchmarkRunner::poll()
{
    if (m_viewport == nullptr)
    {
        emit finished(2);
        return;
    }

    const ViewportBenchmarkStats stats = m_viewport->benchmarkStats();
    const int64_t nowMs = QDateTime::currentMSecsSinceEpoch();
    if (stats.terrainReady && stats.readyFrameCount >= uint64_t(m_config.targetReadyFrames))
    {
        emit finished(writeReport(stats));
        return;
    }

    if ((nowMs - m_startedAtMs) >= m_config.timeoutMs)
    {
        emit finished(writeReport(stats));
        return;
    }

    QTimer::singleShot(250, this, [this]() { poll(); });
}

int BenchmarkRunner::writeReport(const ViewportBenchmarkStats& stats) const
{
    if (m_config.outputPath.isEmpty())
    {
        return stats.terrainReady ? 0 : 1;
    }

    const QFileInfo terrainInfo(m_config.terrainPath);
    QJsonObject report;
    report.insert("terrain_path", QJsonValue::fromVariant(m_config.terrainPath));
    report.insert("diffuse_path", QJsonValue::fromVariant(m_config.diffusePath));
    report.insert("terrain_file_bytes", terrainInfo.exists() ? qint64(terrainInfo.size()) : qint64(-1));
    report.insert("terrain_ready", stats.terrainReady);
    report.insert("viewport_width", int(stats.viewportWidth));
    report.insert("viewport_height", int(stats.viewportHeight));
    report.insert("terrain_width", int(stats.terrainWidth));
    report.insert("terrain_height", int(stats.terrainHeight));
    report.insert("total_frame_count", qint64(stats.totalFrameCount));
    report.insert("ready_frame_count", qint64(stats.readyFrameCount));
    report.insert("last_frame_ms", stats.lastFrameMs);
    report.insert("avg_ready_frame_ms", stats.readyFrameAverageMs);
    report.insert("max_ready_frame_ms", stats.readyFrameMaxMs);
    report.insert("ready_elapsed_ms", stats.readyElapsedMs);
    report.insert("load_time_ms", stats.loadTimeMs);
    report.insert("cpu_smap_time_ms", stats.cpuSmapTimeMs);
    report.insert("gpu_smap_time_ms", stats.gpuSmapTimeMs);
    report.insert("avg_ready_fps", stats.readyFrameAverageMs > 0.0 ? 1000.0 / stats.readyFrameAverageMs : 0.0);
    report.insert("target_ready_frames", m_config.targetReadyFrames);
    report.insert("timeout_ms", m_config.timeoutMs);
    report.insert("finished_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QFile file(m_config.outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return 3;
    }

    file.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    file.close();
    return stats.terrainReady ? 0 : 1;
}
