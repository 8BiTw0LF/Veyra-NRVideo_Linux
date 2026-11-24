import QtQuick

// Modal for choosing a V4L2 capture card (or a window / screen through the compositor's share dialog), its video
// mode and the audio source.
Item {
    id: dlg
    property var tr: function (k) { return k }
    property var devices: []
    property var modes: []
    property var sources: []        // [{name, desc}]
    property int devIdx: 0
    property int modeIdx: 0
    property int srcIdx: 0          // 0 = no audio
    property string help: ""        // why no card is listed
    signal closed()
    signal remotePlay()             // "PlayStation Remote Play" was chosen

    visible: false
    property bool testView: false   // "Find the best signal" replaces the lists while it runs and shows its results
    function openDialog() { refresh(); testView = player.signalTestRunning; visible = true }
    function closeDialog() {
        if (player.signalTestRunning) player.stopSignalTest()
        testView = false
        visible = false
        closed()
    }
    readonly property bool isShare: devices.length > 0 && devIdx < devices.length && devices[devIdx].path === "screencast"
    readonly property int cardCount: devices.length - (player.screenCastAvailable() ? 1 : 0)
    function refresh() {
        var dv = player.captureDevices()
        if (player.screenCastAvailable()) dv.push({ path: "screencast", name: tr("screenShare") })
        devices = dv
        help = player.captureHelp()
        var s = [{ name: "", desc: tr("noAudio") }]
        var list = player.audioSources()
        list.push({ name: "@default", desc: tr("defaultInput") })
        for (var i = 0; i < list.length; ++i) s.push(list[i])
        sources = s
        var last = player.lastCaptureDevice()
        devIdx = 0
        for (var d = 0; d < devices.length; ++d) if (devices[d].path === last) devIdx = d
        pickDevice(devIdx)
    }
    property bool favFull: false    // a fourth favourite was refused
    function toggleFavorite(i) {
        favFull = !player.toggleFavoriteMode(i)
        if (favFull) return
        // the list order changes: keep the chosen mode chosen
        const chosen = modes[modeIdx].label
        modes = player.captureModes(devices[devIdx].path)
        for (var k = 0; k < modes.length; ++k) if (modes[k].label === chosen) modeIdx = k
    }
    function pickDevice(i) {
        favFull = false
        devIdx = i
        var share = devices.length > 0 && devices[i].path === "screencast"
        modes = devices.length > 0 && !share ? player.captureModes(devices[i].path) : []
        modeIdx = 0
        if (share) {
            // the game plays its own sound: no audio unless one was chosen for a window/screen before
            srcIdx = 0
            if (player.lastCaptureDevice() === "screencast")
                for (var a = 0; a < sources.length; ++a) if (sources[a].name !== "" && sources[a].name === player.lastCaptureAudio()) srcIdx = a
            return
        }
        // audio: remembered source, else best name match for this card, else none
        var want = player.lastCaptureAudio()
        var guess = devices.length > 0 ? player.guessAudioSource(devices[i].name) : ""
        srcIdx = 0
        for (var k = 0; k < sources.length; ++k) if (sources[k].name !== "" && (sources[k].name === guess)) srcIdx = k
        if (srcIdx === 0) for (var j = 0; j < sources.length; ++j) if (sources[j].name !== "" && sources[j].name === want) srcIdx = j
    }

    Rectangle { anchors.fill: parent; color: "#cc000000"; MouseArea { anchors.fill: parent; onClicked: dlg.closeDialog() } }

    component Row_: Rectangle {
        id: row
        property string text
        property bool sel: false
        property int rightPad: 12
        signal clicked()
        width: parent ? parent.width : 100; height: 34; radius: 8 * Theme.round
        color: sel ? Theme.accentSoft : (ma.containsMouse ? Theme.border : Theme.surface)
        border.color: sel ? Theme.accent : "transparent"
        Text { font.family: Theme.font; anchors { verticalCenter: parent.verticalCenter; left: parent.left; right: parent.right; leftMargin: 12; rightMargin: row.rightPad }
            text: row.text; color: row.sel ? Theme.heading : Theme.text; font.pixelSize: 13; elide: Text.ElideRight }
        MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: row.clicked() }
    }
    component Section: Text { font.family: Theme.font; color: Theme.label; font.pixelSize: 12; font.weight: Font.Medium }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(620, parent.width - 40); height: Math.min(640, parent.height - 40)
        radius: 16 * Theme.round; color: Theme.surfaceDeep; border.color: Theme.border
        MouseArea { anchors.fill: parent }   // swallow clicks

        Column {
            anchors { fill: parent; margins: 22 }
            spacing: 10
            visible: !dlg.testView
            Text { font.family: Theme.headFont; width: Math.min(implicitWidth, parent.width - 62); elide: Text.ElideRight; font.capitalization: Theme.headCaps ? Font.AllUppercase : Font.MixedCase; text: tr("captureTitle"); color: Theme.heading; font.pixelSize: 20 * Theme.headScale; font.weight: Font.DemiBold }

            Section { text: tr("device") }
            Column {
                width: parent.width; spacing: 4
                Repeater { model: dlg.devices
                    Row_ { text: modelData.path === "screencast" ? modelData.name : modelData.name + "  (" + modelData.path + ")"
                        sel: index === dlg.devIdx; onClicked: dlg.pickDevice(index) } }
                Text { font.family: Theme.font; visible: dlg.cardCount <= 0; text: tr("noDevices") + (dlg.help !== "" ? "\n" + dlg.help : ""); color: Theme.warn; font.pixelSize: 13; width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15 }
            }

            Text { font.family: Theme.font; visible: dlg.isShare; text: tr("screenShareHint"); color: Theme.muted; font.pixelSize: 12; width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15 }
            Section { text: tr("mode"); visible: dlg.modes.length > 0 }
            Flickable {
                visible: dlg.modes.length > 0
                width: parent.width; height: Math.min(170, contentHeight); contentHeight: modeCol.height; clip: true
                Column { id: modeCol; width: parent.width; spacing: 4
                    Repeater { model: dlg.modes
                        Row_ {
                            id: modeRow
                            rightPad: 40
                            text: modelData.label + (modelData.recommended ? "   · " + tr("recommended") : "")
                            sel: index === dlg.modeIdx; onClicked: dlg.modeIdx = index
                            // ★ marks a favourite signal (up to three, shown first, the last used on top)
                            Text {
                                anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                                z: 2
                                text: modelData.favorite ? "★" : "☆"
                                color: modelData.favorite ? Theme.accent : (favMa.containsMouse ? Theme.text : Theme.muted)
                                font.pixelSize: 18
                                MouseArea {
                                    id: favMa
                                    anchors { fill: parent; margins: -8 }
                                    hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: dlg.toggleFavorite(index)
                                }
                            }
                        } } }
            }
            Text { font.family: Theme.font; visible: dlg.modes.length > 0; width: parent.width; wrapMode: Text.Wrap
                text: dlg.favFull ? tr("favoritesFull").arg(player.maxFavoriteModes()) : tr("favoritesHint").arg(player.maxFavoriteModes())
                color: dlg.favFull ? Theme.warn : Theme.muted; font.pixelSize: 11; lineHeight: 1.15 }
            // measures the card's modes on this PC (USB, graphics card, CPU) instead of guessing from the list
            Text { font.family: Theme.font; visible: dlg.modes.length > 1 && !dlg.isShare; text: tr("sigButton"); color: Theme.accent
                font.pixelSize: 12; font.weight: Font.DemiBold
                MouseArea { anchors { fill: parent; margins: -4 } cursorShape: Qt.PointingHandCursor; onClicked: dlg.testView = true } }

            // a compressed mode where the card has an uncompressed one (after PushFPS 4.1): said, never switched for you
            Rectangle {
                readonly property string advice: dlg.modes.length > 0 ? player.modeAdvice(dlg.modeIdx) : ""
                readonly property int better: dlg.modes.length > 0 ? player.betterModeIndex(dlg.modeIdx) : -1
                visible: advice !== ""
                width: parent.width; height: visible ? adviceCol.height + 16 : 0; radius: 8 * Theme.round
                color: Theme.surface; border.color: Theme.border
                Column {
                    id: adviceCol
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                    spacing: 6
                    Text { font.family: Theme.font; width: parent.width; wrapMode: Text.Wrap; text: parent.parent.advice; color: Theme.text; font.pixelSize: 12; lineHeight: 1.15 }
                    Text { font.family: Theme.font; visible: parent.parent.better >= 0; text: tr("useUncompressed"); color: Theme.accent; font.pixelSize: 12; font.weight: Font.DemiBold
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: dlg.modeIdx = parent.parent.parent.better } }
                }
            }

            Section { text: tr("audio") }
            Flickable {
                width: parent.width; height: Math.min(130, contentHeight); contentHeight: srcCol.height; clip: true
                Column { id: srcCol; width: parent.width; spacing: 4
                    Repeater { model: dlg.sources
                        Row_ { text: modelData.desc; sel: index === dlg.srcIdx; onClicked: dlg.srcIdx = index } } }
            }
        }

        Rectangle {
            visible: !dlg.testView
            anchors { left: parent.left; bottom: parent.bottom; margins: 22 }
            width: psT.implicitWidth + 28; height: 38; radius: 10 * Theme.round; color: m0.containsMouse ? Theme.surfaceHover : Theme.surface
            Text { id: psT; font.family: Theme.font; anchors.centerIn: parent; text: "PlayStation Remote Play"; color: Theme.text; font.pixelSize: 13 }
            MouseArea { id: m0; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { dlg.visible = false; dlg.closed(); dlg.remotePlay() } } }
        Row {
            visible: !dlg.testView
            anchors { right: parent.right; bottom: parent.bottom; margins: 22 }
            spacing: 8
            Rectangle { width: 96; height: 38; radius: 10 * Theme.round; color: m1.containsMouse ? Theme.surfaceHover : Theme.surface
                Text { font.family: Theme.font; anchors.centerIn: parent; text: tr("refresh"); color: Theme.text; font.pixelSize: 13 }
                MouseArea { id: m1; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: dlg.refresh() } }
            Rectangle { width: 96; height: 38; radius: 10 * Theme.round; color: m2.containsMouse ? Theme.surfaceHover : Theme.surface
                Text { font.family: Theme.font; anchors.centerIn: parent; text: tr("cancel"); color: Theme.text; font.pixelSize: 13 }
                MouseArea { id: m2; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: dlg.closeDialog() } }
            Rectangle { width: 120; height: 38; radius: 10 * Theme.round; opacity: dlg.devices.length > 0 && (dlg.modes.length > 0 || dlg.isShare) ? 1 : 0.4
                color: m3.containsMouse ? Theme.accentHover : Theme.accent
                Text { font.family: Theme.font; anchors.centerIn: parent; text: tr("start"); color: Theme.accentInk; font.pixelSize: 14; font.weight: Font.DemiBold }
                MouseArea { id: m3; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    enabled: dlg.devices.length > 0 && (dlg.modes.length > 0 || dlg.isShare)
                    onClicked: {
                        if (dlg.isShare) { player.openScreenCast(dlg.sources[dlg.srcIdx].name, false); dlg.visible = false; dlg.closed(); return }
                        if (player.openCapture(dlg.devices[dlg.devIdx].path, dlg.modeIdx, dlg.sources[dlg.srcIdx].name)) { dlg.visible = false; dlg.closed() }
                    } } }
        }
        // ---- Find the best signal
        Item {
            id: sigView
            visible: dlg.testView
            anchors { fill: parent; margins: 22 }
            readonly property var results: player.signalTestResults
            Column {
                id: sigHead
                width: parent.width; spacing: 8
                Text { font.family: Theme.headFont; width: parent.width; elide: Text.ElideRight; font.capitalization: Theme.headCaps ? Font.AllUppercase : Font.MixedCase
                    text: tr("sigTitle"); color: Theme.heading; font.pixelSize: 20 * Theme.headScale; font.weight: Font.DemiBold }
                Text { font.family: Theme.font; width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 12; color: Theme.muted
                    visible: sigView.results.length === 0 || player.signalTestRunning; text: tr("sigIntro") }
                Rectangle { visible: player.signalTestRunning; width: parent.width; height: 6; radius: 3; color: Theme.surface
                    Rectangle { width: parent.width * player.signalTestProgress; height: parent.height; radius: 3; color: Theme.accent
                        Behavior on width { NumberAnimation { duration: 300 } } } }
                Text { font.family: Theme.font; width: parent.width; wrapMode: Text.Wrap; visible: text !== ""; text: player.signalTestText
                    color: Theme.text; font.pixelSize: 13; font.weight: Font.Medium; lineHeight: 1.15 }
            }
            Flickable {
                anchors { left: parent.left; right: parent.right; top: sigHead.bottom; topMargin: 10; bottom: sigNote.top; bottomMargin: 8 }
                contentHeight: resCol.height; clip: true
                Column { id: resCol; width: parent.width; spacing: 6
                    Repeater { model: sigView.results
                        Rectangle {
                            width: resCol.width; height: rc.height + 16; radius: 8 * Theme.round
                            color: modelData.recommended ? Theme.accentSoft : Theme.surface
                            border.color: modelData.recommended ? Theme.accent : "transparent"
                            opacity: modelData.keepsUp || !modelData.measured ? 1 : 0.75
                            Column { id: rc
                                anchors { left: parent.left; right: useBtn.left; top: parent.top; margins: 8; rightMargin: 8 }
                                spacing: 3
                                Text { font.family: Theme.font; width: parent.width; elide: Text.ElideRight; text: modelData.label; color: Theme.heading; font.pixelSize: 13; font.weight: Font.DemiBold }
                                Flow { width: parent.width; spacing: 4; visible: modelData.tags.length > 0
                                    Repeater { model: modelData.tags
                                        Rectangle { height: 18; width: tagT.implicitWidth + 12; radius: 9; color: Theme.surfaceDeep; border.color: Theme.accent
                                            Text { id: tagT; anchors.centerIn: parent; font.family: Theme.font; text: modelData; color: Theme.accent; font.pixelSize: 10; font.weight: Font.DemiBold } } } }
                                Text { font.family: Theme.font; width: parent.width; wrapMode: Text.Wrap; visible: text !== ""; text: modelData.stats; color: Theme.text; font.pixelSize: 11; lineHeight: 1.1 }
                                Text { font.family: Theme.font; width: parent.width; wrapMode: Text.Wrap; visible: text !== ""; text: modelData.flag; color: modelData.keepsUp ? Theme.muted : Theme.warn; font.pixelSize: 11; lineHeight: 1.1 }
                            }
                            Text { id: favS
                                anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                                text: modelData.favorite ? "★" : "☆"; font.pixelSize: 18
                                color: modelData.favorite ? Theme.accent : (fsMa.containsMouse ? Theme.text : Theme.muted)
                                MouseArea { id: fsMa; anchors { fill: parent; margins: -8 } hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: { dlg.favFull = !player.toggleFavoriteSignal(index) } } }
                            Rectangle { id: useBtn
                                visible: modelData.measured && !player.signalTestRunning
                                anchors { right: favS.left; rightMargin: 12; verticalCenter: parent.verticalCenter }
                                width: visible ? useT.implicitWidth + 22 : 0; height: 30; radius: 8 * Theme.round
                                color: modelData.recommended ? (uMa.containsMouse ? Theme.accentHover : Theme.accent) : (uMa.containsMouse ? Theme.surfaceHover : Theme.surfaceDeep)
                                Text { id: useT; anchors.centerIn: parent; font.family: Theme.font; text: tr("sigUse"); font.pixelSize: 12; font.weight: Font.DemiBold
                                    color: modelData.recommended ? Theme.accentInk : Theme.text }
                                MouseArea { id: uMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: if (player.useSignalResult(index)) { dlg.testView = false; dlg.visible = false; dlg.closed() } } }
                        } } }
            }
            Text { id: sigNote; font.family: Theme.font; anchors { left: parent.left; right: parent.right; bottom: sigBtns.top; bottomMargin: 10 }
                wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 11; color: Theme.muted
                text: (dlg.favFull ? tr("favoritesFull").arg(player.maxFavoriteModes()) + "\n" : "") + tr("sigNote") }
            Row {
                id: sigBtns
                anchors { right: parent.right; bottom: parent.bottom }
                spacing: 8
                Rectangle { width: Math.max(96, bT.implicitWidth + 28); height: 38; radius: 10 * Theme.round; color: bMa.containsMouse ? Theme.surfaceHover : Theme.surface
                    Text { id: bT; font.family: Theme.font; anchors.centerIn: parent; text: player.signalTestRunning ? tr("sigStop") : tr("sigBack"); color: Theme.text; font.pixelSize: 13 }
                    MouseArea { id: bMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { if (player.signalTestRunning) player.stopSignalTest(); else { dlg.testView = false; dlg.favFull = false; dlg.modes = player.captureModes(dlg.devices[dlg.devIdx].path) } } } }
                Rectangle { visible: !player.signalTestRunning; width: Math.max(120, sT.implicitWidth + 28); height: 38; radius: 10 * Theme.round
                    color: sMa.containsMouse ? Theme.accentHover : Theme.accent
                    Text { id: sT; font.family: Theme.font; anchors.centerIn: parent; text: sigView.results.length > 0 ? tr("sigAgain") : tr("sigStart"); color: Theme.accentInk; font.pixelSize: 14; font.weight: Font.DemiBold }
                    MouseArea { id: sMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { dlg.favFull = false; player.startSignalTest(dlg.devices[dlg.devIdx].path, dlg.sources[dlg.srcIdx].name) } } }
            }
        }
    }
}
