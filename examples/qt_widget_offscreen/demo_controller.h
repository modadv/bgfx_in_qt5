#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantList>

class DemoController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUrl terrainSource READ terrainSource NOTIFY sourcesChanged)
    Q_PROPERTY(QUrl diffuseSource READ diffuseSource NOTIFY sourcesChanged)
    Q_PROPERTY(QVariantList benchmarkPresets READ benchmarkPresets NOTIFY benchmarkPresetsChanged)
    Q_PROPERTY(int benchmarkPresetIndex READ benchmarkPresetIndex WRITE setBenchmarkPresetIndex NOTIFY benchmarkPresetIndexChanged)

public:
    explicit DemoController(QObject* parent = nullptr);

    QUrl terrainSource() const { return m_terrainSource; }
    QUrl diffuseSource() const { return m_diffuseSource; }
    QVariantList benchmarkPresets() const { return m_benchmarkPresets; }
    int benchmarkPresetIndex() const { return m_benchmarkPresetIndex; }
    void setTerrainSource(const QUrl& source);
    void setDiffuseSource(const QUrl& source);
    void setBenchmarkPresetIndex(int index);

    Q_INVOKABLE void openTerrainDialog();
    Q_INVOKABLE void openDiffuseDialog();
    Q_INVOKABLE void clearSources();
    Q_INVOKABLE void loadSelectedBenchmarkPreset();
    Q_INVOKABLE void loadBenchmarkPreset(int index);

signals:
    void sourcesChanged();
    void benchmarkPresetsChanged();
    void benchmarkPresetIndexChanged();

private:
    void loadBenchmarkManifest();
    QString resolveRepoFile(const QString& repoRelativePath) const;

    QUrl m_terrainSource;
    QUrl m_diffuseSource;
    QVariantList m_benchmarkPresets;
    int m_benchmarkPresetIndex = -1;
};
