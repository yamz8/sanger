import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Sanger

ApplicationWindow {
    id: win
    width: 1240
    height: 800
    minimumWidth: 760
    minimumHeight: 520
    visible: true
    title: backend.hasReference ? "Sanger — " + backend.referenceName : "Sanger"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    property bool quitting: false
    readonly property bool keysEnabled: !quitDialog.visible
    readonly property bool hasReads: backend.readCount > 0
    readonly property var selected: backend.selectedRead >= 0 ? backend.reads[backend.selectedRead] : null
    // Reading translator.language makes number formats follow a language switch.
    readonly property var loc: { translator.language; return Qt.locale() }

    function number(value) { return Number(value).toLocaleString(loc, 'f', 0) }
    function decimal(value, places) { return Number(value).toLocaleString(loc, 'f', places) }
    function quit() {
        if (backend.dirty) quitDialog.open()
        else { quitting = true; Qt.quit() }
    }

    onClosing: (close) => {
        if (!quitting && backend.dirty) {
            close.accepted = false
            quitDialog.open()
        }
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Esc"; context: Qt.ApplicationShortcut; enabled: win.helpVisible; onActivated: win.helpVisible = false }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: win.quit() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.primaryAction() }
    Shortcut { sequences: ["O", StandardKey.Open]; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.openFiles() }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.openReference() }
    Shortcut { sequences: ["E", StandardKey.Save]; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.exportFasta() }
    Shortcut { sequence: "N"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.nextMismatch() }
    Shortcut { sequence: "Shift+N"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.previousMismatch() }
    Shortcut { sequence: "Left"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.moveCursor(-1) }
    Shortcut { sequence: "Right"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.moveCursor(1) }
    Shortcut { sequence: "Shift+Left"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.moveCursor(-10) }
    Shortcut { sequence: "Shift+Right"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.moveCursor(10) }
    Shortcut { sequence: "Home"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.cursorToStart() }
    Shortcut { sequence: "End"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.cursorToEnd() }
    Shortcut { sequence: "Up"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.selectRelative(-1) }
    Shortcut { sequence: "Down"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.selectRelative(1) }
    Shortcut { sequence: "["; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.trimStartAtCursor() }
    Shortcut { sequence: "]"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.trimEndAtCursor() }
    Shortcut { sequence: "\\"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.resetTrim() }
    Shortcut { sequence: "T"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.stricterTrim() }
    Shortcut { sequence: "Shift+T"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.looserTrim() }
    Shortcut { sequence: "X"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.toggleIncluded() }
    Shortcut { sequences: [StandardKey.ZoomIn, "Ctrl+="]; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: trace.zoomBy(1.4) }
    Shortcut { sequences: [StandardKey.ZoomOut]; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: trace.zoomBy(1 / 1.4) }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.redo() }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        DropArea {
            id: drop
            anchors.fill: parent
            onDropped: (event) => {
                if (event.hasUrls) {
                    backend.loadUrls(event.urls)
                    event.acceptProposedAction()
                }
            }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Label {
                    text: "Sanger"
                    font.pixelSize: 17
                    font.bold: true
                }
                Label {
                    Layout.leftMargin: 8
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: "#8a8a94"
                    text: backend.hasReference
                          ? qsTr("Reference %1, %Ln bases", "", backend.referenceLength).arg(backend.referenceName)
                          : (win.hasReads ? qsTr("No reference yet") : "")
                }
                Label {
                    // Narrow windows keep only the buttons, whose tooltips say the rest.
                    visible: win.hasReads && win.width > 1080
                    color: "#8a8a94"
                    text: qsTr("Trim at error rate %1").arg(Number(backend.errorLimit).toLocaleString(win.loc, 'g', 3))
                }
                Button {
                    visible: win.hasReads
                    flat: true
                    text: qsTr("Stricter")
                    onClicked: backend.stricterTrim()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Trim more of the low-quality ends (T)")
                }
                Button {
                    visible: win.hasReads
                    flat: true
                    text: qsTr("Looser")
                    onClicked: backend.looserTrim()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Keep more of the ends (Shift+T)")
                }
                ToolSeparator { visible: win.hasReads }
                Button {
                    flat: true
                    text: qsTr("Open")
                    onClicked: backend.openFiles()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Open traces or a reference (O)")
                }
                Button {
                    flat: true
                    text: qsTr("Reference")
                    onClicked: backend.openReference()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Open a FASTA reference (R)")
                }
                Button {
                    highlighted: true
                    enabled: backend.canExport
                    text: qsTr("Export FASTA")
                    onClicked: backend.exportFasta()
                    ToolTip.visible: hovered
                    ToolTip.text: backend.hasReference ? qsTr("Export the consensus (E)") : qsTr("Export the trimmed reads (E)")
                }
            }

            Rectangle {
                id: alignmentPanel
                visible: win.hasReads
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(alignment.contentHeight, win.height * 0.42)
                Layout.minimumHeight: Math.min(alignment.contentHeight, 120)
                color: "#141418"
                radius: theme.radius
                clip: true

                AlignmentView {
                    id: alignment
                    anchors.fill: parent
                    session: backend
                    accent: theme.accent
                    referenceLabel: qsTr("Reference")
                    consensusLabel: qsTr("Consensus")
                    noMatchLabel: qsTr("Does not match the reference")
                }
                MouseArea {
                    anchors.fill: parent
                    preventStealing: true
                    property bool overview: false
                    function pick(mouse) {
                        if (overview) {
                            backend.setCursor(alignment.overviewColumnAt(mouse.x))
                            return
                        }
                        const read = alignment.readAt(mouse.y)
                        if (read >= 0)
                            backend.select(read)
                        const column = alignment.columnAt(mouse.x)
                        if (column >= 0)
                            backend.setCursor(column)
                    }
                    onPressed: (mouse) => { overview = alignment.isOverview(mouse.y); pick(mouse) }
                    onPositionChanged: (mouse) => { if (pressed) pick(mouse) }
                }
                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: (event) => {
                        const horizontal = event.angleDelta.x !== 0 ? event.angleDelta.x : 0
                        if (horizontal !== 0)
                            alignment.scrollBy(-Math.round(horizontal / 20))
                        else if (event.modifiers & Qt.ShiftModifier)
                            alignment.scrollBy(-Math.round(event.angleDelta.y / 20))
                        else
                            alignment.scrollRows(event.angleDelta.y > 0 ? -1 : 1)
                    }
                }
            }

            RowLayout {
                visible: win.hasReads
                Layout.fillWidth: true
                Layout.leftMargin: 4
                Layout.rightMargin: 4
                spacing: 14

                Label {
                    text: win.selected ? win.selected.name : ""
                    font.bold: true
                    elide: Text.ElideMiddle
                    Layout.maximumWidth: 260
                }
                Label {
                    color: "#a8a8b2"
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: {
                        const read = win.selected
                        if (!read)
                            return ""
                        const parts = []
                        if (backend.hasReference && read.aligned)
                            parts.push(read.reverse ? qsTr("reverse strand") : qsTr("forward strand"))
                        if (read.trimmedLength > 0)
                            parts.push(qsTr("bases %1–%2 of %3").arg(win.number(read.trimStart)).arg(win.number(read.trimEnd)).arg(win.number(read.length)))
                        else
                            parts.push(qsTr("trimmed away entirely"))
                        if (read.meanQuality >= 0)
                            parts.push(qsTr("mean Q%1").arg(win.number(read.meanQuality)))
                        if (backend.hasReference && read.aligned)
                            parts.push(qsTr("%1% identity").arg(win.decimal(read.identity * 100, 1)))
                        if (read.manual)
                            parts.push(qsTr("trimmed by hand"))
                        if (!read.included)
                            parts.push(qsTr("left out"))
                        return parts.join("  ·  ")
                    }
                }
                Label {
                    color: "#a8a8b2"
                    elide: Text.ElideRight
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: implicitWidth
                    Layout.fillWidth: true
                    text: {
                        const parts = []
                        if (backend.cursorReference > 0)
                            parts.push(backend.cursorInsert > 0
                                       ? qsTr("Reference %1, insertion %2").arg(win.number(backend.cursorReference)).arg(backend.cursorInsert)
                                       : qsTr("Reference %1").arg(win.number(backend.cursorReference)))
                        if (backend.cursorBase > 0)
                            parts.push(qsTr("Base %1").arg(win.number(backend.cursorBase)))
                        else if (backend.hasReference && win.selected)
                            parts.push(win.selected.aligned ? qsTr("not in this read") : qsTr("this read does not match the reference"))
                        if (backend.cursorQuality >= 0)
                            parts.push("Q" + win.number(backend.cursorQuality))
                        return parts.join("  ·  ")
                    }
                }
                Label {
                    visible: backend.hasReference
                    color: backend.mismatchNumber > 0 ? theme.accent : "#a8a8b2"
                    text: backend.mismatchNumber > 0
                          ? qsTr("Mismatch %1 of %2").arg(win.number(backend.mismatchNumber)).arg(win.number(backend.mismatchCount))
                          : qsTr("%n mismatches", "", backend.mismatchCount)
                }
            }

            Rectangle {
                visible: win.hasReads
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#141418"
                radius: theme.radius
                clip: true

                TraceView {
                    id: trace
                    anchors.fill: parent
                    anchors.topMargin: 6
                    anchors.bottomMargin: 6
                    session: backend
                    accent: theme.accent
                    trimmedLabel: qsTr("Trimmed")
                }
                MouseArea {
                    anchors.fill: parent
                    preventStealing: true
                    cursorShape: dragging ? Qt.ClosedHandCursor : Qt.ArrowCursor
                    property real lastX: 0
                    property real pressX: 0
                    property bool dragging: false
                    onPressed: (mouse) => { pressX = lastX = mouse.x; dragging = false }
                    onPositionChanged: (mouse) => {
                        if (Math.abs(mouse.x - pressX) > 4)
                            dragging = true
                        if (dragging) {
                            trace.pan(lastX - mouse.x)
                            lastX = mouse.x
                        }
                    }
                    onReleased: (mouse) => {
                        if (!dragging)
                            backend.cursorToBase(trace.baseAt(mouse.x))
                        dragging = false
                    }
                }
                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: (event) => {
                        if (event.modifiers & Qt.ControlModifier)
                            trace.zoomBy(event.angleDelta.y > 0 ? 1.2 : 1 / 1.2)
                        else
                            trace.pan(-(event.angleDelta.x !== 0 ? event.angleDelta.x : event.angleDelta.y))
                    }
                }
            }

            Item {
                visible: !win.hasReads
                Layout.fillWidth: true
                Layout.fillHeight: true

                Column {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 40, 560)
                    spacing: 14

                    Label {
                        width: parent.width
                        text: qsTr("Drop AB1 or SCF traces here, with a FASTA reference to align them to.")
                        font.pixelSize: 16
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Label {
                        width: parent.width
                        color: "#8a8a94"
                        text: qsTr("Press Space to open files, or ? for every shortcut.")
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Button {
                        anchors.horizontalCenter: parent.horizontalCenter
                        highlighted: true
                        text: qsTr("Open Traces")
                        onClicked: backend.openFiles()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 4
                Layout.rightMargin: 4
                spacing: 16

                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    color: backend.messageIsError ? "#ff6b6b" : "#a8a8b2"
                    text: backend.message.replace(/\n/g, "  ")
                }
                Label {
                    visible: backend.hasReference && backend.consensusLength > 0
                    color: "#8a8a94"
                    text: qsTr("Consensus %Ln bases", "", backend.consensusLength)
                }
                Label {
                    color: "#6e6e78"
                    text: qsTr("? Shortcuts")
                }
            }
        }

        Rectangle {
            anchors.fill: parent
            visible: drop.containsDrag
            color: Qt.rgba(0, 0, 0, 0.55)
            border.color: theme.accent
            border.width: 2
            radius: theme.radius
            Label {
                anchors.centerIn: parent
                text: qsTr("Drop to open")
                font.pixelSize: 20
            }
        }

        ShortcutHelp {
            anchors.fill: parent
            visible: win.helpVisible
            onDismissed: win.helpVisible = false
        }

        Dialog {
            id: quitDialog
            anchors.centerIn: parent
            modal: true
            title: qsTr("Quit without exporting?")
            Label {
                text: qsTr("The consensus has changed since it was last exported.")
                wrapMode: Text.WordWrap
                width: Math.min(420, win.width - 80)
            }
            footer: DialogButtonBox {
                Button {
                    text: qsTr("Cancel")
                    flat: true
                    DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                }
                Button {
                    text: qsTr("Quit")
                    flat: true
                    DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
                    onClicked: { win.quitting = true; Qt.quit() }
                }
            }
        }
    }
}
