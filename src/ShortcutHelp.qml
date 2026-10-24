import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Every shortcut, over the window, until ? or Esc or a click.
Rectangle {
    id: overlay
    color: Qt.rgba(0, 0, 0, 0.72)
    signal dismissed()

    readonly property var shortcuts: [
        ["Space", qsTr("Next mismatch, or open files when there are none")],
        ["N  /  Shift+N", qsTr("Next or previous mismatch")],
        ["←  →", qsTr("Move the cursor one column")],
        ["Shift+←  →", qsTr("Move the cursor ten columns")],
        ["Home  /  End", qsTr("Start or end of the consensus")],
        ["↑  ↓", qsTr("Select the read above or below")],
        ["[  /  ]", qsTr("Trim the selected read's start or end at the cursor")],
        ["\\", qsTr("Trim the selected read by quality again")],
        ["T  /  Shift+T", qsTr("Stricter or looser quality trim for every read")],
        ["X", qsTr("Leave the selected read out of the consensus, or back in")],
        ["O", qsTr("Open traces or a reference")],
        ["R", qsTr("Open a FASTA reference")],
        ["E", qsTr("Export the consensus as FASTA")],
        ["Ctrl+=  /  Ctrl+-", qsTr("Zoom the chromatogram")],
        ["Ctrl+Z  /  Ctrl+Shift+Z", qsTr("Undo or redo")],
        ["?", qsTr("Show or hide these shortcuts")],
        ["Q", qsTr("Quit")]
    ]

    MouseArea {
        anchors.fill: parent
        onClicked: overlay.dismissed()
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, grid.implicitWidth + 56)
        height: Math.min(parent.height - 48, content.implicitHeight + 48)
        color: "#18181d"
        radius: theme.radius
        border.color: "#2a2a32"

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: 24
            spacing: 14

            Label {
                text: qsTr("Shortcuts")
                font.pixelSize: 18
                font.bold: true
            }
            GridLayout {
                id: grid
                columns: 2
                columnSpacing: 28
                rowSpacing: 7
                Repeater {
                    model: overlay.shortcuts
                    delegate: Label {
                        required property var modelData
                        required property int index
                        text: modelData[0]
                        color: theme.accent
                        font.family: "monospace"
                        Layout.row: index
                        Layout.column: 0
                    }
                }
                Repeater {
                    model: overlay.shortcuts
                    delegate: Label {
                        required property var modelData
                        required property int index
                        text: modelData[1]
                        Layout.row: index
                        Layout.column: 1
                        Layout.maximumWidth: 460
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
