#pragma once

#include "quick/terrain_viewport_item.h"

#include <QObject>
#include <QString>

class BenchmarkRunner : public QObject
{
    Q_OBJECT

public:
    struct Config
    {
        QString terrainPath;
        QString diffusePath;
        QString outputPath;
        int targetReadyFrames = 180;
        int timeoutMs = 300000;
    };

    BenchmarkRunner(QObject* parent, TerrainViewportItem* viewport, const Config& config);

    void start();

signals:
    void finished(int exitCode);

private:
    void poll();
    int writeReport(const ViewportBenchmarkStats& stats) const;

    TerrainViewportItem* m_viewport = nullptr;
    Config m_config;
    int64_t m_startedAtMs = 0;
};
