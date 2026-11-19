import QtQuick
import QtQuick.Window
import VeyAmux 1.0
import "Strings.js" as S

Window {
    id: win
    visible: false          // shown from C++ once the graphics device is configured
    width: 1280
    height: 720
    minimumWidth: 720
    minimumHeight: 420
    color: Theme.bg
    title: player.hasMedia ? player.title + " — VeyAmux" : "VeyAmux"

    // the theme (Theme.qml) follows the saved choice
    Binding { target: Theme; property: "name"; value: player.theme }

    readonly property bool fs: visibility === Window.FullScreen
    // small screens (Steam Deck and other handhelds, under ~10" wide) and small windows: the menu fills the whole window
    readonly property real screenMm: Screen.pixelDensity > 0 ? Screen.width * Screen.devicePixelRatio / Screen.pixelDensity : 1000
    readonly property bool compactMenu: handheld || Qt.application.arguments.indexOf("--compact-menu") >= 0 || screenMm < 260 || width < 1000
    property bool panelOpen: startPanel
    onPanelOpenChanged: if (panelOpen) player.demoRelease()   // the demo stops swimming while the menu is open
    property bool controlsShown: true
    property bool scrubbing: false
    property real scrubValue: 0

    // texts: Strings.js; player.language is the chosen language (follows the system unless one is picked)
    function t(k) { var s = S.strings[player.language] || S.strings.en; if (s[k] !== undefined) return s[k]; return S.strings.en[k] !== undefined ? S.strings.en[k] : k }
    function fmt(sec) {
        sec = Math.max(0, Math.floor(sec))
        var h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60), s = sec % 60
        return (h > 0 ? h + ":" + (m < 10 ? "0" : "") : "") + m + ":" + (s < 10 ? "0" : "") + s
    }
    function wake() { controlsShown = true; hideTimer.restart() }
    readonly property var scales: [1.0, 1.5, 2.0, 3.0]

    Timer { id: hideTimer; interval: 2600; onTriggered: if (player.playing && !panelOpen && !scrubbing && !keepControls) controlsShown = false }

    // ------------------------------------------------------------- video surface
    // HDR output: the whole window is drawn into an FP16 layer and converted to scRGB light for the HDR swapchain
    Item {
    id: hdrRoot
    anchors.fill: parent
    layer.enabled: player.hdrOutputActive
    layer.format: ShaderEffectSource.RGBA16F
    layer.effect: ShaderEffect {
        property real white: 203.0
        property real mode: player.hdrModeActive
        fragmentShader: "qrc:/shaders/hdr_window.frag.qsb"
    }

    VideoItem {
        id: video; anchors.fill: parent; bridge: player
        // HDR output: the picture is PQ-coded; this layer turns it into extended sRGB for the window layer below
        layer.enabled: player.hdrOutputActive
        layer.effect: ShaderEffect { fragmentShader: "qrc:/shaders/pq_video.frag.qsb" }
    }


    // Empty state
    Column {
        id: startCol
        visible: !player.hasMedia
        anchors.centerIn: parent
        spacing: 18
        Image { anchors.horizontalCenter: parent.horizontalCenter; source: Theme.logo; sourceSize: Qt.size(112, 112); width: 112; height: 112; smooth: true }
        Text { font.family: Theme.headFont; font.capitalization: Theme.headCaps ? Font.AllUppercase : Font.MixedCase; anchors.horizontalCenter: parent.horizontalCenter; text: "VeyAmux"; color: Theme.heading; font.pixelSize: 54 * Theme.headScale; font.weight: Font.DemiBold; font.letterSpacing: 2 }
        Text { font.family: Theme.font; anchors.horizontalCenter: parent.horizontalCenter; text: t("drop"); color: Theme.label; font.pixelSize: 16 }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 12
            Rectangle {
                width: 170; height: 46; radius: 23 * Theme.round; gradient: openMa.containsMouse ? Theme.sunsetHover : Theme.sunset
                border.width: Theme.edge; border.color: "#000000"
                Rectangle { visible: Theme.edge > 0; x: 2; y: parent.height - 6; width: parent.width - 4; height: 4; color: "#59000000" }
                Text { font.family: Theme.headCaps ? Theme.headFont : Theme.font; anchors.centerIn: parent; text: t("open"); color: Theme.accentInk
                    font.pixelSize: Theme.headCaps ? 12 : 15; font.weight: Font.DemiBold; font.capitalization: Theme.headCaps ? Font.AllUppercase : Font.MixedCase }
                MouseArea { id: openMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: player.openDialog() }
            }
            Rectangle {
                width: 160; height: 44; radius: 22 * Theme.round; color: capMa.containsMouse ? Theme.surfaceHover : Theme.surface; border.color: Theme.border
                border.width: Math.max(1, Theme.edge)
                Rectangle { visible: Theme.edge > 0; x: 2; y: parent.height - 6; width: parent.width - 4; height: 4; color: "#59000000" }
                Text { font.family: Theme.headCaps ? Theme.headFont : Theme.font; anchors.centerIn: parent; text: t("capture"); color: Theme.text
                    font.pixelSize: Theme.headCaps ? 12 : 15; font.weight: Font.DemiBold; font.capitalization: Theme.headCaps ? Font.AllUppercase : Font.MixedCase }
                MouseArea { id: capMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: capDlg.openDialog() }
            }
            Rectangle {
                width: 160; height: 44; radius: 22 * Theme.round; color: psMa.containsMouse ? Theme.surfaceHover : Theme.surface; border.color: Theme.border
                border.width: Math.max(1, Theme.edge)
                Rectangle { visible: Theme.edge > 0; x: 2; y: parent.height - 6; width: parent.width - 4; height: 4; color: "#59000000" }
                Text { font.family: Theme.headCaps ? Theme.headFont : Theme.font; anchors.centerIn: parent; text: "PlayStation"; color: Theme.text
                    font.pixelSize: Theme.headCaps ? 12 : 15; font.weight: Font.DemiBold; font.capitalization: Theme.headCaps ? Font.AllUppercase : Font.MixedCase }
                MouseArea { id: psMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: psDlg.openDialog() }
            }
        }
        Text { font.family: Theme.font; anchors.horizontalCenter: parent.horizontalCenter; text: t("hint"); color: Theme.muted; font.pixelSize: 14 }
        // demo scene: try every feature without a capture card
        Column {
            visible: player.demoAvailable
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 8
            Item { width: 1; height: 4 }
            Text { font.family: Theme.font; anchors.horizontalCenter: parent.horizontalCenter; text: t("demoTitle"); color: Theme.label; font.pixelSize: 15; font.weight: Font.DemiBold }
            Text { font.family: Theme.font; anchors.horizontalCenter: parent.horizontalCenter; width: 540; horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap; text: t("demoHint"); color: Theme.muted; font.pixelSize: 13 }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 10
                Repeater {
                    model: [30, 60]
                    delegate: Rectangle {
                        required property int modelData
                        width: 132; height: 38; radius: 19 * Theme.round
                        color: dmMa.containsMouse ? Theme.surfaceHover : Theme.surface; border.color: Theme.accentLine
                        border.width: Math.max(1, Theme.edge)
                        Rectangle { visible: Theme.edge > 0; x: 2; y: parent.height - 6; width: parent.width - 4; height: 4; color: "#59000000" }
                        Text { font.family: Theme.headCaps ? Theme.headFont : Theme.font; anchors.centerIn: parent; text: "Demo · " + modelData + " fps"; color: Theme.text
                            font.pixelSize: Theme.headCaps ? 10 : 14; font.weight: Font.DemiBold; font.capitalization: Theme.headCaps ? Font.AllUppercase : Font.MixedCase }
                        MouseArea { id: dmMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: player.openDemo(modelData) }
                    }
                }
            }
        }
        // theme picker: each chip shows the theme's own colours and logo
        Item { width: 1; height: 10 }
        Text { font.family: Theme.font; anchors.horizontalCenter: parent.horizontalCenter; text: t("theme"); color: Theme.label; font.pixelSize: 13 }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10
            Repeater {
                model: Theme.names
                delegate: Rectangle {
                    required property string modelData
                    readonly property var pal: Theme.palettes[modelData]
                    readonly property bool sel: Theme.name === modelData
                    width: 128; height: 44; radius: 12 * Theme.round
                    color: pal.surface
                    border.width: sel ? 2 : 1
                    border.color: sel ? pal.accent : (thMa.containsMouse ? pal.accentLine : Theme.border)
                    Row {
                        anchors.centerIn: parent
                        spacing: 8
                        Image { source: "icons/logo-" + modelData + ".svg"; sourceSize: Qt.size(26, 26); width: 26; height: 26; anchors.verticalCenter: parent.verticalCenter }
                        Text { font.family: Theme.font; text: pal.title; color: pal.text; font.pixelSize: 14; font.weight: sel ? Font.DemiBold : Font.Medium; anchors.verticalCenter: parent.verticalCenter }
                    }
                    Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 6 } height: 3; radius: 1.5 * Theme.round
                        gradient: Gradient { orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: pal.g0 } GradientStop { position: 1; color: pal.g1 } } }
                    MouseArea { id: thMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: player.theme = modelData }
                }
            }
        }
        // language picker: Auto follows the system
        Item { width: 1; height: 4 }
        Text { font.family: Theme.font; anchors.horizontalCenter: parent.horizontalCenter; text: t("language"); color: Theme.label; font.pixelSize: 13 }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 6
            Repeater {
                model: ["auto"].concat(S.languages)
                delegate: Rectangle {
                    required property string modelData
                    readonly property bool sel: player.languageChoice === modelData
                    width: lgText.implicitWidth + 22; height: 30; radius: 15 * Theme.round
                    color: sel ? Theme.accent : (lgMa.containsMouse ? Theme.surfaceHover : Theme.surface)
                    gradient: sel ? Theme.sunset : null
                    border.width: Theme.edge; border.color: "#000000"
                    Text { id: lgText; font.family: Theme.font; anchors.centerIn: parent; font.pixelSize: 13
                        text: modelData === "auto" ? t("langAuto") : S.names[modelData]
                        color: parent.sel ? Theme.accentInk : Theme.text; font.weight: parent.sel ? Font.DemiBold : Font.Normal }
                    MouseArea { id: lgMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: player.languageChoice = modelData }
                }
            }
        }
    }

    // Click / hover / double-click on the video; in the demo scene, dragging looks around
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        enabled: player.hasMedia
        property real lx: 0
        property real ly: 0
        cursorShape: player.demoActive && pressed ? Qt.ClosedHandCursor : (win.controlsShown || !win.fs) ? Qt.ArrowCursor : Qt.BlankCursor
        onPressed: (m) => { lx = m.x; ly = m.y }
        onPositionChanged: (m) => {
            wake()
            if (pressed && player.demoActive) { player.demoLook(m.x - lx, m.y - ly); lx = m.x; ly = m.y }
        }
        // a tap on a touchscreen (Steam Deck) only shows the controls with the menu button; a mouse click pauses
        onClicked: (m) => { if (!player.demoActive && m.source === Qt.MouseEventNotSynthesized) player.togglePlay(); wake() }
        onDoubleClicked: (m) => { if (!player.demoActive && m.source === Qt.MouseEventNotSynthesized) player.togglePlay(); win.visibility = fs ? Window.Windowed : Window.FullScreen }
    }

    // demo scene: how to swim (while the controls are shown)
    Rectangle {
        visible: player.demoActive && player.demoProgress >= 1 && win.controlsShown && !win.panelOpen
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 96 }
        width: Math.min(parent.width - 40, demoKeysText.implicitWidth + 32); height: demoKeysText.implicitHeight + 16
        radius: 10 * Theme.round; color: Theme.overlay
        Text { id: demoKeysText; anchors.centerIn: parent; width: parent.width - 32; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
            font.family: Theme.font; font.pixelSize: 13; color: Theme.text; text: t("demoKeys") }
    }

    DropArea {
        anchors.fill: parent
        onDropped: (drop) => { if (drop.hasUrls) player.openUrl(drop.urls[0]) }
    }

    // ------------------------------------------------------------- status toast
    Rectangle {
        visible: player.status !== ""
        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: 18 }
        width: Math.min(parent.width - 40, toastText.implicitWidth + 32); height: 36; radius: 18 * Theme.round; color: player.hdrOutputActive ? Qt.rgba(Theme.toast.r, Theme.toast.g, Theme.toast.b, 1) : Theme.toast
        border.color: Theme.accentLine
        Text { font.family: Theme.font; id: toastText; anchors.centerIn: parent; text: player.status; color: Theme.warn; font.pixelSize: 13; elide: Text.ElideRight; width: parent.width - 28; horizontalAlignment: Text.AlignHCenter }
    }

    // short confirmations (profile loaded, clip or screenshot saved)
    Rectangle {
        visible: player.notice !== "" && player.status === ""
        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: 18 }
        width: Math.min(parent.width - 40, noticeText.implicitWidth + 32); height: 36; radius: 18 * Theme.round; color: player.hdrOutputActive ? Qt.rgba(Theme.toast.r, Theme.toast.g, Theme.toast.b, 1) : Theme.toast
        border.color: Theme.okLine
        Text { font.family: Theme.font; id: noticeText; anchors.centerIn: parent; text: player.notice; color: Theme.ok; font.pixelSize: 13; elide: Text.ElideRight; width: parent.width - 28; horizontalAlignment: Text.AlignHCenter }
    }

    // frame-time overlay (F3)
    Rectangle {
        id: ftOverlay
        visible: player.overlay
        z: 6
        anchors { left: parent.left; bottom: parent.bottom; leftMargin: 16; bottomMargin: 90 }
        width: 440; height: ftCol.implicitHeight + 16; radius: 10 * Theme.round
        color: player.hdrOutputActive ? Theme.bg : Theme.overlay; border.color: Theme.border
        Column {
            id: ftCol
            x: 8; y: 8; width: parent.width - 16; spacing: 6
            Text { width: parent.width; wrapMode: Text.Wrap; text: player.overlayStats; color: Theme.text; font.pixelSize: 12; font.family: "monospace" }
            Canvas {
                id: ftCanvas
                width: parent.width; height: 70
                onPaint: {
                    const ctx = getContext("2d");
                    ctx.reset();
                    const g = player.overlayGraph, n = g.length;
                    const target = player.overlayTarget;
                    const top = Math.max(target * 2.5, 20);
                    ctx.fillStyle = "rgba(255,255,255,0.05)";
                    ctx.fillRect(0, 0, width, height);
                    if (target > 0) {   // average frame time
                        const y = height - target / top * height;
                        ctx.strokeStyle = "rgba(63,224,208,0.6)"; ctx.lineWidth = 1;
                        ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke();
                    }
                    if (n < 2) return;
                    ctx.strokeStyle = Theme.accent; ctx.lineWidth = 1.5;
                    ctx.beginPath();
                    for (let i = 0; i < n; ++i) {
                        const x = i * width / (n - 1);
                        const y = height - Math.min(g[i], top) / top * height;
                        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
                    }
                    ctx.stroke();
                }
                Connections { target: player; function onOverlayTick() { ftCanvas.requestPaint() } }
            }
            Text { font.family: Theme.font; text: t("overlayHint"); color: Theme.dim; font.pixelSize: 11 }
        }
    }

    // recording indicator (stays small and out of the way; the picture itself is recorded without it)
    Row {
        visible: player.recording || player.streaming
        anchors { top: parent.top; right: parent.right; topMargin: 14; rightMargin: 16 }
        spacing: 6; z: 5
        Rectangle { width: 10; height: 10; radius: 5 * Theme.round; color: "#ff4040"; anchors.verticalCenter: parent.verticalCenter
            SequentialAnimation on opacity { running: player.recording || player.streaming; loops: Animation.Infinite
                NumberAnimation { to: 0.3; duration: 700 } NumberAnimation { to: 1; duration: 700 } } }
        Text { font.family: Theme.font; text: player.streaming ? (player.recording ? "LIVE · REC" : "LIVE") : "REC"; color: "#ffdddd"; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 1 }
    }

    // ------------------------------------------------------------- title (top)
    Rectangle {
        id: topBar
        visible: player.hasMedia
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 70
        opacity: controlsShown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        gradient: Gradient { GradientStop { position: 0; color: "#b0000000" } GradientStop { position: 1; color: "#00000000" } }
        Text { font.family: Theme.font; x: 22; y: 18; width: parent.width - 44; text: player.title; color: Theme.heading; font.pixelSize: 16; font.weight: Font.Medium; elide: Text.ElideRight }
        Text { font.family: Theme.font; x: 22; y: 40; text: player.mediaInfo; color: Theme.label; font.pixelSize: 12 }
    }

    // ------------------------------------------------------------- bottom controls
    Rectangle {
        id: bottomBar
        visible: player.hasMedia
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        height: 110
        opacity: controlsShown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        gradient: Gradient { GradientStop { position: 0; color: "#00000000" } GradientStop { position: 1; color: "#d0000000" } }

        // seek bar
        Item {
            id: seek
            visible: player.duration > 0
            anchors { left: parent.left; right: parent.right; bottom: controlsRow.top; leftMargin: 22; rightMargin: 22; bottomMargin: 4 }
            height: 22
            Text { font.family: Theme.font; id: tCur; anchors.verticalCenter: parent.verticalCenter; text: fmt(scrubbing ? scrubValue : player.position); color: Theme.text; font.pixelSize: 12; width: 52 }
            VSlider {
                anchors { left: tCur.right; right: tDur.left; verticalCenter: parent.verticalCenter; leftMargin: 6; rightMargin: 6 }
                from: 0; to: Math.max(player.duration, 0.001)
                value: scrubbing ? scrubValue : player.position
                onDraggingChanged: { if (dragging) { win.scrubbing = true } else { win.scrubbing = false; player.seek(win.scrubValue) } }
                onMoved: (v) => { win.scrubValue = v; wake() }
            }
            Text { font.family: Theme.font; id: tDur; anchors { right: parent.right; verticalCenter: parent.verticalCenter } text: fmt(player.duration); color: Theme.label; font.pixelSize: 12; width: 52; horizontalAlignment: Text.AlignRight }
        }

        Rectangle {
            visible: player.live
            anchors { left: parent.left; bottom: controlsRow.top; leftMargin: 22; bottomMargin: 10 }
            width: 58; height: 22; radius: 11 * Theme.round; color: Theme.live
            Text { font.family: Theme.font; anchors.centerIn: parent; text: t("live"); color: Theme.heading; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 1 }
        }
        Row {
            id: controlsRow
            anchors { left: parent.left; bottom: parent.bottom; leftMargin: 14; bottomMargin: 10 }
            spacing: 2
            IconButton { size: 40; source: "icons/folder.svg"; onClicked: player.openDialog() }
            IconButton { size: 40; source: "icons/capture.svg"; active: player.live; onClicked: capDlg.openDialog() }
            IconButton { size: 40; source: "icons/back10.svg"; onClicked: { player.seekRelative(-10); wake() } }
            IconButton { size: 46; source: player.playing ? "icons/pause.svg" : "icons/play.svg"; onClicked: { player.togglePlay(); wake() } }
            IconButton { size: 40; source: "icons/fwd10.svg"; onClicked: { player.seekRelative(10); wake() } }
            IconButton { size: 40; source: player.muted || player.volume < 0.01 ? "icons/mute.svg" : "icons/volume.svg"; onClicked: player.muted = !player.muted }
            VSlider {
                width: 90; anchors.verticalCenter: parent.verticalCenter
                from: 0; to: 1.0; value: player.muted ? 0 : player.volume
                onMoved: (v) => { player.muted = false; player.volume = v }
            }
            Item { width: 14; height: 1 }
            IconButton { size: 40; source: "icons/sparkles.svg"; active: win.panelOpen; onClicked: { win.panelOpen = !win.panelOpen; wake() } }
            IconButton { size: 40; source: win.fs ? "icons/exitfs.svg" : "icons/fullscreen.svg"; onClicked: win.visibility = win.fs ? Window.Windowed : Window.FullScreen }
        }
    }

    // ------------------------------------------------------------- controller (Gamepad.h)
    // Select + Start (or Guide held) opens / closes the menu; while it is open the controllers are kept from the game.
    // The D-pad / left stick moves between the menu's controls (the nearest one in that direction), A presses,
    // left/right change a slider, LB/RB jump between the columns, B closes the menu.
    // The controller also steers the start screen and the capture dialog (no game is running there).
    Binding { target: gamepad; property: "grabbed"; value: win.panelOpen || capDlg.visible || psDlg.visible || !player.hasMedia }
    Connections {
        target: gamepad
        function onMenuCombo() {
            if (capDlg.visible || psDlg.visible) return
            win.panelOpen = !win.panelOpen
            padNav.active = true; padNav.current = null
            if (win.panelOpen) padOpenTimer.restart(); else Qt.callLater(padNav.reset)
        }
        function onButton(name) { if (padNav.scope()) { padNav.active = true; padNav.press(name) } }
    }
    // a newly shown dialog starts on its first control
    Connections { target: capDlg; function onVisibleChanged() { if (padNav.active) { padNav.current = null; Qt.callLater(padNav.reset) } } }
    Connections { target: psDlg; function onVisibleChanged() { if (!psDlg.visible) keyRoot.forceActiveFocus(); if (padNav.active) { padNav.current = null; Qt.callLater(padNav.reset) } } }
    // the panel fades in (160 ms) and is invisible until then, so the first control is picked once it shows
    Timer { id: padOpenTimer; interval: 200; onTriggered: if (win.panelOpen && !padNav.current) padNav.reset() }
    QtObject {
        id: padNav
        property bool active: false        // shows the highlight; mouse use hides it again
        property Item current: null
        // every control in the menu: clickable areas (buttons, segments, switches, list rows) and sliders
        function collect(item, out) {
            if (!item || !item.visible || item.opacity <= 0.01) return
            if (item.from !== undefined && item.to !== undefined && item.neutral !== undefined && item.moved !== undefined) {
                out.push(item); return                       // a slider (VSlider): its own mouse area is not a target
            }
            if (item.acceptedButtons !== undefined && item.enabled && item !== panelEater && item.width > 4 && item.height > 4
                    && item.width < panel.width * 0.6) out.push(item)
            for (var i = 0; i < item.children.length; ++i) collect(item.children[i], out)
        }
        // what the controller steers: the capture dialog, else the menu, else the start screen
        function scope() { return psDlg.visible ? psDlg : capDlg.visible ? capDlg : win.panelOpen ? area : !player.hasMedia ? startCol : null }
        function targets() { var o = []; collect(scope(), o); return o }
        function centre(t) { var p = t.mapToItem(win.contentItem, t.width / 2, t.height / 2); return p }
        function isSlider(t) { return t && t.neutral !== undefined && t.moved !== undefined }
        function reset() {
            var ts = targets(); if (ts.length === 0) { current = null; return }
            var best = ts[0], bp = centre(best)
            for (var i = 1; i < ts.length; ++i) { var p = centre(ts[i]); if (p.x + p.y * 3 < bp.x + bp.y * 3) { best = ts[i]; bp = p } }
            current = best
        }
        function move(dx, dy) {
            var ts = targets(); if (!current || ts.indexOf(current) < 0) { reset(); return }
            var c = centre(current), best = null, bestScore = 1e9
            for (var i = 0; i < ts.length; ++i) {
                if (ts[i] === current) continue
                var p = centre(ts[i]), vx = p.x - c.x, vy = p.y - c.y
                var along = vx * dx + vy * dy, across = Math.abs(vx * dy - vy * dx)
                if (along <= 2) continue                     // not in that direction
                var score = along + across * 2.5
                if (score < bestScore) { bestScore = score; best = ts[i] }
            }
            if (best) current = best
        }
        function column(step) {
            if (scope() !== area) return
            var cols = [secA, secB, secC], c = current ? centre(current) : null, idx = 0
            for (var i = 0; i < cols.length; ++i) { var r = cols[i].mapToItem(win.contentItem, 0, 0); if (c && c.x >= r.x - 10) idx = i }
            var want = Math.max(0, Math.min(cols.length - 1, idx + step)), o = []
            collect(cols[want], o)
            if (o.length) { var best = o[0]; for (var j = 1; j < o.length; ++j) if (centre(o[j]).y < centre(best).y) best = o[j]; current = best }
        }
        function press(name) {
            if (!current || targets().indexOf(current) < 0) reset()
            if (name === "up") move(0, -1)
            else if (name === "down") move(0, 1)
            else if (name === "left") { if (isSlider(current)) current.moved(Math.max(current.from, current.value - (current.to - current.from) / 40)); else move(-1, 0) }
            else if (name === "right") { if (isSlider(current)) current.moved(Math.min(current.to, current.value + (current.to - current.from) / 40)); else move(1, 0) }
            else if (name === "lb") column(-1)
            else if (name === "rb") column(1)
            else if (name === "b") { if (psDlg.visible) psDlg.close(); else if (capDlg.visible) { capDlg.visible = false; capDlg.closed() } else win.panelOpen = false }
            else if (name === "a" && current && !isSlider(current)) current.clicked(null)
            Qt.callLater(function () { padRing.place() })
        }
    }
    // the highlight around the selected control
    Rectangle {
        id: padRing
        z: 1000
        visible: padNav.active && padNav.scope() !== null && padNav.current !== null && padNav.targets().indexOf(padNav.current) >= 0
        color: "transparent"; border.width: 3; border.color: Theme.accent; radius: 8 * Theme.round
        function place() {
            var t = padNav.current; if (!t) return
            var p = t.mapToItem(win.contentItem, 0, 0), q = t.mapToItem(win.contentItem, t.width, t.height)
            x = p.x - 4; y = p.y - 4; width = q.x - p.x + 8; height = q.y - p.y + 8
        }
        Connections { target: padNav; function onCurrentChanged() {
            // full-window menu: scroll the selected control into view
            if (win.compactMenu && padNav.current && panelOpen) {
                const p = padNav.current.mapToItem(menuFlick.contentItem, 0, 0)
                const h = padNav.current.height * area.fit
                if (p.y < menuFlick.contentY + 20) menuFlick.contentY = Math.max(0, p.y - 40)
                else if (p.y + h > menuFlick.contentY + menuFlick.height - 20)
                    menuFlick.contentY = Math.min(menuFlick.contentHeight - menuFlick.height, p.y + h - menuFlick.height + 40)
            }
            padRing.place()
        } }
    }

    // ------------------------------------------------------------- enhancement panel
    // Half the window wide, everything visible at once (no scrolling): the sections sit in three columns (two in narrow
    // windows), and if they still do not fit the height, the whole content is scaled down to fit.
    Rectangle {
        id: panel
        width: win.compactMenu ? parent.width : Math.max(560, parent.width / 2 - 18)
        // as tall as the (scaled) content needs, at most the window height (small screens: the whole window)
        height: win.compactMenu ? parent.height : Math.min(parent.height - 24, area.contentH * area.fit + 2 * area.anchors.margins)
        y: win.compactMenu ? 0 : 12
        x: win.compactMenu ? 0 : parent.width - width - 12   // always anchored; never animates on window resize (fullscreen)
        opacity: panelOpen ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 160 } }
        radius: win.compactMenu ? 0 : 18 * Theme.round
        // HDR output: fully opaque. Bright HDR video behind a 95 % panel shines through several times stronger than in
        // SDR (up to ~7x SDR white) and greys out the menu.
        color: player.hdrOutputActive || win.compactMenu ? Qt.rgba(Theme.panel.r, Theme.panel.g, Theme.panel.b, 1) : Theme.panel
        border.color: Theme.border
        // sunset line along the top edge
        Rectangle { x: 18; width: parent.width - 36; height: 2; radius: 1 * Theme.round; gradient: Theme.sunset; opacity: 0.85 }
        MouseArea { id: panelEater; anchors.fill: parent; hoverEnabled: true; onPositionChanged: { wake(); padNav.active = false } }   // keep controls awake & eat clicks
        // full-window menu: nothing is left to click beside it, so it gets its own close button (touch)
        Rectangle {
            visible: win.compactMenu; z: 10
            anchors { right: parent.right; top: parent.top; margins: 8 }
            width: 44; height: 44; radius: 22 * Theme.round; color: closeMa.containsMouse ? Theme.surfaceHover : Theme.surface
            Text { anchors.centerIn: parent; text: "✕"; color: Theme.text; font.pixelSize: 20 }
            MouseArea { id: closeMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: win.panelOpen = false }
        }

        component Param: Column {
            id: p
            property string label
            property real from; property real to; property real value; property real neutral: NaN
            property int decimals: 2
            signal moved(real v)
            width: parent.width; spacing: 4; opacity: player.enhance ? 1 : 0.45
            Row {
                width: parent.width
                Text { font.family: Theme.font; text: p.label; color: Theme.label; font.pixelSize: 16; width: parent.width - 50 }
                Text { font.family: Theme.font; text: p.value.toFixed(p.decimals); color: Theme.text; font.pixelSize: 16; width: 50; horizontalAlignment: Text.AlignRight }
            }
            VSlider { width: parent.width; from: p.from; to: p.to; value: p.value; neutral: p.neutral; onMoved: (v) => p.moved(v) }
        }

        Item {
            id: area
            anchors { fill: parent; margins: 26; topMargin: win.compactMenu ? 60 : 26 }
            // The menu is laid out at a fixed design width and then scaled as one piece to the panel (half the window),
            // so text, controls and spacing all grow and shrink together. Three columns, two in tall narrow windows.
            readonly property int cols: win.compactMenu ? 2 : width / Math.max(1, win.height - 76) >= 0.75 ? 3 : 2
            readonly property real gap: 36
            readonly property real designW: cols === 3 ? 1220 : 820
            readonly property real colW: (designW - (cols - 1) * gap) / cols
            readonly property real contentW: designW
            readonly property real contentH: cols === 3 ? Math.max(secA.height, secB.height, secC.height)
                                                        : Math.max(secA.height + gap + secB.height, secC.height)
            readonly property real fit: win.compactMenu ? width / designW
                                                        : Math.min(width / designW, (win.height - 24 - 2 * anchors.margins) / Math.max(1, contentH))
            Component.onCompleted: if (Qt.application.arguments.indexOf("--layout-debug") >= 0) layoutDbg.start()
            Timer { id: layoutDbg; interval: 3000; onTriggered: { console.log("LAYOUT cols", area.cols, "W", area.width, "H", win.height, "designW", area.designW, "contentH", area.contentH, "A", secA.height, "B", secB.height, "C", secC.height, "fitW", (area.width / area.designW).toFixed(3), "fit", area.fit.toFixed(3)); for (const c of [secA, secB, secC]) { let o = []; for (let i = 0; i < c.children.length; ++i) o.push(Math.round(c.children[i].height)); console.log("LAYOUT col", o.join(" ")) } } }

            // small screens: the menu is shown at a readable size and scrolls (drag with a finger / the touchpad;
            // the controller scrolls to what it selects)
            Flickable {
                id: menuFlick
                anchors.fill: parent
                interactive: win.compactMenu
                clip: win.compactMenu
                contentWidth: width; contentHeight: content.height * area.fit
                boundsBehavior: Flickable.StopAtBounds
                onContentYChanged: padRing.place()
            Item {
                id: content
                width: area.contentW; height: area.contentH
                transformOrigin: Item.Top
                scale: area.fit
                x: (area.width - width) / 2    // centred; scaling keeps the top centre in place

                Column {
                    id: secA
                    x: 0; y: 0; width: area.colW
                    spacing: 20
                    Row {
                        width: parent.width
                        Text { font.family: Theme.headFont; font.capitalization: Theme.headCaps ? Font.AllUppercase : Font.MixedCase; text: t("enhance"); color: Theme.heading; font.pixelSize: 24 * Theme.headScale; font.weight: Font.Bold; font.letterSpacing: 0.5; width: parent.width - 56; anchors.verticalCenter: parent.verticalCenter }
                        ToggleSwitch { checked: player.enhance; onToggled: (v) => player.enhance = v; anchors.verticalCenter: parent.verticalCenter }
                    }
                    Column {
                        width: parent.width; spacing: 10; opacity: player.enhance ? 1 : 0.45
                        Text { font.family: Theme.font; text: t("upscale"); color: Theme.label; font.pixelSize: 16 }
                        Segmented {
                            width: parent.width; enabled: player.enhance
                            model: [t("native"), "1.5×", "2×", "3×"]
                            current: { var i = scales.indexOf(Math.round(player.scale * 10) / 10); return i < 0 ? 0 : i }
                            onPicked: (i) => player.scale = scales[i]
                        }
                        Text { font.family: Theme.font; text: t("method"); color: Theme.label; font.pixelSize: 16; topPadding: 2 }
                        Segmented {
                            width: parent.width; enabled: player.enhance
                            // shown as SGSR | FSR 1 | FSR 3.1 | NIS; stored values 0 / 2 / 1 / 3
                            readonly property var values: [0, 2, 1, 3]
                            model: ["SGSR", "FSR 1", "FSR 3.1", "NIS"]
                            current: Math.max(0, values.indexOf(player.upscaler))
                            onPicked: (i) => player.upscaler = values[i]
                        }
                        Text { font.family: Theme.font;
                            width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 15
                            color: player.upscalerError !== "" ? Theme.warn : Theme.muted
                            text: player.upscalerError !== "" ? t("fsrFallback") + player.upscalerError
                                  : (player.upscaler === 1 ? (player.scale <= 1.001 ? t("fsrNativeAaHint") : t("fsr31Hint"))
                                     : player.upscaler === 2 ? t("fsr1Hint") + (player.scale <= 1.001 ? " " + t("fsrNeedsScale") : "")
                                     : player.upscaler === 3 ? t("nisHint") + (player.scale <= 1.001 ? " " + t("fsrNeedsScale") : "")
                                                             : t("sgsrHint"))
                        }
                    }
                    Column {
                        width: parent.width; spacing: 10; opacity: player.enhance ? 1 : 0.45
                        Text { font.family: Theme.font; text: t("denoise"); color: Theme.label; font.pixelSize: 16 }
                        Segmented {
                            width: parent.width; model: t("denoiseModes"); enabled: player.enhance
                            current: player.denoise; onPicked: (i) => player.denoise = i
                        }
                        Segmented {
                            width: parent.width; model: t("denoiseLevels"); enabled: player.enhance && player.denoise > 0
                            current: player.denoiseStrength; onPicked: (i) => player.denoiseStrength = i
                        }
                        Text { font.family: Theme.font;
                            width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 15; color: Theme.muted
                            text: t("denoiseHint" + player.denoise)
                        }
                        Text { font.family: Theme.font; width: parent.width; wrapMode: Text.Wrap; text: t("hdr"); color: Theme.label; font.pixelSize: 16; topPadding: 6 }
                        Segmented {
                            width: parent.width; model: ["600", "1000", "2000", "4000 nits"]
                            readonly property var peaks: [600, 1000, 2000, 4000]
                            current: Math.max(0, peaks.indexOf(player.hdrPeak))
                            onPicked: (i) => player.hdrPeak = peaks[i]
                        }
                        Text { font.family: Theme.font; width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 15; color: Theme.muted; text: t("hdrHint") }
                        Item {
                            width: parent.width; height: 22
                            Text { font.family: Theme.font; text: t("hdrOut"); color: Theme.label; font.pixelSize: 16; anchors.verticalCenter: parent.verticalCenter }
                            ToggleSwitch { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                checked: player.hdrOutput; onToggled: (v) => player.hdrOutput = v }
                        }
                        Segmented {
                            visible: player.hdrOutput
                            width: parent.width; model: ["HDR10", "scRGB"]
                            current: player.hdrMode; onPicked: (i) => player.hdrMode = i
                        }
                        Segmented {
                            visible: player.hdrOutput
                            width: parent.width
                            model: ["Auto", "400", "600", "1000", "1500 nits"]
                            readonly property var peaks: [0, 400, 600, 1000, 1500]
                            current: Math.max(0, peaks.indexOf(player.displayPeak))
                            onPicked: (i) => player.displayPeak = peaks[i]
                        }
                        Text { font.family: Theme.font;
                            visible: player.hdrOutput
                            width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 15
                            color: player.hdrOutputActive ? Theme.ok : Theme.warn
                            text: player.hdrOutputRestart ? t("hdrOutRestart") : player.hdrOutputActive ? t("hdrOutOn") + (player.displayPeak === 0 ? " " + (player.displayPeakEdid > 0 ? t("peakEdid").replace("%1", player.displayPeakEdid) : t("peakNoEdid")) : "")
                                  : player.hdrOutputNoVulkan ? t("hdrOutSw") : player.hdrOutputXwayland ? t("hdrOutX11") : t("hdrOutNo")
                        }
                        Item {
                            visible: player.gamescopeAvailable || player.inGamescope
                            width: parent.width; height: 26
                            Text { font.family: Theme.font; text: t("gamescope"); color: Theme.label; font.pixelSize: 16; anchors.verticalCenter: parent.verticalCenter }
                            ToggleSwitch { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                checked: player.gamescope; onToggled: (v) => player.gamescope = v }
                        }
                        Text { font.family: Theme.font;
                            visible: player.gamescopeAvailable || player.inGamescope
                            width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 15
                            color: player.inGamescope ? Theme.accent3 : Theme.muted
                            text: player.inGamescope ? t("gamescopeOn") : t("gamescopeHint")
                        }
                        Rectangle {
                            visible: player.hdrOutputRestart || player.gamescopeRestart
                            width: hdrRs.implicitWidth + 22; height: 26; radius: 8 * Theme.round; color: hdrRsMa.containsMouse ? Theme.accentHover : Theme.accent
                            Text { font.family: Theme.font; id: hdrRs; anchors.centerIn: parent; text: t("restartNow"); color: Theme.accentInk; font.pixelSize: 16; font.weight: Font.DemiBold }
                            MouseArea { id: hdrRsMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: player.restartApp() }
                        }
                    }
                    Column {
                        width: parent.width; spacing: 10; opacity: player.enhance ? 1 : 0.45
                        Row {
                            width: parent.width
                            Text { font.family: Theme.font; text: t("aa") + " (FXAA)"; color: Theme.label; font.pixelSize: 16; width: parent.width - 56; anchors.verticalCenter: parent.verticalCenter }
                            ToggleSwitch { checked: player.fxaa; enabled: player.enhance; onToggled: (v) => player.fxaa = v; anchors.verticalCenter: parent.verticalCenter }
                        }
                        Segmented {
                            width: parent.width; model: t("aaq"); enabled: player.enhance && player.fxaa
                            current: player.fxaaQuality; onPicked: (i) => player.fxaaQuality = i
                        }
                        Row {
                            width: parent.width
                            Text { font.family: Theme.font; text: t("deflicker"); color: Theme.label; font.pixelSize: 16; width: parent.width - 56; anchors.verticalCenter: parent.verticalCenter }
                            ToggleSwitch { checked: player.deflicker; enabled: player.enhance; onToggled: (v) => player.deflicker = v; anchors.verticalCenter: parent.verticalCenter }
                        }
                        Text { font.family: Theme.font; width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 15; color: Theme.muted; text: t("deflickerHint") }
                    }
                    // ---- sound output (the system default plus every output the sound server reports; refreshed when
                    // the menu opens) and a spectrum of the source's sound, so you can see whether sound comes in
                    Column {
                        id: sinkCol
                        width: parent.width; spacing: 4
                        property var sinks: []
                        function refresh() { sinks = player.audioSinks() }
                        Component.onCompleted: refresh()
                        Connections { target: win; function onPanelOpenChanged() { if (win.panelOpen) sinkCol.refresh() } }
                        Text { font.family: Theme.font; text: t("audioOut"); color: Theme.label; font.pixelSize: 16 }
                        Repeater {
                            model: [{ name: "", desc: t("audioOutDefault") }].concat(sinkCol.sinks.slice(0, 8))
                            delegate: Rectangle {
                                required property var modelData
                                readonly property bool sel: player.audioOutput === modelData.name
                                width: sinkCol.width; height: 28; radius: 8 * Theme.round
                                color: sinkMa.containsMouse ? Theme.surfaceHover : Theme.surface
                                gradient: sel ? Theme.sunset : null
                                Text { font.family: Theme.font; anchors { left: parent.left; right: parent.right; leftMargin: 10; rightMargin: 10; verticalCenter: parent.verticalCenter }
                                    text: modelData.desc; elide: Text.ElideRight; font.pixelSize: 15
                                    color: parent.sel ? Theme.accentInk : Theme.text }
                                MouseArea { id: sinkMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: player.audioOutput = modelData.name }
                            }
                        }
                        Item { width: 1; height: 6 }
                        Item {
                            width: parent.width; height: 18
                            Text { font.family: Theme.font; text: t("audioIn"); color: Theme.label; font.pixelSize: 16; anchors.verticalCenter: parent.verticalCenter }
                            Text { font.family: Theme.font;
                                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                font.pixelSize: 13; font.weight: Font.Medium
                                color: player.audioActive ? Theme.ok : Theme.muted
                                text: player.audioActive ? (player.muted ? t("audioInMuted") : t("audioInOn")) : t("audioInNone")
                            }
                        }
                        // spectrum: 24 bands from 50 Hz (left) to 16 kHz (right)
                        Rectangle {
                            id: spec
                            width: parent.width; height: 46; radius: 8 * Theme.round; color: Theme.surfaceDeep; border.color: Theme.border
                            Row {
                                id: specRow
                                anchors { fill: parent; margins: 6 }
                                spacing: 2
                                readonly property int n: 24
                                Repeater {
                                    model: specRow.n
                                    delegate: Item {
                                        required property int index
                                        width: (specRow.width - (specRow.n - 1) * specRow.spacing) / specRow.n
                                        height: specRow.height
                                        readonly property real level: player.audioBands.length > index ? player.audioBands[index] : 0
                                        Rectangle {
                                            anchors.bottom: parent.bottom
                                            width: parent.width; radius: 2 * Theme.round
                                            height: Math.max(2, parent.height * parent.level)
                                            gradient: Theme.sunset
                                            opacity: player.muted ? 0.45 : 1
                                        }
                                    }
                                }
                            }
                        }
                        Text { font.family: Theme.font; width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 13; color: Theme.muted; text: t("audioInHint") }
                    }
                    Rectangle { width: parent.width; height: 1; color: Theme.line }
                    // ---- Retro TV (CRT-Geom)
                    Column {
                        width: parent.width; spacing: 10
                        Item {
                            width: parent.width; height: 28
                            Text { font.family: Theme.headFont; width: Math.min(implicitWidth, parent.width - 62); elide: Text.ElideRight; font.capitalization: Theme.headCaps ? Font.AllUppercase : Font.MixedCase; text: "Retro TV"; color: Theme.heading; font.pixelSize: 21 * Theme.headScale; font.weight: Font.Bold; font.letterSpacing: 0.5; anchors.verticalCenter: parent.verticalCenter }
                            ToggleSwitch { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                checked: player.retroTv; onToggled: (v) => player.retroTv = v }
                        }
                        Text { font.family: Theme.font; width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 15; color: Theme.muted; text: t("retroHint") }
                        Text { font.family: Theme.font; visible: player.retroTv; text: t("retroLines"); color: Theme.label; font.pixelSize: 16 }
                        Segmented {
                            visible: player.retroTv
                            width: parent.width
                            readonly property var values: [240, 480, 0]
                            model: ["240", "480", t("native")]
                            current: Math.max(0, values.indexOf(player.retroLines))
                            onPicked: (i) => player.retroLines = values[i]
                        }
                        // ---- pixel-perfect scaling (shares the Retro TV section)
                        Item {
                            width: parent.width; height: 30
                            Text { font.family: Theme.font; text: t("pixelPerfect"); color: Theme.label; font.pixelSize: 17; width: parent.width - 62; elide: Text.ElideRight; anchors.verticalCenter: parent.verticalCenter }
                            ToggleSwitch { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                checked: player.pixelPerfect; onToggled: (v) => player.pixelPerfect = v }
                        }
                        Text { font.family: Theme.font; width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 15; color: Theme.muted; text: t("pixelHint") }
                        Text { font.family: Theme.font; visible: player.pixelPerfect; text: t("pixelGrid") + (player.pixelGrid === 1 && player.pixelGridSize !== "" ? "  ·  " + player.pixelGridSize : ""); color: Theme.label; font.pixelSize: 16 }
                        Segmented {
                            visible: player.pixelPerfect
                            width: parent.width
                            model: t("pixelGridModes")
                            current: player.pixelGrid
                            onPicked: (i) => player.pixelGrid = i
                        }
                        Text { font.family: Theme.font; visible: player.pixelPerfect; width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 14; color: Theme.muted; text: t("pixelGridHint") }
                    }
                }
                Column {
                    id: secB
                    x: area.cols === 3 ? area.colW + area.gap : 0
                    y: area.cols === 3 ? 0 : secA.height + area.gap
                    width: area.colW
                    spacing: 20
                    Param { label: t("sharpen"); from: 0; to: 1; value: player.sharpen; onMoved: (v) => player.sharpen = v }
                    Param { label: t("exposure"); from: -2; to: 2; value: player.exposure; neutral: 0; onMoved: (v) => player.exposure = v }
                    Param { label: t("contrast"); from: 0.6; to: 1.4; value: player.contrast; neutral: 1; onMoved: (v) => player.contrast = v }
                    Param { label: t("saturation"); from: 0; to: 2; value: player.saturation; neutral: 1; onMoved: (v) => player.saturation = v }
                    Param {
                        label: t("audioDelay"); from: -300; to: 300; decimals: 0; neutral: 0; opacity: 1
                        value: player.audioDelay; onMoved: (v) => player.audioDelay = Math.round(v / 10) * 10
                    }
                    Text { font.family: Theme.font; width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 15; color: Theme.muted; text: t("audioDelayHint") }
                    Item {
                        width: parent.width; height: 26
                        Text { font.family: Theme.font; text: t("normalize"); color: Theme.label; font.pixelSize: 16; anchors.verticalCenter: parent.verticalCenter }
                        ToggleSwitch { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                            checked: player.normalize; onToggled: (v) => player.normalize = v }
                    }
                    Rectangle {
                        width: parent.width; height: 34; radius: 10 * Theme.round; color: rstMa.containsMouse ? Theme.surfaceHover : Theme.surface
                        Text { font.family: Theme.font; anchors.centerIn: parent; text: t("reset"); color: Theme.text; font.pixelSize: 17 }
                        MouseArea { id: rstMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: player.resetEnhance() }
                    }

                    // ---- profiles
                    Column {
                        width: parent.width; spacing: 6
                        Text { font.family: Theme.font; text: t("profiles"); color: Theme.label; font.pixelSize: 16 }
                        Flow {
                            width: parent.width; spacing: 6
                            visible: player.profiles.length > 0
                            Repeater {
                                model: player.profiles
                                delegate: Rectangle {
                                    required property string modelData
                                    required property int index
                                    readonly property bool active: player.activeProfile === modelData
                                    readonly property string bound: player.profileSignal(modelData)
                                    height: 28; radius: 8 * Theme.round; width: chipRow.implicitWidth + 16
                                    color: active ? Theme.accent : (chipMa.containsMouse ? Theme.surfaceHover : Theme.surface)
                                    Row {
                                        id: chipRow; anchors.centerIn: parent; spacing: 6
                                        Text { font.family: Theme.font; text: (index < 9 ? (index + 1) + "  " : "") + modelData + (bound !== "" ? "  ⚡" : ""); color: active ? Theme.accentInk : Theme.text; font.pixelSize: 16; anchors.verticalCenter: parent.verticalCenter }
                                        Text { font.family: Theme.font;
                                            text: "×"; color: active ? Theme.accentInk : Theme.muted; font.pixelSize: 18; anchors.verticalCenter: parent.verticalCenter
                                            MouseArea { anchors.fill: parent; anchors.margins: -4; cursorShape: Qt.PointingHandCursor; onClicked: player.deleteProfile(modelData) }
                                        }
                                    }
                                    MouseArea { id: chipMa; anchors.fill: parent; anchors.rightMargin: 18; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: player.loadProfile(modelData) }
                                }
                            }
                        }
                        Row {
                            width: parent.width; spacing: 6
                            Rectangle {
                                width: parent.width - saveBtn.width - 6; height: 30; radius: 8 * Theme.round; color: Theme.surface
                                border.color: nameIn.activeFocus ? Theme.accent : Theme.surfaceHover
                                TextInput {
                                    id: nameIn; anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                                    verticalAlignment: TextInput.AlignVCenter; color: Theme.text; font.pixelSize: 16; maximumLength: 24
                                    clip: true; selectByMouse: true
                                    Text { font.family: Theme.font; anchors.verticalCenter: parent.verticalCenter; visible: !parent.text && !parent.activeFocus; text: t("profileName"); color: Theme.dim; font.pixelSize: 16 }
                                    onAccepted: saveBtn.save()
                                    Keys.onEscapePressed: { text = ""; keyRoot.forceActiveFocus() }
                                }
                            }
                            Rectangle {
                                id: saveBtn
                                function save() { if (nameIn.text.trim() !== "") { player.saveProfile(nameIn.text, bindSig.checked); nameIn.text = ""; keyRoot.forceActiveFocus() } }
                                width: saveTxt.implicitWidth + 24; height: 30; radius: 8 * Theme.round
                                color: nameIn.text.trim() !== "" ? (saveMa.containsMouse ? Theme.accentHover : Theme.accent) : Theme.surfaceHover
                                Text { font.family: Theme.font; id: saveTxt; anchors.centerIn: parent; text: t("saveProfile"); color: nameIn.text.trim() !== "" ? Theme.accentInk : Theme.muted; font.pixelSize: 16; font.weight: Font.Medium }
                                MouseArea { id: saveMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: saveBtn.save() }
                            }
                        }
                        Row {
                            id: bindSig
                            property bool checked: false
                            visible: player.signalKey !== ""
                            spacing: 8
                            Rectangle {
                                width: 16; height: 16; radius: 4 * Theme.round; anchors.verticalCenter: parent.verticalCenter
                                color: bindSig.checked ? Theme.accent : "transparent"; border.color: bindSig.checked ? Theme.accent : Theme.dim
                                Text { font.family: Theme.font; anchors.centerIn: parent; text: "✓"; visible: bindSig.checked; color: Theme.accentInk; font.pixelSize: 15 }
                            }
                            Text { font.family: Theme.font; text: t("bindSignal").replace("%1", player.signalKey); color: Theme.text; font.pixelSize: 15; width: secB.width - 30; wrapMode: Text.Wrap }
                            TapHandler { onTapped: bindSig.checked = !bindSig.checked }
                        }
                        Text { font.family: Theme.font; width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 15; color: Theme.muted; text: t("profilesHint") }
                    }
                    Column {
                        width: parent.width; spacing: 3
                        Text { font.family: Theme.font; width: parent.width; text: t("gpu") + ": " + (player.gpuName !== "" ? player.gpuName : t("cpuOnly")); color: Theme.muted; font.pixelSize: 15; wrapMode: Text.Wrap }
                        Text { font.family: Theme.font; width: parent.width; text: player.perf; color: Theme.label; font.pixelSize: 16; wrapMode: Text.Wrap }
                    }
                    Rectangle { width: parent.width; height: 1; color: Theme.line }
                    FrameGenSection { width: parent.width; tr: win.t }
                }
                Column {
                    id: secC
                    x: area.cols === 3 ? 2 * (area.colW + area.gap) : area.colW + area.gap
                    y: 0; width: area.colW
                    spacing: 20
                    Column {
                        width: parent.width; spacing: 8
                        Text { font.family: Theme.headFont; width: Math.min(implicitWidth, parent.width - 62); elide: Text.ElideRight; font.capitalization: Theme.headCaps ? Font.AllUppercase : Font.MixedCase; text: "Steam"; color: Theme.heading; font.pixelSize: 21 * Theme.headScale; font.weight: Font.Bold; font.letterSpacing: 0.5 }
                        Text { font.family: Theme.font; width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 15; color: Theme.muted; text: t("steamHint") }
                        Rectangle {
                            width: steamTxt.implicitWidth + 24; height: 32; radius: 10 * Theme.round
                            color: steamMa.containsMouse ? Theme.surfaceHover : Theme.surface
                            Text { font.family: Theme.font; id: steamTxt; anchors.centerIn: parent; text: t("steamAdd"); color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                            MouseArea { id: steamMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: player.addToSteam() }
                        }
                        Item {
                            width: parent.width; height: 26
                            Text { font.family: Theme.font; text: t("autostart"); color: Theme.label; font.pixelSize: 16; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 60; elide: Text.ElideRight }
                            ToggleSwitch { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                checked: player.autostart; onToggled: (v) => player.autostart = v }
                        }
                        Text { font.family: Theme.font; width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 15; color: Theme.muted; text: t("autostartHint") }
                        // controller: which one is connected, and how to open the menu with it
                        Item {
                            width: parent.width; height: 26
                            Text { font.family: Theme.font; text: t("controller"); color: Theme.label; font.pixelSize: 17; anchors.verticalCenter: parent.verticalCenter }
                            Text { font.family: Theme.font; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; width: parent.width * 0.62; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight
                                font.pixelSize: 14; color: gamepad.controllers.length > 0 ? Theme.ok : Theme.muted
                                text: gamepad.controllers.length > 0 ? "● " + t("padOn").replace("%1", gamepad.controllers.join(", ")) : t("padNone") }
                        }
                        Text { font.family: Theme.font; width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 15; color: Theme.muted; text: t("padHint") }
                    }
                    Rectangle { width: parent.width; height: 1; color: Theme.line }
                    Rectangle { width: parent.width; height: 1; color: Theme.line }
                    ObsSection { width: parent.width; tr: win.t }
                    Rectangle { width: parent.width; height: 1; color: Theme.line }
                    RecordSection { width: parent.width; tr: win.t }
                }
            }
            }
        }
    }

    CaptureDialog { id: capDlg; anchors.fill: parent; z: 100; tr: win.t; onRemotePlay: psDlg.openDialog() }
    RemotePlayDialog { id: psDlg; anchors.fill: parent; z: 101; tr: win.t }

    // ------------------------------------------------------------- keyboard
    Item {
        id: keyRoot
        anchors.fill: parent
        focus: true
        // demo scene: W A S D Q E Shift swim, 1-6 viewpoints (while the menu is closed)
        function demoKeyName(k) {
            switch (k) {
            case Qt.Key_W: return "w"; case Qt.Key_A: return "a"; case Qt.Key_S: return "s"; case Qt.Key_D: return "d"
            case Qt.Key_Q: return "q"; case Qt.Key_E: return "e"; case Qt.Key_Shift: return "shift"
            }
            return ""
        }
        Keys.onReleased: (e) => { const k = demoKeyName(e.key); if (k !== "" && !e.isAutoRepeat && player.demoActive) { player.demoKey(k, false); e.accepted = true } }
        Keys.onPressed: (e) => {
            if (psDlg.visible) { if (e.key === Qt.Key_Escape) { psDlg.close(); e.accepted = true } return }
            if (player.demoActive && !win.panelOpen && !capDlg.visible && !(e.modifiers & (Qt.ControlModifier | Qt.AltModifier))) {
                const k = demoKeyName(e.key)
                if (k !== "") { if (!e.isAutoRepeat) player.demoKey(k, true); e.accepted = true; return }
                if (e.key >= Qt.Key_1 && e.key <= Qt.Key_6) { player.demoPose(e.key - Qt.Key_1); e.accepted = true; return }
            }
            if (e.key >= Qt.Key_1 && e.key <= Qt.Key_9 && !(e.modifiers & (Qt.ControlModifier | Qt.AltModifier))) {
                player.loadProfileIndex(e.key - Qt.Key_1); e.accepted = true; return
            }
            wake()
            if (capDlg.visible) { if (e.key === Qt.Key_Escape) { capDlg.visible = false; e.accepted = true } return }
            switch (e.key) {
            case Qt.Key_Space: player.togglePlay(); break
            case Qt.Key_Left: player.seekRelative(e.modifiers & Qt.ShiftModifier ? -30 : -5); break
            case Qt.Key_Right: player.seekRelative(e.modifiers & Qt.ShiftModifier ? 30 : 5); break
            case Qt.Key_Up: player.volume = Math.min(1.5, player.volume + 0.05); break
            case Qt.Key_Down: player.volume = Math.max(0, player.volume - 0.05); break
            case Qt.Key_M: player.muted = !player.muted; break
            case Qt.Key_F: case Qt.Key_F11: win.visibility = win.fs ? Window.Windowed : Window.FullScreen; break
            case Qt.Key_Escape: if (win.fs) win.visibility = Window.Windowed; break
            case Qt.Key_E: player.enhance = !player.enhance; break
            case Qt.Key_P: win.panelOpen = !win.panelOpen; break
            case Qt.Key_O: player.openDialog(); break
            case Qt.Key_C: capDlg.openDialog(); break
            case Qt.Key_F3: player.overlay = !player.overlay; break
            case Qt.Key_F9: player.saveReplay(); break
            case Qt.Key_F10: player.toggleRecording(); break
            case Qt.Key_F12: case Qt.Key_Print: player.screenshot(); break
            default: return
            }
            e.accepted = true
        }
    }
    }   // hdrRoot
    Component.onCompleted: { hideTimer.start(); if (startDialog) capDlg.openDialog() }
}
