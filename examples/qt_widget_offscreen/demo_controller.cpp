#include "demo_controller.h"

#include <QCoreApplication>
#include <QFileDialog>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

#ifndef DEMO_SOURCE_ROOT
#define DEMO_SOURCE_ROOT ""
#endif

DemoController::DemoController(QObject* parent)
    : QObject(parent)
{
    loadBenchmarkManifest();
}

void DemoController::setTerrainSource(const QUrl& source)
{
    if (m_terrainSource == source)
    {
        return;
    }

    qInfo().noquote() << "[DemoController] setTerrainSource"
                      << source.toString()
                      << "localPath=" << (source.isLocalFile() ? source.toLocalFile() : QString())
                      << "exists=" << (source.isLocalFile() ? QFileInfo::exists(source.toLocalFile()) : false);

    m_terrainSource = source;
    emit sourcesChanged();
}

void DemoController::setDiffuseSource(const QUrl& source)
{
    if (m_diffuseSource == source)
    {
        return;
    }

    qInfo().noquote() << "[DemoController] setDiffuseSource"
                      << source.toString()
                      << "localPath=" << (source.isLocalFile() ? source.toLocalFile() : QString())
                      << "exists=" << (source.isLocalFile() ? QFileInfo::exists(source.toLocalFile()) : false);

    m_diffuseSource = source;
    emit sourcesChanged();
}

void DemoController::setBenchmarkPresetIndex(int index)
{
    if (m_benchmarkPresetIndex == index)
    {
        return;
    }

    m_benchmarkPresetIndex = index;
    emit benchmarkPresetIndexChanged();
}

void DemoController::openTerrainDialog()
{
    const QString file = QFileDialog::getOpenFileName(
        nullptr,
        QStringLiteral("Select Terrain Height Field"),
        QString(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.r16);;All Files (*)"));
    if (file.isEmpty())
    {
        return;
    }

    setTerrainSource(QUrl::fromLocalFile(file));
}

void DemoController::openDiffuseDialog()
{
    const QString file = QFileDialog::getOpenFileName(
        nullptr,
        QStringLiteral("Select Diffuse Texture"),
        QString(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff);;All Files (*)"));
    if (file.isEmpty())
    {
        return;
    }

    setDiffuseSource(QUrl::fromLocalFile(file));
}

void DemoController::clearSources()
{
    m_terrainSource = QUrl();
    m_diffuseSource = QUrl();
    emit sourcesChanged();
}

void DemoController::loadSelectedBenchmarkPreset()
{
    loadBenchmarkPreset(m_benchmarkPresetIndex);
}

void DemoController::loadBenchmarkPreset(int index)
{
    if (index < 0 || index >= m_benchmarkPresets.size())
    {
        qWarning().noquote() << "[DemoController] loadBenchmarkPreset invalid index"
                             << index << "count=" << m_benchmarkPresets.size();
        return;
    }

    const QVariantMap preset = m_benchmarkPresets.at(index).toMap();
    const QString terrainPath = preset.value(QStringLiteral("terrainPath")).toString();
    if (terrainPath.isEmpty())
    {
        qWarning().noquote() << "[DemoController] loadBenchmarkPreset empty terrainPath index=" << index;
        return;
    }

    qInfo().noquote() << "[DemoController] loadBenchmarkPreset"
                      << preset.value(QStringLiteral("name")).toString()
                      << terrainPath
                      << "exists=" << QFileInfo::exists(terrainPath);

    setBenchmarkPresetIndex(index);
    setTerrainSource(QUrl::fromLocalFile(terrainPath));
    setDiffuseSource(QUrl());
}

QString DemoController::resolveRepoFile(const QString& repoRelativePath) const
{
    if (repoRelativePath.isEmpty())
    {
        return QString();
    }

    const QFileInfo inputInfo(repoRelativePath);
    if (inputInfo.isAbsolute())
    {
        return inputInfo.absoluteFilePath();
    }

    const QString sourceRoot = QString::fromUtf8(DEMO_SOURCE_ROOT);
    if (!sourceRoot.isEmpty())
    {
        return QDir(sourceRoot).absoluteFilePath(repoRelativePath);
    }

    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(repoRelativePath);
}

void DemoController::loadBenchmarkManifest()
{
    const QString manifestPath = resolveRepoFile(QStringLiteral("assets/benchmarks/terrain/usgs_3dep/manifest.json"));
    qInfo().noquote() << "[DemoController] loadBenchmarkManifest path=" << manifestPath;
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning().noquote() << "[DemoController] loadBenchmarkManifest open failed";
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray())
    {
        qWarning().noquote() << "[DemoController] loadBenchmarkManifest invalid json root";
        return;
    }

    QVariantList presets;
    int missingFileCount = 0;
    const QJsonArray items = doc.array();
    for (const QJsonValue& item : items)
    {
        if (!item.isObject())
        {
            continue;
        }

        const QJsonObject object = item.toObject();
        const QString relativePath = object.value(QStringLiteral("png_path")).toString();
        const QString terrainPath = resolveRepoFile(relativePath);
        if (terrainPath.isEmpty() || !QFileInfo::exists(terrainPath))
        {
            ++missingFileCount;
            continue;
        }

        const int width = object.value(QStringLiteral("width")).toInt();
        const int height = object.value(QStringLiteral("height")).toInt();
        const bool derived = object.contains(QStringLiteral("derived_from"));
        QVariantMap preset;
        preset.insert(QStringLiteral("name"), object.value(QStringLiteral("name")).toString());
        preset.insert(QStringLiteral("terrainPath"), terrainPath);
        preset.insert(QStringLiteral("resolution"), QStringLiteral("%1x%2").arg(width).arg(height));
        preset.insert(QStringLiteral("source"), derived ? QStringLiteral("derived") : QStringLiteral("direct"));
        preset.insert(
            QStringLiteral("label"),
            QStringLiteral("%1  [%2, %3]")
                .arg(object.value(QStringLiteral("name")).toString(),
                     QStringLiteral("%1x%2").arg(width).arg(height),
                     derived ? QStringLiteral("derived") : QStringLiteral("direct"))
        );
        presets.push_back(preset);
    }

    if (presets.isEmpty())
    {
        qWarning().noquote() << "[DemoController] loadBenchmarkManifest no valid presets";
        return;
    }

    m_benchmarkPresets = presets;
    if (m_benchmarkPresetIndex < 0)
    {
        m_benchmarkPresetIndex = 0;
    }
    qInfo().noquote() << "[DemoController] loadBenchmarkManifest loaded presets=" << m_benchmarkPresets.size()
                      << "missingFiles=" << missingFileCount;
    emit benchmarkPresetsChanged();
    emit benchmarkPresetIndexChanged();
}
