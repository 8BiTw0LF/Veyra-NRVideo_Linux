import QtQuick

// The Media and Games pages: one look for both. A row of sources on the left (your folders, Jellyfin and Plex servers
// / the systems your games are for), a grid of posters or box art, and a card with the details of what you pick.
// Media comes from `media` (MediaLibrary.h), games from `games` (GameLibrary.h).
Item {
    id: lib
    property var tr: function (k) { return k }
    property string mode: "media"            // "media" or "games"
    readonly property bool isMedia: mode === "media"
    readonly property real k: Math.max(0.8, Math.min(1.6, height / 900))
    property alias flick: grid
    // what the controller steers: the open card, else the page
    readonly property Item navScope: addDlg.visible ? addDlg : detail.visible ? detail : lib
    signal closed()
    visible: false

    function openPage(m) {
        mode = m
        detail.item = null
        addDlg.visible = false
        visible = true
        if (isMedia) media.refresh(); else games.refresh()

    }
    function close() { visible = false; detail.item = null; addDlg.visible = false; if (isMedia) media.cancelLogin(); closed() }
    // B / Escape: the card, then one folder up, then the page
    function back() {
        if (addDlg.visible) { addDlg.visible = false; media.cancelLogin(); return }
        if (detail.item) { detail.item = null; return }
        if (isMedia && media.back()) return
        close()
    }
    // LB / RB: the next source / system
    function switchSource(step) {
        const list = srcRepeater.model
        if (!list || list.length === 0) return
        let i = 0
        for (let j = 0; j < list.length; ++j) if (list[j].id === currentId) i = j
        i = (i + step + list.length) % list.length
        pickSource(list[i].id)
    }
    function pickSource(id) { detail.item = null; if (isMedia) media.select(id); else games.select(id) }
    readonly property string currentId: isMedia ? media.current : games.current
    // keep the controller's choice on screen
    function ensureVisible(it) {
        if (!it || !visible) return
        let p = it.mapToItem(grid.contentItem, 0, 0)
        if (p.y < 0 || p.y > grid.contentHeight + 10) return   // not in the grid
        if (p.y < grid.contentY + 10) grid.contentY = Math.max(0, p.y - 20)
        else if (p.y + it.height > grid.contentY + grid.height - 10)
            grid.contentY = Math.max(0, Math.min(grid.contentHeight - grid.height, p.y + it.height - grid.height + 20))
    }

    function sourceName(s) {
        if (s.kind === "local") return tr("mediaLocal")
        if (s.id === "recent") return tr("gamesRecent")
        return s.name
    }
    function sourceIcon(s) {
        if (!isMedia) return s.id === "recent" ? "icons/restore.svg" : "icons/joystick.svg"
        return s.kind === "local" ? "icons/folder.svg" : "icons/tv.svg"
    }
    function itemTitle(it) { return it.virtual ? tr(it.title) : it.kind === "action" ? tr("mediaAddFolder") : it.title }
    function fmtTime(s) {
        s = Math.max(0, Math.floor(s))
        const h = Math.floor(s / 3600), m = Math.floor(s / 60) % 60, sec = s % 60
        return (h > 0 ? h + ":" + (m < 10 ? "0" : "") : "") + m + ":" + (sec < 10 ? "0" : "") + sec
    }
    function activate(it, index) {
        if (isMedia) {
            if (it.kind === "folder" || it.kind === "action") media.openItem(index)
            else if (it.kind === "video") detail.show(it, index)
        } else {
            detail.show(it, index)
        }
    }

    // messages from the libraries (a Strings.js key and its %1)
    property string note: ""
    property bool noteError: false
    Timer { id: noteTimer; interval: 4500; onTriggered: lib.note = "" }
    function say(key, arg, error) { note = tr(key).replace("%1", arg); noteError = error; noteTimer.restart() }
    Connections { target: media; function onMessage(key, arg, error) { lib.say(key, arg, error) } }
    Connections { target: games; function onMessage(key, arg, error) { lib.say(key, arg, error) } }
    // a video or game that did not start says why (the player's status line is behind this page)
    Connections { target: player; function onStatusChanged() { if (lib.visible && player.status !== "" && !player.hasMedia) { lib.note = player.status; lib.noteError = true; noteTimer.restart() } } }

    Rectangle { anchors.fill: parent; color: Theme.bg; MouseArea { anchors.fill: parent } }

    component Btn: Rectangle {
        id: b
        property string text
        property url icon
        property bool primary: false
        signal clicked()
        width: Math.max(44 * lib.k, row.implicitWidth + 26 * lib.k); height: 38 * lib.k; radius: 10 * Theme.round
        opacity: enabled ? 1 : 0.4
        color: bm.containsMouse ? Theme.surfaceHover : Theme.surface
        gradient: primary ? (bm.containsMouse ? Theme.sunsetHover : Theme.sunset) : null
        border.width: Theme.edge; border.color: "#000000"
        Row {
            id: row; anchors.centerIn: parent; spacing: 7 * lib.k
            Image { visible: b.icon != ""; source: b.icon; width: 17 * lib.k; height: width; sourceSize: Qt.size(36, 36); anchors.verticalCenter: parent.verticalCenter }
            Text { visible: b.text !== ""; font.family: Theme.font; anchors.verticalCenter: parent.verticalCenter; text: b.text
                color: b.primary ? Theme.accentInk : Theme.text; font.pixelSize: 14 * lib.k; font.weight: b.primary ? Font.DemiBold : Font.Normal }
        }
        MouseArea { id: bm; anchors.fill: parent; hoverEnabled: true; enabled: b.enabled; cursorShape: Qt.PointingHandCursor; onClicked: b.clicked() }
    }
    component Field: Rectangle {
        id: f
        property alias text: inp.text
        property string placeholder
        property bool secret: false
        signal accepted()
        height: 38 * lib.k; radius: 9 * Theme.round; color: Theme.surface; border.color: inp.activeFocus ? Theme.accentLine : Theme.border
        TextInput {
            id: inp; anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
            verticalAlignment: TextInput.AlignVCenter; color: Theme.text; font.pixelSize: 15 * lib.k; clip: true; selectByMouse: true
            echoMode: f.secret ? TextInput.Password : TextInput.Normal
            onAccepted: f.accepted()
            Text { font.family: Theme.font; anchors.verticalCenter: parent.verticalCenter; visible: !parent.text && !parent.activeFocus
                text: f.placeholder; color: Theme.dim; font.pixelSize: 15 * lib.k; width: parent.width; elide: Text.ElideRight }
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.IBeamCursor; onClicked: inp.forceActiveFocus() }
    }

    // ------------------------------------------------------------- header
    Item {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: 18 * lib.k; rightMargin: 18 * lib.k; topMargin: 16 * lib.k }
        height: 52 * lib.k
        Row {
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            spacing: 12 * lib.k
            IconButton { size: 44 * lib.k; source: "icons/back.svg"; anchors.verticalCenter: parent.verticalCenter; onClicked: lib.back() }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                font.family: Theme.headFont; font.capitalization: Theme.headCaps ? Font.AllUppercase : Font.MixedCase
                text: lib.tr(lib.isMedia ? "startMedia" : "startGames"); color: Theme.heading
                font.pixelSize: 26 * Theme.headScale * lib.k; font.weight: Font.DemiBold
            }
            // where in the source we are
            Text {
                visible: lib.isMedia && media.crumbs.length > 0
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, header.width * 0.35); elide: Text.ElideLeft
                font.family: Theme.font; color: Theme.label; font.pixelSize: 15 * lib.k
                text: media.crumbs.map(function (c) { return /^media[A-Z]/.test(c) ? lib.tr(c) : c }).join("  ›  ")
            }
        }
        Row {
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            spacing: 8 * lib.k
            // Media: search the server (films, series, episodes); Enter searches, the list comes as a folder (B goes back)
            Rectangle {
                id: searchBox
                visible: lib.isMedia && media.canSearch
                width: 240 * lib.k; height: 38 * lib.k; radius: 10 * Theme.round
                color: Theme.surfaceDeep; border.width: Math.max(1, Theme.edge); border.color: searchIn.activeFocus ? Theme.accentLine : Theme.border
                Image { id: searchIcon; source: "icons/search.svg"; width: 16 * lib.k; height: width; sourceSize: Qt.size(32, 32); opacity: 0.7
                    anchors { left: parent.left; leftMargin: 10 * lib.k; verticalCenter: parent.verticalCenter } }
                TextInput {
                    id: searchIn
                    anchors { left: searchIcon.right; right: parent.right; leftMargin: 8 * lib.k; rightMargin: 10 * lib.k; verticalCenter: parent.verticalCenter }
                    font.family: Theme.font; font.pixelSize: 14 * lib.k; color: Theme.heading; clip: true
                    selectByMouse: true
                    onAccepted: { media.search(text); focus = false }
                    Keys.onEscapePressed: { text = ""; focus = false }
                }
                Text { anchors.fill: searchIn; verticalAlignment: Text.AlignVCenter; visible: searchIn.text === "" && !searchIn.activeFocus
                    font.family: Theme.font; font.pixelSize: 14 * lib.k; color: Theme.dim; text: lib.tr("mediaSearch") }
                // (a click, or A on the controller: the field takes the keys; Steam's own keyboard is Steam + X)
                MouseArea { anchors.fill: parent; visible: !searchIn.activeFocus; cursorShape: Qt.IBeamCursor
                    onClicked: { searchIn.forceActiveFocus(); Qt.inputMethod.show() } }
            }
            Btn { visible: lib.isMedia; icon: "icons/folder.svg"; text: lib.tr("mediaOpenFile"); onClicked: player.openDialog() }
            Btn { visible: lib.isMedia; icon: "icons/plus.svg"; text: "Jellyfin"; onClicked: { addDlg.kind = "jellyfin"; addDlg.visible = true } }
            Btn { visible: lib.isMedia; icon: "icons/plus.svg"; text: "Plex"; onClicked: { addDlg.kind = "plex"; addDlg.visible = true; media.addPlex() } }
            Btn { visible: lib.isMedia; icon: "icons/refresh.svg"; onClicked: media.refresh() }
            // Games
            Btn { visible: !lib.isMedia; icon: "icons/folder.svg"; text: lib.tr(games.folder === "" ? "gamesChooseFolder" : "gamesFolder"); onClicked: games.chooseFolder() }
            Btn { visible: !lib.isMedia && games.folder !== ""; icon: "icons/refresh.svg"; text: lib.tr("gamesRescan"); enabled: games.busy === ""; onClicked: games.rescan() }
            Btn { visible: !lib.isMedia && games.systems.length > 0; icon: "icons/image.svg"; text: lib.tr("gamesCovers"); enabled: games.busy === ""; onClicked: games.downloadCovers() }
            Btn { visible: !lib.isMedia; icon: "icons/system.svg"; text: "BIOS"; onClicked: games.openBiosFolder() }
            Btn { visible: !lib.isMedia; icon: "icons/trophy.svg"; text: games.raUser !== "" ? games.raUser : "RetroAchievements"
                onClicked: { addDlg.kind = "ra"; addDlg.visible = true } }
        }
    }

    // ------------------------------------------------------------- sources
    Flickable {
        id: side
        anchors { left: parent.left; top: header.bottom; bottom: parent.bottom; leftMargin: 18 * lib.k; topMargin: 14 * lib.k; bottomMargin: 18 * lib.k }
        width: 250 * lib.k
        contentHeight: sideCol.height; clip: true
        Column {
            id: sideCol
            width: parent.width; spacing: 6 * lib.k
            Repeater {
                id: srcRepeater
                model: lib.isMedia ? media.sources : games.systems
                delegate: Rectangle {
                    required property var modelData
                    readonly property bool sel: modelData.id === lib.currentId
                    width: sideCol.width; height: 46 * lib.k; radius: 11 * Theme.round
                    color: sel ? Theme.accentSoft : (sm.containsMouse ? Theme.surfaceHover : "transparent")
                    border.width: sel ? Math.max(1, Theme.edge) : 0; border.color: Theme.accentLine
                    Image { id: sIcon; source: lib.sourceIcon(modelData); width: 18 * lib.k; height: width; sourceSize: Qt.size(36, 36)
                        anchors { left: parent.left; leftMargin: 12 * lib.k; verticalCenter: parent.verticalCenter }
                        opacity: sel ? 1 : 0.75 }
                    Text {
                        anchors { left: sIcon.right; right: cnt.left; leftMargin: 10 * lib.k; rightMargin: 6; verticalCenter: parent.verticalCenter }
                        font.family: Theme.font; text: lib.sourceName(modelData); elide: Text.ElideRight
                        color: sel ? Theme.heading : Theme.text; font.pixelSize: 15 * lib.k; font.weight: sel ? Font.DemiBold : Font.Normal
                    }
                    Text { id: cnt; anchors { right: parent.right; rightMargin: 12 * lib.k; verticalCenter: parent.verticalCenter }
                        visible: !lib.isMedia; text: modelData.count !== undefined ? modelData.count : ""; color: Theme.muted; font.family: Theme.font; font.pixelSize: 13 * lib.k }
                    MouseArea { id: sm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: lib.pickSource(modelData.id) }
                }
            }
            // a server can be removed (its sign-in is forgotten)
            Btn {
                visible: lib.isMedia && lib.currentId !== "local"
                width: sideCol.width; icon: "icons/trash.svg"; text: lib.tr("mediaRemoveServer")
                onClicked: media.removeSource(lib.currentId)
            }
            Item { width: 1; height: 10 * lib.k }
            Text {
                visible: !lib.isMedia && games.folder !== ""
                width: sideCol.width; wrapMode: Text.WrapAnywhere; font.family: Theme.font; color: Theme.dim; font.pixelSize: 12 * lib.k
                text: games.folder
            }
        }
    }

    // ------------------------------------------------------------- the grid
    readonly property var items: isMedia ? media.items : games.items
    // Media: the posters of the cards on screen (and a little beyond) are fetched, once the grid has settled; the rest
    // wait until they are scrolled to
    Timer {
        id: wantTimer; interval: 120
        onTriggered: {
            if (!lib.isMedia || !lib.visible) return
            const top = grid.contentY - 400, bottom = grid.contentY + grid.height + 400
            for (let i = flow.children.length - 1; i >= 0; --i) {
                const c = flow.children[i]
                if (c.modelData && c.y + c.height > top && c.y < bottom) media.want(c.modelData.key)
            }
        }
    }
    Connections { target: grid; function onContentYChanged() { wantTimer.restart() } function onHeightChanged() { wantTimer.restart() } }
    Connections { target: flow; function onHeightChanged() { wantTimer.restart() } }
    Connections { target: media; function onItemsChanged() { wantTimer.restart() } }
    Flickable {
        id: grid
        anchors { left: side.right; right: parent.right; top: header.bottom; bottom: parent.bottom; leftMargin: 18 * lib.k; rightMargin: 18 * lib.k; topMargin: 14 * lib.k }
        contentHeight: flow.height + 30 * lib.k; clip: true
        boundsBehavior: Flickable.StopAtBounds
        Flow {
            id: flow
            width: grid.width
            spacing: 16 * lib.k
            Repeater {
                model: lib.items
                delegate: Rectangle {
                    id: card
                    required property var modelData
                    required property int index
                    readonly property bool wide: modelData.shape === "wide"
                    readonly property real picW: (lib.isMedia ? (wide ? 280 : 172) : 178) * lib.k
                    readonly property real picH: (lib.isMedia ? (wide ? 158 : 258) : 236) * lib.k
                    width: picW + 16 * lib.k; height: picH + 70 * lib.k
                    radius: 14 * Theme.round
                    color: cm.containsMouse ? Theme.surfaceHover : Theme.surface
                    border.width: Math.max(1, Theme.edge); border.color: cm.containsMouse ? Theme.accentLine : Theme.border
                    Behavior on color { ColorAnimation { duration: 120 } }
                    Rectangle {
                        id: pic
                        x: 8 * lib.k; y: 8 * lib.k; width: card.picW; height: card.picH; radius: 9 * Theme.round
                        color: Theme.surfaceDeep; clip: true
                        Image {
                            id: img
                            anchors.fill: parent
                            source: (lib.isMedia && media.imageVersion >= 0 ? media.image(card.modelData.key) : "") || card.modelData.image || ""
                            fillMode: lib.isMedia ? Image.PreserveAspectCrop : Image.PreserveAspectFit
                            asynchronous: true; cache: true
                            sourceSize: Qt.size(card.picW * 1.5, card.picH * 1.5)
                        }
                        // no picture: the kind's icon
                        Rectangle {
                            visible: img.status !== Image.Ready
                            anchors.centerIn: parent; width: 64 * lib.k; height: width; radius: width / 2 * Theme.round
                            gradient: Theme.sunset
                            Image { anchors.centerIn: parent; width: 30 * lib.k; height: width; sourceSize: Qt.size(64, 64)
                                source: card.modelData.kind === "action" ? "icons/plus.svg" : card.modelData.kind === "folder" ? (card.modelData.title === "mediaResume" || card.modelData.title === "mediaRecent" ? "icons/restore.svg" : card.modelData.title === "mediaGenres" ? "icons/image.svg" : "icons/folder.svg")
                                      : card.modelData.kind === "game" ? "icons/joystick.svg" : "icons/play.svg" }
                        }
                        // watched / how far
                        Rectangle {
                            visible: (card.modelData.progress || 0) > 0.01 && !card.modelData.played
                            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                            height: 5 * lib.k; color: "#99000000"
                            Rectangle { width: parent.width * Math.min(1, card.modelData.progress || 0); height: parent.height; color: Theme.accent }
                        }
                        Rectangle {
                            visible: !!card.modelData.played
                            anchors { right: parent.right; top: parent.top; margins: 6 * lib.k }
                            width: 26 * lib.k; height: width; radius: width / 2; color: Theme.accent
                            Image { anchors.centerIn: parent; source: "icons/check.svg"; width: 15 * lib.k; height: width; sourceSize: Qt.size(32, 32) }
                        }
                    }
                    Column {
                        anchors { left: parent.left; right: parent.right; top: pic.bottom; leftMargin: 10 * lib.k; rightMargin: 10 * lib.k; topMargin: 8 * lib.k }
                        spacing: 2 * lib.k
                        Text { width: parent.width; elide: Text.ElideRight; font.family: Theme.font; color: Theme.heading
                            font.pixelSize: 14 * lib.k; font.weight: Font.DemiBold; text: lib.itemTitle(card.modelData) }
                        Text { width: parent.width; elide: Text.ElideRight; font.family: Theme.font; color: Theme.muted
                            font.pixelSize: 12 * lib.k; text: card.modelData.sub || "" }
                    }
                    MouseArea { id: cm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: lib.activate(card.modelData, card.index) }
                }
            }
        }
    }

    // empty page, busy line
    Column {
        anchors.centerIn: grid
        width: Math.min(560 * lib.k, grid.width - 40)
        spacing: 14 * lib.k
        visible: lib.items.length === 0 || (!lib.isMedia && lib.items.length === 0)
        Text {
            width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
            font.family: Theme.font; color: Theme.label; font.pixelSize: 16 * lib.k
            text: lib.isMedia ? (media.busy ? lib.tr("mediaLoading") : lib.tr("mediaEmpty"))
                  : games.busy !== "" ? "" : games.folder === "" ? lib.tr("gamesIntro") : lib.tr("gamesEmpty")
        }
        Btn { visible: !lib.isMedia && games.folder === ""; anchors.horizontalCenter: parent.horizontalCenter; primary: true
            icon: "icons/folder.svg"; text: lib.tr("gamesChooseFolder"); onClicked: games.chooseFolder() }
    }
    Rectangle {
        visible: (!lib.isMedia && games.busy !== "") || lib.note !== ""
        anchors { horizontalCenter: grid.horizontalCenter; bottom: parent.bottom; bottomMargin: 22 * lib.k }
        width: Math.min(busyRow.implicitWidth + 36 * lib.k, grid.width - 20); height: 44 * lib.k; radius: height / 2 * Theme.round
        color: Theme.toast; border.color: lib.noteError && games.busy === "" ? Theme.warn : Theme.accentLine
        Row {
            id: busyRow; anchors.centerIn: parent; spacing: 10 * lib.k
            Image { visible: !lib.isMedia && games.busy !== ""; source: "icons/spinner.svg"; width: 18 * lib.k; height: width; sourceSize: Qt.size(36, 36)
                anchors.verticalCenter: parent.verticalCenter
                RotationAnimation on rotation { from: 0; to: 360; duration: 900; loops: Animation.Infinite; running: parent.visible } }
            Text {
                anchors.verticalCenter: parent.verticalCenter; font.family: Theme.font; font.pixelSize: 14 * lib.k
                color: lib.noteError && games.busy === "" ? Theme.warn : Theme.text
                text: !lib.isMedia && games.busy !== ""
                      ? lib.tr(games.busy).replace("%1", games.busyArg) + (games.progress >= 0 ? "  " + games.progress + " %" : "")
                      : lib.note
            }
        }
    }

    // ------------------------------------------------------------- details of a video / game
    Item {
        id: detail
        property var item: null
        property int index: -1
        anchors.fill: parent
        visible: item !== null
        z: 5
        function show(it, i) { item = it; index = i }
        Rectangle { anchors.fill: parent; color: "#cc000000"; MouseArea { anchors.fill: parent; onClicked: detail.item = null } }
        Rectangle {
            id: dcard
            anchors.centerIn: parent
            width: Math.min(760 * lib.k, parent.width - 40); height: Math.min(dRow.implicitHeight + 44 * lib.k, parent.height - 40)
            radius: 16 * Theme.round; color: Theme.surfaceDeep; border.color: Theme.border
            MouseArea { anchors.fill: parent }
            Row {
                id: dRow
                anchors { fill: parent; margins: 22 * lib.k }
                spacing: 22 * lib.k
                Rectangle {
                    id: dPic
                    readonly property bool wide: detail.item && detail.item.shape === "wide"
                    width: (wide ? 300 : 200) * lib.k; height: (wide ? 169 : 290) * lib.k; radius: 10 * Theme.round
                    color: Theme.surface; clip: true
                    Image { anchors.fill: parent; source: !detail.item ? "" : (lib.isMedia && media.imageVersion >= 0 ? media.image(detail.item.key) : "") || detail.item.image || ""
                        fillMode: lib.isMedia ? Image.PreserveAspectCrop : Image.PreserveAspectFit; asynchronous: true }
                }
                Column {
                    width: parent.width - dPic.width - parent.spacing
                    spacing: 10 * lib.k
                    Text { width: parent.width; wrapMode: Text.Wrap; font.family: Theme.headFont; color: Theme.heading
                        font.capitalization: Theme.headCaps ? Font.AllUppercase : Font.MixedCase
                        font.pixelSize: 22 * Theme.headScale * lib.k; font.weight: Font.DemiBold; text: detail.item ? detail.item.title : "" }
                    Text { width: parent.width; wrapMode: Text.Wrap; font.family: Theme.font; color: Theme.label; font.pixelSize: 14 * lib.k
                        text: detail.item ? (detail.item.sub || "") : "" }
                    Text { width: parent.width; wrapMode: Text.Wrap; font.family: Theme.font; color: Theme.muted; font.pixelSize: 13 * lib.k
                        maximumLineCount: 8; elide: Text.ElideRight; visible: text !== ""
                        text: detail.item && detail.item.overview ? detail.item.overview : "" }
                    Item { width: 1; height: 6 * lib.k }
                    Flow {
                        width: parent.width; spacing: 10 * lib.k
                        // media: continue / from the start
                        Btn {
                            visible: lib.isMedia && detail.item && (detail.item.resume || 0) > 5
                            primary: true; icon: "icons/play.svg"
                            text: detail.item ? lib.tr("mediaContinue").replace("%1", lib.fmtTime(detail.item.resume || 0)) : ""
                            onClicked: { const i = detail.index; detail.item = null; lib.startPlay(i, false) }
                        }
                        Btn {
                            visible: lib.isMedia
                            primary: !(detail.item && (detail.item.resume || 0) > 5)
                            icon: "icons/play.svg"; text: lib.tr(detail.item && (detail.item.resume || 0) > 5 ? "mediaFromStart" : "mediaPlay")
                            onClicked: { const i = detail.index; detail.item = null; lib.startPlay(i, true) }
                        }
                        // games
                        // quit before: Continue (from where it was) first, Start over next to it
                        Btn { visible: !lib.isMedia && detail.item && detail.item.canResume; primary: true; icon: "icons/play.svg"; text: lib.tr("gamesContinue")
                            onClicked: { const k = detail.item.key; detail.item = null; lib.startGame(k, true) } }
                        Btn { visible: !lib.isMedia; primary: !(detail.item && detail.item.canResume); icon: "icons/play.svg"
                            text: lib.tr(detail.item && detail.item.canResume ? "gamesStartOver" : "gamesPlay")
                            onClicked: { const k = detail.item.key; detail.item = null; lib.startGame(k, false) } }
                        Btn { visible: !lib.isMedia; icon: "icons/image.svg"; text: lib.tr("gamesOwnCover")
                            onClicked: games.setCover(detail.item.key) }
                        Btn { text: lib.tr("cancel"); onClicked: detail.item = null }
                    }
                    Text {
                        visible: !lib.isMedia && detail.item && games.biosHint(detail.item.system) !== ""
                        width: parent.width; wrapMode: Text.Wrap; font.family: Theme.font; color: Theme.dim; font.pixelSize: 12 * lib.k
                        text: detail.item ? lib.tr("gamesBiosHint").replace("%1", games.biosHint(detail.item.system)) : ""
                    }
                }
            }
        }
    }

    // playing: the page steps aside; Main.qml brings it back when the video or game is closed
    signal started()
    function startPlay(i, fromStart) { media.play(i, fromStart); if (player.hasMedia) { visible = false; started() } }
    function startGame(path, resume) { games.play(path, resume) }
    Connections {
        target: player
        function onMediaChanged() { if (lib.visible && player.hasMedia) { lib.visible = false; detail.item = null; lib.started() } }
    }

    // ------------------------------------------------------------- adding a server
    Item {
        id: addDlg
        property string kind: "jellyfin"
        anchors.fill: parent
        visible: false
        z: 6
        Rectangle { anchors.fill: parent; color: "#cc000000"; MouseArea { anchors.fill: parent; onClicked: lib.back() } }
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(560 * lib.k, parent.width - 40); height: addCol.height + 44 * lib.k
            radius: 16 * Theme.round; color: Theme.surfaceDeep; border.color: Theme.border
            MouseArea { anchors.fill: parent }
            Column {
                id: addCol
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 22 * lib.k }
                spacing: 10 * lib.k
                Text { font.family: Theme.headFont; color: Theme.heading; font.pixelSize: 20 * Theme.headScale * lib.k; font.weight: Font.DemiBold
                    font.capitalization: Theme.headCaps ? Font.AllUppercase : Font.MixedCase
                    text: addDlg.kind === "plex" ? "Plex" : addDlg.kind === "ra" ? "RetroAchievements" : "Jellyfin" }
                // RetroAchievements: signed in (and signing out), or the user name and password
                Column {
                    visible: addDlg.kind === "ra"
                    width: parent.width; spacing: 8 * lib.k
                    Text { width: parent.width; wrapMode: Text.Wrap; font.family: Theme.font; color: Theme.muted; font.pixelSize: 13 * lib.k
                        text: games.raUser !== "" ? lib.tr("raSignedInAs").replace("%1", games.raUser) : lib.tr("raHint") }
                    Field { id: raUserIn; visible: games.raUser === ""; width: parent.width; placeholder: lib.tr("mediaUser") }
                    Field { id: raPassIn; visible: games.raUser === ""; width: parent.width; placeholder: lib.tr("mediaPassword"); secret: true
                        onAccepted: games.raSignIn(raUserIn.text, raPassIn.text) }
                    Flow {
                        width: parent.width; spacing: 8 * lib.k
                        Btn { visible: games.raUser === ""; primary: true; text: lib.tr("mediaSignIn"); enabled: raUserIn.text !== "" && raPassIn.text !== "" && !games.raBusy
                            onClicked: games.raSignIn(raUserIn.text, raPassIn.text) }
                        Btn { visible: games.raUser !== ""; text: lib.tr("raSignOut"); onClicked: games.raSignOut() }
                        Btn { text: lib.tr(games.raUser !== "" ? "close" : "cancel"); onClicked: lib.back() }
                    }
                }
                // Jellyfin: the address, then a password or Quick Connect
                Column {
                    visible: addDlg.kind === "jellyfin" && media.login.kind !== "quick"
                    width: parent.width; spacing: 8 * lib.k
                    Text { width: parent.width; wrapMode: Text.Wrap; font.family: Theme.font; color: Theme.muted; font.pixelSize: 13 * lib.k; text: lib.tr("mediaJellyfinHint") }
                    Field { id: jfUrl; width: parent.width; placeholder: "http://192.168.1.10:8096" }
                    Field { id: jfUser; width: parent.width; placeholder: lib.tr("mediaUser") }
                    Field { id: jfPass; width: parent.width; placeholder: lib.tr("mediaPassword"); secret: true
                        onAccepted: if (jfUrl.text !== "" && jfUser.text !== "") media.addJellyfin(jfUrl.text, jfUser.text, jfPass.text) }
                    Flow {
                        width: parent.width; spacing: 8 * lib.k
                        Btn { primary: true; text: lib.tr("mediaSignIn"); enabled: jfUrl.text !== "" && jfUser.text !== "" && !media.busy
                            onClicked: media.addJellyfin(jfUrl.text, jfUser.text, jfPass.text) }
                        Btn { text: "Quick Connect"; enabled: jfUrl.text !== "" && !media.busy; onClicked: media.jellyfinQuickConnect(jfUrl.text) }
                        Btn { text: lib.tr("cancel"); onClicked: lib.back() }
                    }
                }
                // a code to confirm elsewhere: Jellyfin's Quick Connect, or Plex's PIN (the browser opened plex.tv)
                Column {
                    visible: addDlg.kind !== "ra" && (media.login.kind === "quick" || addDlg.kind === "plex")
                    width: parent.width; spacing: 10 * lib.k
                    Text { width: parent.width; wrapMode: Text.Wrap; font.family: Theme.font; color: Theme.muted; font.pixelSize: 13 * lib.k
                        text: lib.tr(media.login.kind === "quick" ? "mediaQuickHint" : "mediaPlexHint") }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; font.family: "monospace"; font.letterSpacing: 6 * lib.k
                        color: Theme.heading; font.pixelSize: 38 * lib.k; font.weight: Font.Bold
                        text: media.login.code || "…" }
                    Text { visible: addDlg.kind === "plex"; width: parent.width; wrapMode: Text.Wrap; font.family: Theme.font; color: Theme.dim
                        font.pixelSize: 12 * lib.k; text: lib.tr("mediaPlexPass") }
                    Row {
                        spacing: 8 * lib.k
                        Image { source: "icons/spinner.svg"; width: 18 * lib.k; height: width; sourceSize: Qt.size(36, 36); anchors.verticalCenter: parent.verticalCenter
                            RotationAnimation on rotation { from: 0; to: 360; duration: 900; loops: Animation.Infinite; running: addDlg.visible } }
                        Text { anchors.verticalCenter: parent.verticalCenter; font.family: Theme.font; color: Theme.label; font.pixelSize: 13 * lib.k; text: lib.tr("mediaWaiting") }
                    }
                    Btn { text: lib.tr("cancel"); onClicked: lib.back() }
                }
            }
        }
        // signed in: the dialog closes by itself
        Connections { target: media; function onSourcesChanged() { if (addDlg.visible && media.login.kind === undefined) addDlg.visible = false } }
        Connections { target: games; function onRaChanged() { if (addDlg.visible && addDlg.kind === "ra" && games.raUser !== "") { raPassIn.text = ""; addDlg.visible = false } } }
    }
}
