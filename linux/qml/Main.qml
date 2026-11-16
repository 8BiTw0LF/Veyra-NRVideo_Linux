import QtQuick
import QtQuick.Window
import VeyAmux 1.0

Window {
    id: win
    visible: false          // shown from C++ once the graphics device is configured
    width: 1280
    height: 720
    minimumWidth: 720
    minimumHeight: 420
    color: Theme.bg
    title: player.hasMedia ? player.title + " — VeyAmux" : "VeyAmux"

    readonly property bool fs: visibility === Window.FullScreen
    property bool panelOpen: startPanel
    property bool controlsShown: true
    property bool scrubbing: false
    property real scrubValue: 0

    readonly property var strings: ({
        en: { open: "Open file", drop: "Drop a video here, or open a file", enhance: "Enhance",
              upscale: "Upscale", off: "Off", aa: "Anti-aliasing", aaq: ["Medium", "High", "Ultra"],
              sharpen: "Sharpness", exposure: "Exposure", contrast: "Contrast", saturation: "Saturation",
              hdr: "HDR capture (P010): source peak brightness",
              hdrHint: "Used when the capture card sends HDR (pick a P010 (HDR) mode under Capture card). The HDR picture is tone-mapped to your SDR display (BT.2390, as in Veyra); match the peak to the game's HDR calibration - lower = brighter.",
              vrr: "VRR: the game's own frame rate, evenly paced",
              vrrHint: "Shows only the game's new pictures, spaced evenly at its own rate (e.g. 45 fps), for a display with VRR/adaptive sync. Turn on Adaptive sync in the display settings and run VeyAmux fullscreen. Adds up to about half a frame of latency.",
              vrrRestart: "VeyAmux must restart to switch vsync for VRR.",
              profiles: "Profiles", profileName: "Name, e.g. PS5", saveProfile: "Save", bindSignal: "Use automatically for this signal (%1)", profilesHint: "Saves the picture settings and the audio delay. Keys 1-9 switch profile; ⚡ = applied automatically when that capture signal is opened.",
              normalize: "Even out loudness", audioOut: "Sound output", audioOutDefault: "System default",
              audioDelay: "Audio delay (ms)", audioDelayHint: "Positive = sound later. Use it when the picture lags the sound (e.g. with Lossless Scaling). Capture cards can only delay the sound.",
              denoise: "Noise reduction", denoiseModes: ["Off", "Spatial", "Temporal", "Both"], denoiseLevels: ["Light", "Medium", "Strong"],
              denoiseHint0: "Removes grain, dither and compression noise before upscaling and sharpening, so Sharpness can be turned up without sharpening the noise.",
              denoiseHint1: "Non-local means: averages each pixel with similar-looking pixels nearby. Frame by frame, never smears motion; strong settings soften fine texture a little.",
              denoiseHint2: "Averages each pixel over several frames along the measured motion: keeps the most detail. Fast, unpredictable motion falls back to the current frame.",
              denoiseHint3: "Temporal first, then a gentle spatial pass: the cleanest picture. Pick the level after how noisy the source is.",
              reset: "Reset", gpu: "GPU", cpuOnly: "No GPU — passthrough", hint: "Space play/pause · ←/→ seek · F fullscreen · E enhance · M mute · C capture card",
              native: "Native",
              method: "Method", lanczos: "Lanczos", fsr31: "AMD FSR 3.1",
              lanczosHint: "Sharp classic scaling, frame by frame. No added latency.",
              fsr1Hint: "AMD FidelityFX Super Resolution 1 (open source): edge-adaptive upscaling (EASU) plus RCAS sharpening, frame by frame. Made for game pictures, no added latency.",
              fsr31Hint: "AMD FidelityFX (open source), builds the picture over several frames from motion VeyAmux measures in the video. Experimental: FSR is made for games, and in VeyAmux's tests on video it came out softer than Lanczos.",
              fsrNeedsScale: "Choose 1.5×, 2× or 3× to use it.",
              fsrFallback: "FSR 3.1 could not start, using Lanczos: ",
              capture: "Capture card", device: "Device", mode: "Mode (resolution · rate · format)", audio: "Audio source", noAudio: "No audio", defaultInput: "Default input (system default microphone)",
              noDevices: "No capture devices found. Check that the card is connected (it must appear as /dev/video*).",
              refresh: "Refresh", cancel: "Cancel", start: "Start", live: "LIVE",
              fg: "Frame generation", fgOff: "Off",
              obs: "OBS (browser source)",
              obsOff: "Off. Turn on to stream the picture to OBS while you use VeyAmux.",
              obsWaiting: "Ready - waiting for OBS (%1).",
              obsLive: "OBS is receiving the picture (%1 connected).",
              obsRes: "Resolution sent to OBS",
              obsSave: "Save browser file…", obsCopy: "Copy address",
              obsSaved: "Browser file: %1",
              autostart: "Start when the capture card is plugged in", autostartHint: "A small background helper (started with your desktop) opens VeyAmux fullscreen as soon as the card you used last appears.",
              steamAdd: "Add to Steam", steamHint: "Adds VeyAmux to your Steam library as a non-Steam game: it starts fullscreen on the last capture card, also from Big Picture and Game Mode. Close Steam first.",
              hdrOut: "HDR output (experimental)",
              gamescope: "Start in gamescope (HDR + VRR)",
              gamescopeHint: "Runs VeyAmux fullscreen inside gamescope, which gives HDR and adaptive sync even through XWayland. Takes effect at the next start.",
              gamescopeOn: "Running inside gamescope.",
              hdrOutOn: "HDR display active: HDR sources are shown up to the display peak above, the rest at 203 nits SDR white.",
              hdrOutNo: "No HDR swapchain: turn on HDR in the system's display settings (KDE Plasma 6 / GNOME 48 or newer) and restart VeyAmux.",
              hdrOutRestart: "Takes effect when VeyAmux restarts.",
              hdrOutSw: "VeyAmux is running with the software renderer (Vulkan could not start), so there is no HDR output.",
              hdrOutX11: "VeyAmux runs through XWayland on this system, and XWayland has no HDR. Start it natively on Wayland (VEYAMUX_UI=vulkan with QT_QPA_PLATFORM=wayland) to use HDR output.",
              overlayHint: "F3 hides · line = frame time (2 s), turquoise = average",
              streamTitle: "Live stream", streamOwn: "Own server", streamKey: "Stream key", streamStart: "Go live", streamStop: "Stop stream",
              streamHint: "Streams VeyAmux's picture and sound straight to Twitch, YouTube or your own RTMP server (H.264, encoded on the GPU), without OBS. Lossless Scaling's generated frames are not included. The key is stored in VeyAmux's settings file.",
              recTitle: "Instant replay & recording", recOff: "Instant replay is off. F10 records, F12 takes a screenshot.",
              recReplayOn: "the last %1 s are kept (F9 saves)", recLength: "Replay length", recRes: "Recording resolution",
              recSave: "Save replay (F9)", recStart: "Record (F10)", recStop: "Stop (F10)", recShot: "Screenshot (F12)", recFolder: "Folder",
              obsHelp: "In OBS: Sources → + → Browser → tick \"Local file\" → pick the saved file, and set width/height to your canvas (e.g. 1920 × 1080). Only the video is sent: add the sound with Application Audio Capture (VeyAmux) or straight from the capture card.",
              lsPerf: "Performance mode",
              fsrFgTitle: "Free (without Lossless Scaling)", fsrFg: "AMD FSR 3.1 2× (free)",
              fsrFgHint: "Open-source frame generation inside VeyAmux, for capture cards: doubles the frame rate (60 → 120 fps). Motion is estimated from the pictures, so quality is below Lossless Scaling, and the real frame is shown half a frame later.",
              fgPaused: "Paused automatically: the capture runs at about 120 Hz, so no frames are generated. Resumes when it drops below 80 fps.",
              lsPerfHint: "Lighter Lossless Scaling model: faster, slightly lower quality. Flow scale is set automatically for your resolution (now %1 %).",
              lsFixed: "Fixed 2× (doubles)", lsFixedHint: "Always doubles the frame rate (60 → 120 fps).",
              ls3x: "Fixed 3× (triples)", ls3xHint: "Always triples the frame rate (60 → 180 fps, 40 → 120 fps). Needs a display fast enough for the result.",
              lsActive: "Active through lsfg-vk. Lossless Scaling: %1", lsReady: "Lossless Scaling found: %1",
              lsRestart: "VeyAmux must restart to turn Lossless Scaling on.",
              lsNoInstall: "Lossless Scaling was not found in your Steam libraries. It has to be bought and installed through Steam.",
              lsNoDll: "Only Lossless.dll was found. lsfg-vk 2 needs lsfg-vk.dll: in Steam open Lossless Scaling → Properties → Betas and choose the lsfg-vk branch.",
              lsNoLayer: "lsfg-vk is not installed. Install it from lsfg-vk.dev; VeyAmux uses it to run Lossless Scaling on Linux.",
              lsOldLayer: "An older lsfg-vk (1.x) was found. VeyAmux needs lsfg-vk 2.0 or newer.",
              lsNoVulkan: "Needs the Vulkan display mode; VeyAmux is running in software mode.",
              lsCrashed: "VeyAmux closed unexpectedly the last time it started with Lossless Scaling, so it has been turned off.",
              restartNow: "Restart now", tryAgain: "Try again", chooseFolder: "Choose folder…", checkAgain: "Check again" },
        da: { open: "Åbn fil", drop: "Træk en video hertil, eller åbn en fil", enhance: "Forbedring",
              upscale: "Opskalering", off: "Fra", aa: "Anti-aliasing", aaq: ["Medium", "Høj", "Ultra"],
              sharpen: "Skarphed", exposure: "Eksponering", contrast: "Kontrast", saturation: "Farvemætning",
              hdr: "HDR-capture (P010): kildens maks. lysstyrke",
              hdrHint: "Bruges når capture-kortet sender HDR (vælg en P010 (HDR)-tilstand under Capture-kort). HDR-billedet tonemappes til din SDR-skærm (BT.2390, som i Veyra); vælg værdien efter spillets HDR-kalibrering – lavere = lysere.",
              vrr: "VRR: spillets egen billedfrekvens i jævn takt",
              vrrHint: "Viser kun spillets nye billeder, jævnt fordelt i dets egen takt (fx 45 fps), til en skærm med VRR/adaptiv synkronisering. Slå Adaptiv synkronisering til i skærmindstillingerne, og kør VeyAmux i fuldskærm. Giver op til ca. et halvt billedes ekstra forsinkelse.",
              vrrRestart: "VeyAmux skal genstarte for at skifte vsync til VRR.",
              profiles: "Profiler", profileName: "Navn, fx PS5", saveProfile: "Gem", bindSignal: "Brug automatisk til dette signal (%1)", profilesHint: "Gemmer billedindstillingerne og lydforsinkelsen. Tasterne 1-9 skifter profil; ⚡ = bruges automatisk, når det capture-signal åbnes.",
              normalize: "Udlign lydstyrke", audioOut: "Lydudgang", audioOutDefault: "Systemets standard",
              audioDelay: "Lydforsinkelse (ms)", audioDelayHint: "Positiv = lyden senere. Brug den, når billedet halter efter lyden (fx med Lossless Scaling). Ved capture-kort kan lyden kun forsinkes.",
              denoise: "Støjfjerning", denoiseModes: ["Fra", "Rumlig", "Temporal", "Begge"], denoiseLevels: ["Let", "Middel", "Kraftig"],
              denoiseHint0: "Fjerner korn, dithering og komprimeringsstøj før opskalering og skarphed, så Skarphed kan skrues op uden at gøre støjen skarp.",
              denoiseHint1: "Non-local means: blander hver pixel med pixels i nærheden, der ligner den. Billede for billede, slører aldrig bevægelse; kraftig gør fin tekstur en anelse blødere.",
              denoiseHint2: "Midler hver pixel over flere billeder langs den målte bevægelse: bevarer flest detaljer. Ved hurtig, uforudsigelig bevægelse bruges det aktuelle billede.",
              denoiseHint3: "Først temporal, så en mild rumlig: det reneste billede. Vælg niveau efter hvor støjfyldt kilden er.",
              reset: "Nulstil", gpu: "GPU", cpuOnly: "Ingen GPU — uden forbedring", hint: "Mellemrum afspil/pause · ←/→ spol · F fuldskærm · E forbedring · M lyd fra · C capture-kort",
              native: "Original",
              method: "Metode", lanczos: "Lanczos", fsr31: "AMD FSR 3.1",
              lanczosHint: "Skarp klassisk skalering, billede for billede. Ingen ekstra forsinkelse.",
              fsr1Hint: "AMD FidelityFX Super Resolution 1 (open source): kantfølsom opskalering (EASU) plus RCAS-skarphed, billede for billede. Lavet til spilbilleder, ingen ekstra forsinkelse.",
              fsr31Hint: "AMD FidelityFX (open source), opbygger billedet over flere billeder ud fra bevægelse, som VeyAmux måler i videoen. Eksperimentel: FSR er lavet til spil, og i VeyAmux' test på video blev den blødere end Lanczos.",
              fsrNeedsScale: "Vælg 1,5×, 2× eller 3× for at bruge den.",
              fsrFallback: "FSR 3.1 kunne ikke starte, bruger Lanczos: ",
              capture: "Capture-kort", device: "Enhed", mode: "Tilstand (opløsning · frekvens · format)", audio: "Lydkilde", noAudio: "Ingen lyd", defaultInput: "Standard-indgang (systemets standardmikrofon)",
              noDevices: "Ingen capture-enheder fundet. Tjek at kortet er tilsluttet (det skal vises som /dev/video*).",
              refresh: "Opdater", cancel: "Annuller", start: "Start", live: "LIVE",
              fg: "Frame generation", fgOff: "Fra",
              obs: "OBS (browserkilde)",
              obsOff: "Fra. Slå til for at sende billedet til OBS, mens du bruger VeyAmux.",
              obsWaiting: "Klar - venter på OBS (%1).",
              obsLive: "OBS modtager billedet (%1 tilsluttet).",
              obsRes: "Opløsning til OBS",
              obsSave: "Gem browserfil…", obsCopy: "Kopiér adresse",
              obsSaved: "Browserfil: %1",
              autostart: "Start når capture-kortet tilsluttes", autostartHint: "En lille hjælper i baggrunden (starter med skrivebordet) åbner VeyAmux i fuldskærm, så snart det kort, du brugte sidst, dukker op.",
              steamAdd: "Tilføj til Steam", steamHint: "Lægger VeyAmux i dit Steam-bibliotek som non-Steam-spil: det starter i fuldskærm på det sidste capture-kort, også fra Big Picture og Game Mode. Luk Steam først.",
              hdrOut: "HDR-output (eksperimentel)",
              gamescope: "Start i gamescope (HDR + VRR)",
              gamescopeHint: "Kører VeyAmux i fuldskærm inde i gamescope, som giver HDR og adaptiv synkronisering selv gennem XWayland. Træder i kraft ved næste start.",
              gamescopeOn: "Kører inde i gamescope.",
              hdrOutOn: "HDR-skærm aktiv: HDR-kilder vises op til skærmens top-lysstyrke ovenfor, resten med 203 nits SDR-hvid.",
              hdrOutNo: "Ingen HDR-swapchain: slå HDR til i systemets skærmindstillinger (KDE Plasma 6 / GNOME 48 eller nyere), og genstart VeyAmux.",
              hdrOutRestart: "Træder i kraft, når VeyAmux genstartes.",
              hdrOutSw: "VeyAmux kører med software-tegning (Vulkan kunne ikke starte), så der er ingen HDR-output.",
              hdrOutX11: "VeyAmux kører gennem XWayland på dette system, og XWayland har ikke HDR. Start det direkte på Wayland (VEYAMUX_UI=vulkan med QT_QPA_PLATFORM=wayland) for at bruge HDR-output.",
              overlayHint: "F3 skjuler · linje = billedtid (2 s), turkis = gennemsnit",
              streamTitle: "Livestream", streamOwn: "Egen server", streamKey: "Stream-nøgle", streamStart: "Gå live", streamStop: "Stop stream",
              streamHint: "Sender VeyAmux' billede og lyd direkte til Twitch, YouTube eller din egen RTMP-server (H.264, kodet på grafikkortet), uden OBS. Lossless Scalings mellembilleder kommer ikke med. Nøglen gemmes i VeyAmux' indstillingsfil.",
              recTitle: "Instant replay og optagelse", recOff: "Instant replay er slået fra. F10 optager, F12 tager et skærmbillede.",
              recReplayOn: "de sidste %1 s gemmes (F9 gemmer)", recLength: "Replay-længde", recRes: "Opløsning på optagelsen",
              recSave: "Gem replay (F9)", recStart: "Optag (F10)", recStop: "Stop (F10)", recShot: "Skærmbillede (F12)", recFolder: "Mappe",
              obsHelp: "I OBS: Kilder → + → Browser → sæt flueben i \"Lokal fil\" → vælg den gemte fil, og sæt bredde/højde til dit lærred (fx 1920 × 1080). Kun billedet sendes: tilføj lyden med programlyd-optagelse (Application Audio Capture → VeyAmux) eller direkte fra capture-kortet.",
              lsPerf: "Ydelsestilstand (performance mode)",
              fsrFgTitle: "Gratis (uden Lossless Scaling)", fsrFg: "AMD FSR 3.1 2× (gratis)",
              fsrFgHint: "Open source frame generation inde i VeyAmux, til capture-kort: fordobler billedfrekvensen (60 → 120 fps). Bevægelsen beregnes ud fra billederne, så kvaliteten er under Lossless Scaling, og det rigtige billede vises et halvt billede senere.",
              fgPaused: "Sat på pause automatisk: capture kører med ca. 120 Hz, så der laves ingen ekstra billeder. Starter igen under 80 fps.",
              lsPerfHint: "Lettere Lossless Scaling-model: hurtigere, lidt lavere kvalitet. Flow scale sættes automatisk efter din opløsning (nu %1 %).",
              lsFixed: "Fast 2× (fordobler)", lsFixedHint: "Fordobler altid billedfrekvensen (60 → 120 fps).",
              ls3x: "Fast 3× (tredobler)", ls3xHint: "Tredobler altid billedfrekvensen (60 → 180 fps, 40 → 120 fps). Kræver en skærm, der er hurtig nok til resultatet.",
              lsActive: "Aktiv via lsfg-vk. Lossless Scaling: %1", lsReady: "Lossless Scaling fundet: %1",
              lsRestart: "VeyAmux skal genstartes for at slå Lossless Scaling til.",
              lsNoInstall: "Lossless Scaling blev ikke fundet i dine Steam-biblioteker. Det skal købes og installeres via Steam.",
              lsNoDll: "Kun Lossless.dll blev fundet. lsfg-vk 2 kræver lsfg-vk.dll: åbn Lossless Scaling i Steam → Egenskaber → Betaer, og vælg lsfg-vk-grenen.",
              lsNoLayer: "lsfg-vk er ikke installeret. Installér det fra lsfg-vk.dev; VeyAmux bruger det til at køre Lossless Scaling på Linux.",
              lsOldLayer: "Der blev fundet en ældre lsfg-vk (1.x). VeyAmux kræver lsfg-vk 2.0 eller nyere.",
              lsNoVulkan: "Kræver Vulkan-visning; VeyAmux kører i software-tilstand.",
              lsCrashed: "VeyAmux lukkede uventet, sidste gang det startede med Lossless Scaling, så det er slået fra.",
              restartNow: "Genstart nu", tryAgain: "Prøv igen", chooseFolder: "Vælg mappe…", checkAgain: "Tjek igen" }
    })
    function t(k) { var s = strings[uiLanguage] || strings.en; return s[k] !== undefined ? s[k] : k }
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
        visible: !player.hasMedia
        anchors.centerIn: parent
        spacing: 18
        Image { anchors.horizontalCenter: parent.horizontalCenter; source: "icons/logo.svg"; sourceSize: Qt.size(112, 112); width: 112; height: 112; smooth: true }
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "VeyAmux"; color: Theme.heading; font.pixelSize: 54; font.weight: Font.DemiBold; font.letterSpacing: 2 }
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: t("drop"); color: Theme.label; font.pixelSize: 16 }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 12
            Rectangle {
                width: 170; height: 46; radius: 23; gradient: openMa.containsMouse ? Theme.sunsetHover : Theme.sunset
                Text { anchors.centerIn: parent; text: t("open"); color: Theme.accentInk; font.pixelSize: 15; font.weight: Font.DemiBold }
                MouseArea { id: openMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: player.openDialog() }
            }
            Rectangle {
                width: 160; height: 44; radius: 22; color: capMa.containsMouse ? Theme.surfaceHover : Theme.surface; border.color: Theme.border
                Text { anchors.centerIn: parent; text: t("capture"); color: Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold }
                MouseArea { id: capMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: capDlg.openDialog() }
            }
        }
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: t("hint"); color: Theme.muted; font.pixelSize: 14 }
    }

    // Click / hover / double-click on the video
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        enabled: player.hasMedia
        cursorShape: (win.controlsShown || !win.fs) ? Qt.ArrowCursor : Qt.BlankCursor
        onPositionChanged: wake()
        onClicked: { player.togglePlay(); wake() }
        onDoubleClicked: { player.togglePlay(); win.visibility = fs ? Window.Windowed : Window.FullScreen }
    }

    DropArea {
        anchors.fill: parent
        onDropped: (drop) => { if (drop.hasUrls) player.openUrl(drop.urls[0]) }
    }

    // ------------------------------------------------------------- status toast
    Rectangle {
        visible: player.status !== ""
        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: 18 }
        width: Math.min(parent.width - 40, toastText.implicitWidth + 32); height: 36; radius: 18; color: Theme.toast
        border.color: Theme.accentLine
        Text { id: toastText; anchors.centerIn: parent; text: player.status; color: Theme.warn; font.pixelSize: 13; elide: Text.ElideRight; width: parent.width - 28; horizontalAlignment: Text.AlignHCenter }
    }

    // short confirmations (profile loaded, clip or screenshot saved)
    Rectangle {
        visible: player.notice !== "" && player.status === ""
        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: 18 }
        width: Math.min(parent.width - 40, noticeText.implicitWidth + 32); height: 36; radius: 18; color: Theme.toast
        border.color: Theme.okLine
        Text { id: noticeText; anchors.centerIn: parent; text: player.notice; color: Theme.ok; font.pixelSize: 13; elide: Text.ElideRight; width: parent.width - 28; horizontalAlignment: Text.AlignHCenter }
    }

    // frame-time overlay (F3)
    Rectangle {
        id: ftOverlay
        visible: player.overlay
        z: 6
        anchors { left: parent.left; bottom: parent.bottom; leftMargin: 16; bottomMargin: 90 }
        width: 440; height: ftCol.implicitHeight + 16; radius: 10
        color: "#c80d0716"; border.color: Theme.border
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
                    ctx.strokeStyle = "#ff3d8e"; ctx.lineWidth = 1.5;
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
            Text { text: t("overlayHint"); color: Theme.dim; font.pixelSize: 11 }
        }
    }

    // recording indicator (stays small and out of the way; the picture itself is recorded without it)
    Row {
        visible: player.recording || player.streaming
        anchors { top: parent.top; right: parent.right; topMargin: 14; rightMargin: 16 }
        spacing: 6; z: 5
        Rectangle { width: 10; height: 10; radius: 5; color: "#ff4040"; anchors.verticalCenter: parent.verticalCenter
            SequentialAnimation on opacity { running: player.recording || player.streaming; loops: Animation.Infinite
                NumberAnimation { to: 0.3; duration: 700 } NumberAnimation { to: 1; duration: 700 } } }
        Text { text: player.streaming ? (player.recording ? "LIVE · REC" : "LIVE") : "REC"; color: "#ffdddd"; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 1 }
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
        Text { x: 22; y: 18; width: parent.width - 44; text: player.title; color: Theme.heading; font.pixelSize: 16; font.weight: Font.Medium; elide: Text.ElideRight }
        Text { x: 22; y: 40; text: player.mediaInfo; color: Theme.label; font.pixelSize: 12 }
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
            Text { id: tCur; anchors.verticalCenter: parent.verticalCenter; text: fmt(scrubbing ? scrubValue : player.position); color: Theme.text; font.pixelSize: 12; width: 52 }
            VSlider {
                anchors { left: tCur.right; right: tDur.left; verticalCenter: parent.verticalCenter; leftMargin: 6; rightMargin: 6 }
                from: 0; to: Math.max(player.duration, 0.001)
                value: scrubbing ? scrubValue : player.position
                onDraggingChanged: { if (dragging) { win.scrubbing = true } else { win.scrubbing = false; player.seek(win.scrubValue) } }
                onMoved: (v) => { win.scrubValue = v; wake() }
            }
            Text { id: tDur; anchors { right: parent.right; verticalCenter: parent.verticalCenter } text: fmt(player.duration); color: Theme.label; font.pixelSize: 12; width: 52; horizontalAlignment: Text.AlignRight }
        }

        Rectangle {
            visible: player.live
            anchors { left: parent.left; bottom: controlsRow.top; leftMargin: 22; bottomMargin: 10 }
            width: 58; height: 22; radius: 11; color: Theme.live
            Text { anchors.centerIn: parent; text: t("live"); color: Theme.heading; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 1 }
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

    // ------------------------------------------------------------- enhancement panel
    // Half the window wide, everything visible at once (no scrolling): the sections sit in three columns (two in narrow
    // windows), and if they still do not fit the height, the whole content is scaled down to fit.
    Rectangle {
        id: panel
        width: Math.max(560, parent.width / 2 - 18)
        // as tall as the (scaled) content needs, at most the window height
        height: Math.min(parent.height - 24, area.contentH * area.fit + 2 * area.anchors.margins)
        y: 12
        x: parent.width - width - 12            // always anchored; never animates on window resize (fullscreen)
        opacity: panelOpen ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 160 } }
        radius: 18
        color: Theme.panel
        border.color: Theme.border
        // sunset line along the top edge
        Rectangle { x: 18; width: parent.width - 36; height: 2; radius: 1; gradient: Theme.sunset; opacity: 0.85 }
        MouseArea { anchors.fill: parent; hoverEnabled: true; onPositionChanged: wake() }   // keep controls awake & eat clicks

        component Param: Column {
            id: p
            property string label
            property real from; property real to; property real value; property real neutral: NaN
            property int decimals: 2
            signal moved(real v)
            width: parent.width; spacing: 4; opacity: player.enhance ? 1 : 0.45
            Row {
                width: parent.width
                Text { text: p.label; color: Theme.label; font.pixelSize: 14; width: parent.width - 50 }
                Text { text: p.value.toFixed(p.decimals); color: Theme.text; font.pixelSize: 14; width: 50; horizontalAlignment: Text.AlignRight }
            }
            VSlider { width: parent.width; from: p.from; to: p.to; value: p.value; neutral: p.neutral; onMoved: (v) => p.moved(v) }
        }

        Item {
            id: area
            anchors { fill: parent; margins: 26 }
            // The menu is laid out at a fixed design width and then scaled as one piece to the panel (half the window),
            // so text, controls and spacing all grow and shrink together. Three columns, two in tall narrow windows.
            readonly property int cols: width / Math.max(1, win.height - 76) >= 0.75 ? 3 : 2
            readonly property real gap: 36
            readonly property real designW: cols === 3 ? 1120 : 780
            readonly property real colW: (designW - (cols - 1) * gap) / cols
            readonly property real contentW: designW
            readonly property real contentH: cols === 3 ? Math.max(secA.height, secB.height, secC.height)
                                                        : Math.max(secA.height + gap + secB.height, secC.height)
            readonly property real fit: Math.min(width / designW, (win.height - 24 - 2 * anchors.margins) / Math.max(1, contentH))

            Item {
                id: content
                width: area.contentW; height: area.contentH
                transformOrigin: Item.Top
                scale: area.fit
                x: (area.width - width) / 2    // centred; scaling keeps the top centre in place

                Column {
                    id: secA
                    x: 0; y: 0; width: area.colW
                    spacing: 26
                    Row {
                        width: parent.width
                        Text { text: t("enhance"); color: Theme.heading; font.pixelSize: 21; font.weight: Font.Bold; font.letterSpacing: 0.5; width: parent.width - 44; anchors.verticalCenter: parent.verticalCenter }
                        ToggleSwitch { checked: player.enhance; onToggled: (v) => player.enhance = v; anchors.verticalCenter: parent.verticalCenter }
                    }
                    Column {
                        width: parent.width; spacing: 10; opacity: player.enhance ? 1 : 0.45
                        Text { text: t("upscale"); color: Theme.label; font.pixelSize: 14 }
                        Segmented {
                            width: parent.width; enabled: player.enhance
                            model: [t("native"), "1.5×", "2×", "3×"]
                            current: { var i = scales.indexOf(Math.round(player.scale * 10) / 10); return i < 0 ? 0 : i }
                            onPicked: (i) => player.scale = scales[i]
                        }
                        Text { text: t("method"); color: Theme.label; font.pixelSize: 14; topPadding: 2 }
                        Segmented {
                            width: parent.width; enabled: player.enhance
                            // shown as Lanczos | FSR 1 | FSR 3.1; stored values 0 / 2 / 1
                            readonly property var values: [0, 2, 1]
                            model: [t("lanczos"), "AMD FSR 1", t("fsr31")]
                            current: Math.max(0, values.indexOf(player.upscaler))
                            onPicked: (i) => player.upscaler = values[i]
                        }
                        Text {
                            width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 13
                            color: player.upscalerError !== "" ? Theme.warn : Theme.muted
                            text: player.upscalerError !== "" ? t("fsrFallback") + player.upscalerError
                                  : (player.upscaler === 1 ? t("fsr31Hint") + (player.scale <= 1.001 ? " " + t("fsrNeedsScale") : "")
                                     : player.upscaler === 2 ? t("fsr1Hint") + (player.scale <= 1.001 ? " " + t("fsrNeedsScale") : "")
                                                             : t("lanczosHint"))
                        }
                    }
                    Column {
                        width: parent.width; spacing: 10; opacity: player.enhance ? 1 : 0.45
                        Text { text: t("denoise"); color: Theme.label; font.pixelSize: 14 }
                        Segmented {
                            width: parent.width; model: t("denoiseModes"); enabled: player.enhance
                            current: player.denoise; onPicked: (i) => player.denoise = i
                        }
                        Segmented {
                            width: parent.width; model: t("denoiseLevels"); enabled: player.enhance && player.denoise > 0
                            current: player.denoiseStrength; onPicked: (i) => player.denoiseStrength = i
                        }
                        Text {
                            width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 13; color: Theme.muted
                            text: t("denoiseHint" + player.denoise)
                        }
                        Text { text: t("hdr"); color: Theme.label; font.pixelSize: 14; topPadding: 6 }
                        Segmented {
                            width: parent.width; model: ["600", "1000", "2000", "4000 nits"]
                            readonly property var peaks: [600, 1000, 2000, 4000]
                            current: Math.max(0, peaks.indexOf(player.hdrPeak))
                            onPicked: (i) => player.hdrPeak = peaks[i]
                        }
                        Text { width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 13; color: Theme.muted; text: t("hdrHint") }
                        Item {
                            width: parent.width; height: 22
                            Text { text: t("hdrOut"); color: Theme.label; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
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
                            model: [player.displayPeakEdid > 0 ? "Auto (" + player.displayPeakEdid + ")" : "Auto", "400", "600", "1000", "1500 nits"]
                            readonly property var peaks: [0, 400, 600, 1000, 1500]
                            current: Math.max(0, peaks.indexOf(player.displayPeak))
                            onPicked: (i) => player.displayPeak = peaks[i]
                        }
                        Text {
                            visible: player.hdrOutput
                            width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 13
                            color: player.hdrOutputActive ? Theme.ok : Theme.warn
                            text: player.hdrOutputRestart ? t("hdrOutRestart") : player.hdrOutputActive ? t("hdrOutOn")
                                  : player.hdrOutputNoVulkan ? t("hdrOutSw") : player.hdrOutputXwayland ? t("hdrOutX11") : t("hdrOutNo")
                        }
                        Item {
                            visible: player.gamescopeAvailable || player.inGamescope
                            width: parent.width; height: 26
                            Text { text: t("gamescope"); color: Theme.label; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                            ToggleSwitch { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                checked: player.gamescope; onToggled: (v) => player.gamescope = v }
                        }
                        Text {
                            visible: player.gamescopeAvailable || player.inGamescope
                            width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 13
                            color: player.inGamescope ? Theme.accent3 : Theme.muted
                            text: player.inGamescope ? t("gamescopeOn") : t("gamescopeHint")
                        }
                        Rectangle {
                            visible: player.hdrOutputRestart || player.gamescopeRestart
                            width: hdrRs.implicitWidth + 22; height: 26; radius: 8; color: hdrRsMa.containsMouse ? Theme.accentHover : Theme.accent
                            Text { id: hdrRs; anchors.centerIn: parent; text: t("restartNow"); color: Theme.accentInk; font.pixelSize: 14; font.weight: Font.DemiBold }
                            MouseArea { id: hdrRsMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: player.restartApp() }
                        }
                    }
                    Column {
                        width: parent.width; spacing: 10; opacity: player.enhance ? 1 : 0.45
                        Row {
                            width: parent.width
                            Text { text: t("aa") + " (FXAA)"; color: Theme.label; font.pixelSize: 14; width: parent.width - 44; anchors.verticalCenter: parent.verticalCenter }
                            ToggleSwitch { checked: player.fxaa; enabled: player.enhance; onToggled: (v) => player.fxaa = v; anchors.verticalCenter: parent.verticalCenter }
                        }
                        Segmented {
                            width: parent.width; model: t("aaq"); enabled: player.enhance && player.fxaa
                            current: player.fxaaQuality; onPicked: (i) => player.fxaaQuality = i
                        }
                    }
                }
                Column {
                    id: secB
                    x: area.cols === 3 ? area.colW + area.gap : 0
                    y: area.cols === 3 ? 0 : secA.height + area.gap
                    width: area.colW
                    spacing: 26
                    Param { label: t("sharpen"); from: 0; to: 1; value: player.sharpen; onMoved: (v) => player.sharpen = v }
                    Param { label: t("exposure"); from: -2; to: 2; value: player.exposure; neutral: 0; onMoved: (v) => player.exposure = v }
                    Param { label: t("contrast"); from: 0.7; to: 1.4; value: player.contrast; neutral: 1; onMoved: (v) => player.contrast = v }
                    Param { label: t("saturation"); from: 0; to: 2; value: player.saturation; neutral: 1; onMoved: (v) => player.saturation = v }
                    Param {
                        label: t("audioDelay"); from: -300; to: 300; decimals: 0; neutral: 0; opacity: 1
                        value: player.audioDelay; onMoved: (v) => player.audioDelay = Math.round(v / 10) * 10
                    }
                    Text { width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 13; color: Theme.muted; text: t("audioDelayHint") }
                    Item {
                        width: parent.width; height: 26
                        Text { text: t("normalize"); color: Theme.label; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                        ToggleSwitch { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                            checked: player.normalize; onToggled: (v) => player.normalize = v }
                    }
                    // output device: the system default plus the sinks pactl reports (refreshed when the menu opens)
                    Column {
                        id: sinkCol
                        width: parent.width; spacing: 4
                        property var sinks: []
                        function refresh() { sinks = player.audioSinks() }
                        Component.onCompleted: refresh()
                        Connections { target: win; function onPanelOpenChanged() { if (win.panelOpen) sinkCol.refresh() } }
                        Text { text: t("audioOut"); color: Theme.label; font.pixelSize: 14 }
                        Repeater {
                            model: [{ name: "", desc: t("audioOutDefault") }].concat(sinkCol.sinks.slice(0, 5))
                            delegate: Rectangle {
                                required property var modelData
                                readonly property bool sel: player.audioOutput === modelData.name
                                width: sinkCol.width; height: 28; radius: 8
                                color: sinkMa.containsMouse ? Theme.surfaceHover : Theme.surface
                                gradient: sel ? Theme.sunset : null
                                Text { anchors { left: parent.left; right: parent.right; leftMargin: 10; rightMargin: 10; verticalCenter: parent.verticalCenter }
                                    text: modelData.desc; elide: Text.ElideRight; font.pixelSize: 13
                                    color: parent.sel ? Theme.accentInk : Theme.text }
                                MouseArea { id: sinkMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: player.audioOutput = modelData.name }
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width; height: 34; radius: 10; color: rstMa.containsMouse ? Theme.surfaceHover : Theme.surface
                        Text { anchors.centerIn: parent; text: t("reset"); color: Theme.text; font.pixelSize: 15 }
                        MouseArea { id: rstMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: player.resetEnhance() }
                    }

                    // ---- profiles
                    Column {
                        width: parent.width; spacing: 6
                        Text { text: t("profiles"); color: Theme.label; font.pixelSize: 14 }
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
                                    height: 28; radius: 8; width: chipRow.implicitWidth + 16
                                    color: active ? Theme.accent : (chipMa.containsMouse ? Theme.surfaceHover : Theme.surface)
                                    Row {
                                        id: chipRow; anchors.centerIn: parent; spacing: 6
                                        Text { text: (index < 9 ? (index + 1) + "  " : "") + modelData + (bound !== "" ? "  ⚡" : ""); color: active ? Theme.accentInk : Theme.text; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                                        Text {
                                            text: "×"; color: active ? Theme.accentInk : Theme.muted; font.pixelSize: 16; anchors.verticalCenter: parent.verticalCenter
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
                                width: parent.width - saveBtn.width - 6; height: 30; radius: 8; color: Theme.surface
                                border.color: nameIn.activeFocus ? Theme.accent : Theme.surfaceHover
                                TextInput {
                                    id: nameIn; anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                                    verticalAlignment: TextInput.AlignVCenter; color: Theme.text; font.pixelSize: 14; maximumLength: 24
                                    clip: true; selectByMouse: true
                                    Text { anchors.verticalCenter: parent.verticalCenter; visible: !parent.text && !parent.activeFocus; text: t("profileName"); color: Theme.dim; font.pixelSize: 14 }
                                    onAccepted: saveBtn.save()
                                    Keys.onEscapePressed: { text = ""; keyRoot.forceActiveFocus() }
                                }
                            }
                            Rectangle {
                                id: saveBtn
                                function save() { if (nameIn.text.trim() !== "") { player.saveProfile(nameIn.text, bindSig.checked); nameIn.text = ""; keyRoot.forceActiveFocus() } }
                                width: saveTxt.implicitWidth + 24; height: 30; radius: 8
                                color: nameIn.text.trim() !== "" ? (saveMa.containsMouse ? Theme.accentHover : Theme.accent) : Theme.surfaceHover
                                Text { id: saveTxt; anchors.centerIn: parent; text: t("saveProfile"); color: nameIn.text.trim() !== "" ? Theme.accentInk : Theme.muted; font.pixelSize: 14; font.weight: Font.Medium }
                                MouseArea { id: saveMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: saveBtn.save() }
                            }
                        }
                        Row {
                            id: bindSig
                            property bool checked: false
                            visible: player.signalKey !== ""
                            spacing: 8
                            Rectangle {
                                width: 16; height: 16; radius: 4; anchors.verticalCenter: parent.verticalCenter
                                color: bindSig.checked ? Theme.accent : "transparent"; border.color: bindSig.checked ? Theme.accent : Theme.dim
                                Text { anchors.centerIn: parent; text: "✓"; visible: bindSig.checked; color: Theme.accentInk; font.pixelSize: 13 }
                            }
                            Text { text: t("bindSignal").replace("%1", player.signalKey); color: Theme.text; font.pixelSize: 13; width: secB.width - 30; wrapMode: Text.Wrap }
                            TapHandler { onTapped: bindSig.checked = !bindSig.checked }
                        }
                        Text { width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 13; color: Theme.muted; text: t("profilesHint") }
                    }
                    Column {
                        width: parent.width; spacing: 3
                        Text { width: parent.width; text: t("gpu") + ": " + (player.gpuName !== "" ? player.gpuName : t("cpuOnly")); color: Theme.muted; font.pixelSize: 13; wrapMode: Text.Wrap }
                        Text { width: parent.width; text: player.perf; color: Theme.label; font.pixelSize: 14; wrapMode: Text.Wrap }
                    }
                    Rectangle { width: parent.width; height: 1; color: Theme.line }
                    ObsSection { width: parent.width; tr: win.t }
                    Rectangle { width: parent.width; height: 1; color: Theme.line }
                    Column {
                        width: parent.width; spacing: 8
                        Text { text: "Steam"; color: Theme.heading; font.pixelSize: 18; font.weight: Font.Bold; font.letterSpacing: 0.5 }
                        Text { width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 13; color: Theme.muted; text: t("steamHint") }
                        Rectangle {
                            width: steamTxt.implicitWidth + 24; height: 32; radius: 10
                            color: steamMa.containsMouse ? Theme.surfaceHover : Theme.surface
                            Text { id: steamTxt; anchors.centerIn: parent; text: t("steamAdd"); color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                            MouseArea { id: steamMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: player.addToSteam() }
                        }
                        Item {
                            width: parent.width; height: 26
                            Text { text: t("autostart"); color: Theme.label; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 60; elide: Text.ElideRight }
                            ToggleSwitch { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                checked: player.autostart; onToggled: (v) => player.autostart = v }
                        }
                        Text { width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 13; color: Theme.muted; text: t("autostartHint") }
                    }
                }
                Column {
                    id: secC
                    x: area.cols === 3 ? 2 * (area.colW + area.gap) : area.colW + area.gap
                    y: 0; width: area.colW
                    spacing: 26
                    FrameGenSection { width: parent.width; tr: win.t }
                    Rectangle { width: parent.width; height: 1; color: Theme.line }
                    RecordSection { width: parent.width; tr: win.t }
                }
            }
        }
    }

    CaptureDialog { id: capDlg; anchors.fill: parent; z: 100; tr: win.t }

    // ------------------------------------------------------------- keyboard
    Item {
        id: keyRoot
        anchors.fill: parent
        focus: true
        Keys.onPressed: (e) => {
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
