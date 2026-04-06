import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Demo 1.0

ApplicationWindow {
    id: root
    property bool benchmarkMode: demoBenchmarkMode === true
    property int viewportCount: benchmarkMode ? 1 : 1
    width: 1400
    height: 820
    visible: true
    title: benchmarkMode ? "Terrain Benchmark" : "QML Multi-Viewport Offscreen Demo"

    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        Rectangle {
            visible: !benchmarkMode
            Layout.preferredWidth: benchmarkMode ? 0 : 300
            Layout.fillHeight: true
            color: "#1b1f2a"
            radius: 8
            border.color: "#2b3240"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                Label { text: "Viewport Controls"; color: "white" }
                Label { text: "Left drag: rotate"; color: "#c7d2e0" }
                Label { text: "Wheel: zoom"; color: "#c7d2e0" }

                Button {
                    text: "Load Terrain Height"
                    onClicked: demoController.openTerrainDialog()
                }
                Button {
                    text: "Load Diffuse"
                    onClicked: demoController.openDiffuseDialog()
                }
                Button {
                    text: "Clear"
                    onClicked: demoController.clearSources()
                }

                Label {
                    visible: demoController.benchmarkPresets.length > 0
                    text: "Benchmark Presets"
                    color: "white"
                }
                ComboBox {
                    visible: demoController.benchmarkPresets.length > 0
                    Layout.fillWidth: true
                    model: demoController.benchmarkPresets
                    textRole: "label"
                    currentIndex: demoController.benchmarkPresetIndex
                    onActivated: demoController.benchmarkPresetIndex = currentIndex
                }
                Button {
                    visible: demoController.benchmarkPresets.length > 0
                    text: "Load Benchmark Preset"
                    onClicked: demoController.loadSelectedBenchmarkPreset()
                }

                Label {
                    visible: !benchmarkMode
                    text: "Viewport Layout"
                    color: "white"
                }
                RowLayout {
                    visible: !benchmarkMode
                    Layout.fillWidth: true
                    spacing: 8

                    Button {
                        Layout.fillWidth: true
                        text: "Single"
                        enabled: root.viewportCount !== 1
                        onClicked: root.viewportCount = 1
                    }
                    Button {
                        Layout.fillWidth: true
                        text: "Quad"
                        enabled: root.viewportCount !== 4
                        onClicked: root.viewportCount = 4
                    }
                }
                Label {
                    visible: !benchmarkMode
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: root.viewportCount === 4
                        ? "Quad view is much heavier on Windows because each viewport uses offscreen readback."
                        : "Single view is the smoothest mode for interaction."
                    color: "#c7d2e0"
                }
                Item { Layout.fillHeight: true }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#12161f"
            radius: 8
            border.color: "#2b3240"

            GridLayout {
                anchors.fill: parent
                anchors.margins: 10
                columns: root.viewportCount > 1 ? 2 : 1
                rowSpacing: 8
                columnSpacing: 8

                Repeater {
                    model: root.viewportCount
                    delegate: Rectangle {
                        color: "#0f141d"
                        border.color: "#2c3546"
                        radius: 6
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        implicitWidth: 420
                        implicitHeight: 320

                        TerrainViewport {
                            objectName: "terrainViewport" + index
                            anchors.fill: parent
                            anchors.margins: 1
                            terrainSource: demoController.terrainSource
                            diffuseSource: demoController.diffuseSource
                        }
                    }
                }
            }
        }
    }
}

