#include "benchmark_runner.h"
#include "demo_controller.h"
#include "quick/terrain_viewport_item.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>

int main(int argc, char* argv[])
{
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    qputenv("QT_OPENGL", QByteArrayLiteral("desktop"));
    qputenv("QSG_RENDER_LOOP", QByteArrayLiteral("basic"));
    QQuickWindow::setSceneGraphBackend(QSGRendererInterface::OpenGL);

    QApplication app(argc, argv);
    QDir::setCurrent(QCoreApplication::applicationDirPath());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Qt offscreen terrain demo and benchmark"));
    parser.addHelpOption();
    QCommandLineOption benchmarkTerrainOption(
        QStringList() << "benchmark-heightmap",
        QStringLiteral("Run a single-file benchmark with the given terrain heightmap."),
        QStringLiteral("path"));
    QCommandLineOption benchmarkDiffuseOption(
        QStringList() << "benchmark-diffuse",
        QStringLiteral("Optional diffuse texture for benchmark mode."),
        QStringLiteral("path"));
    QCommandLineOption benchmarkOutputOption(
        QStringList() << "benchmark-output",
        QStringLiteral("Write benchmark report JSON to this file."),
        QStringLiteral("path"));
    QCommandLineOption benchmarkFramesOption(
        QStringList() << "benchmark-ready-frames",
        QStringLiteral("Number of ready frames to accumulate before exiting benchmark mode."),
        QStringLiteral("count"),
        QStringLiteral("180"));
    QCommandLineOption benchmarkTimeoutOption(
        QStringList() << "benchmark-timeout-ms",
        QStringLiteral("Benchmark timeout in milliseconds."),
        QStringLiteral("ms"),
        QStringLiteral("300000"));
    parser.addOption(benchmarkTerrainOption);
    parser.addOption(benchmarkDiffuseOption);
    parser.addOption(benchmarkOutputOption);
    parser.addOption(benchmarkFramesOption);
    parser.addOption(benchmarkTimeoutOption);
    parser.process(app);

    const QString benchmarkTerrainPath = parser.value(benchmarkTerrainOption);
    const bool benchmarkMode = !benchmarkTerrainPath.isEmpty();
    if (benchmarkMode && !QFileInfo::exists(benchmarkTerrainPath))
    {
        return 2;
    }

    qmlRegisterType<TerrainViewportItem>("Demo", 1, 0, "TerrainViewport");

    DemoController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("demoBenchmarkMode", benchmarkMode);
    engine.rootContext()->setContextProperty("demoController", &controller);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));

    if (engine.rootObjects().isEmpty())
    {
        return -1;
    }

    BenchmarkRunner* runner = nullptr;
    if (benchmarkMode)
    {
        controller.setTerrainSource(QUrl::fromLocalFile(QFileInfo(benchmarkTerrainPath).absoluteFilePath()));
        const QString benchmarkDiffusePath = parser.value(benchmarkDiffuseOption);
        if (!benchmarkDiffusePath.isEmpty())
        {
            controller.setDiffuseSource(QUrl::fromLocalFile(QFileInfo(benchmarkDiffusePath).absoluteFilePath()));
        }

        QObject* rootObject = engine.rootObjects().constFirst();
        auto* viewport = rootObject->findChild<TerrainViewportItem*>(QStringLiteral("terrainViewport0"));
        if (viewport == nullptr)
        {
            return 3;
        }

        BenchmarkRunner::Config config;
        config.terrainPath = QFileInfo(benchmarkTerrainPath).absoluteFilePath();
        config.diffusePath = benchmarkDiffusePath.isEmpty()
            ? QString()
            : QFileInfo(benchmarkDiffusePath).absoluteFilePath();
        config.outputPath = parser.value(benchmarkOutputOption);
        config.targetReadyFrames = parser.value(benchmarkFramesOption).toInt();
        config.timeoutMs = parser.value(benchmarkTimeoutOption).toInt();

        runner = new BenchmarkRunner(&app, viewport, config);
        QObject::connect(runner, &BenchmarkRunner::finished, &app, [&app](int exitCode) {
            app.exit(exitCode);
        });
        runner->start();
    }

    return app.exec();
}
