# VeyAmux for Linux – teknisk dokumentation

Kort introduktion og installation: se [README](../README.md).

VeyAmux er en native Linux-version bygget på **Veyra-NRVideo 2.1.5** (Windows, GPL-3.0, https://github.com/Likely7/Veyra-NRVideo):
**Qt 6/QML UI + FFmpeg + Vulkan compute**. Genbruger Veyras originale HLSL-shaders
(`shaders/veyra/*.hlsl`) ved at kompilere dem til SPIR-V med Microsofts DXC – ingen omskrivning af shaderne.

## Byg AppImage
```
sudo apt install cmake ninja-build g++ pkg-config patchelf file python3 glslang-tools qt6-base-dev qt6-declarative-dev \
  qt6-svg-dev qt6-wayland libvulkan-dev libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev \
  libpulse-dev libva-dev qml6-module-qtquick qml6-module-qtquick-window qml6-module-qtquick-layouts
./scripts/build-appimage.sh          # -> dist/VeyAmux-x86_64.AppImage
```
`scripts/fetch-tools.sh` henter DXC, linuxdeploy og appimagetool automatisk. Kør AppImage uden FUSE: `APPIMAGE_EXTRACT_AND_RUN=1`.

## Hvad er med
| Område | Status |
|---|---|
| Afspilning (FFmpeg), seek, pause, volumen, drag&drop, fuldskærm | ✅ |
| Hardware-dekodning på AMD/Intel (VAAPI), automatisk fallback til software | ✅ (ikke testet på rigtig GPU endnu) |
| Lyd via PulseAudio/PipeWire (lyd er master-clock) | ✅ |
| GPU-kæde: RgbToLinear → [støjfjerning: temporal/NLM] → FXAA (AaLuma+AaFxaa, originale shaders) → Lanczos-3, **AMD FSR 1, NVIDIA NIS eller AMD FSR 3.1** opskalering → sharpen/eksponering/kontrast/mætning → ScaleBlit (sRGB) eller LxPqOut (PQ, HDR-output) | ✅ |
| **AMD FSR 3.1-opskalering** (eksperimentel): FidelityFX SDK 1.1.4 FSR 3.1 upscaler på Vulkan med VeyAmux' egen sub-pixel-bevægelsesmåling | ✅ (blødere end Lanczos i test, se nedenfor) |
| **Lossless Scaling frame generation** (Fast 2× / Fast 3×) via brugerens egen lsfg-vk + Lossless Scaling fra Steam | ✅ testet af brugeren på RX 9070 XT / Bazzite |
| Vulkan-device vælges automatisk (diskret GPU først); `VEYAMUX_VK_DEVICE=<n>` tvinger valg | ✅ |
| Capture-kort via V4L2 (UVC): YUYV/UYVY/NV12/I420/RGB/MJPEG, valg af opløsning/frekvens, lavlatens-visning, lyd fra PulseAudio/PipeWire-kilde | ✅ (testet uden hardware: konvertering verificeret, enheden selv ikke) |
| DLSS/NGX, NVOF, NVIDIA VFG-DLL, Reflex, NVAPI, AMD HIP-NR (RDNA3) | ❌ Windows/NVIDIA-specifikke |
| **HDR-capture (P010)** → SDR: BT.2020/PQ → BT.2390-tonemapping med Veyras egne `HdrColor.hlsli`/`HdrToSdr.hlsli` (`shaders/LxP010.hlsl`) | ✅ (testet med syntetisk P010-signal, ikke med kortet) |
| **VRR-tilstand**: spillets egne billeder i jævn takt + vsync | ✅ (algoritmen simuleret, ikke testet på VRR-skærm) |
| HDR-videofiler (tonemapping ved filafspilning), HDR-output til skærmen, PS5/Xbox/Moonlight-streaming, undertekster | ❌ ikke portet endnu |

## Hvad spillet faktisk kører (capture)
Et capture-kort sender altid sit faste signal (fx 3840×2160 @ 60), uanset hvad konsollen/pc'en renderer. Infolinjen
viser derfor først det målte indhold og signalet bagefter, fx `≈1920×1080 @ 30 fps → 60 fps (Lossless Scaling 2×) · signal 3840×2160 NV12 @ 60.00 fps`
(`src/ContentProbe.cpp`):
- **Billedfrekvens (målt):** billeder, der faktisk er nye, per sekund. Alle billeder fra kortet sammenlignes på et glidende
  gitter (også dem, der springes over for lav forsinkelse); kortets egen støj måles og trækkes fra. Står billedet helt
  stille (pause, menu), vises den sidste værdi med "(stillestående billede)".
- **Opløsning (skøn, "≈"):** et opskaleret billede har et spejlbillede af sit frekvensspektrum omkring den oprindelige
  opløsnings grænsefrekvens, uanset skaleringsmetode. Én gang i sekundet analyseres 48 linjer (FFT) på en baggrundstråd og
  medianen af de sidste 5 vises. Testet rigtigt på 720p/900p/1080p/1440p/dynamisk 1728 → 4K (bilineær, bikubisk, Lanczos,
  med skarphed, med HUD) og 720p → 1080p. Begrænsninger: spil med temporal opskalering (FSR 2/3, DLSS, TAAU,
  checkerboard) eller kornfilter efter opskaleringen genskaber fine detaljer og læses derfor som højere opløsning; meget
  flade scener giver intet skøn. `veyamux-probe-test` i build-mappen tester begge målinger på rå luma-filer.

## Støjfjerning
Menu → *Forbedring* → *Støjfjerning*: Fra / Rumlig / Temporal / Begge og Let / Middel / Kraftig. Kører først i kæden
(på kildebilledet før FXAA, opskalering og skarphed), så *Skarphed* kan skrues op uden at gøre støjen skarp.
- **Rumlig** (`shaders/LxNlm.hlsl`): non-local means – hver pixel blandes med pixels i et 7×7-område, hvis 3×3-omgivelser
  og farve ligner den. Billede for billede, ingen spøgelser ved bevægelse.
- **Temporal** (`shaders/LxTemporalNr.hlsl`): rekursivt gennemsnit over flere billeder langs bevægelsen fra VeyAmux'
  egen bevægelsesmåling (`MotionEstimator`), Catmull-Rom-sampling, historikken klemmes til det aktuelle 3×3-områdes
  værdier og vægtes ned, hvor den afviger mere end støjen kan forklare.
- **Begge**: temporal, derefter en mildere rumlig.
Målt på 960×540 med tilføjet støj (σ = 7/255, PSNR mod det rene billede; "detalje" = hvor meget af den ægte fine
detalje der er tilbage, 1,00 = alt): støjfyldt 31,2–31,3 dB. Kraftig: Rumlig 37,1–37,4 dB, Temporal 35,0–38,1 dB,
Begge 36,9–40,9 dB, alle med detalje 0,97–1,02. En rød boks, der flytter 40 px/billede over grå baggrund, gav ingen
spor eller glorier (ingen pixel over 30/255 fra det rene billede). Hastighed på et rigtigt grafikkort er ikke målt her.

## Bevægelsesmåling (FSR-opskalering)
Video har ingen bevægelsesvektorer fra en spilmotor, så VeyAmux måler dem selv til FSR 3.1-opskaleringen og den temporale støjfjerning
(`src/MotionEstimator.cpp`): luma-pyramide med op til 7 niveauer, regulariseret Lucas-Kanade fra groft til fint,
udfyldning af flade områder og til sidst skarpe bevægelseskanter (`LxMvSelect`). Over 2560 px bredde måles i halv opløsning.

## Gentagne billeder (30 fps-spil i 60 Hz-signal)
Når frame generation (Lossless Scaling) er slået til, sorteres capture-billeder, der er en gentagelse af det
forrige, fra *før* behandlingen (`FrameDiff` i `src/ContentProbe.cpp`; kortstøj måles og trækkes fra, og en gentagelse
sammenlignes med det sidst viste billede, så langsomme overtoninger stadig kommer igennem). Frame generation laver så
mellembilleder mellem to forskellige billeder: et 30 fps-spil bliver til jævne 60 fps i stedet for 60 fps med dobbelte
billeder.

Lossless Scaling: lsfg-vk skriver sin log (niveau debug) til `~/.config/VeyAmux/lsfg-vk.log`; VeyAmux viser i menuen
(og i terminalen), om laget blev indlæst, og hvilken fejl lsfg-vk eventuelt melder. *Ydelsestilstand* slår lsfg-vk's
performance mode til; flow scale vælges automatisk efter vinduets opløsning (4K 50 %, 1440p 75 %, ellers 100 %, som
Lossless Scaling anbefaler; valgt efter skærmen, så den ikke skifter – hvert skift får lsfg-vk til at bygge alt forfra).
Begge dele genindlæses af lsfg-vk, mens den kører. lsfg-vk 2 har ingen frame pacing (eneste tilstand er "vsync":
billeder vises, så snart de er klar), så resultatet er kun jævnt, når skærmens Hz er et helt multiplum af den nye
billedfrekvens (fx 60 fps × 2 = 120 fps: sæt skærmen til 120 Hz).

## OBS (browserkilde)
Menu → *OBS (browserkilde)*: slå til, vælg opløsning (720p/1080p/1440p/original) og tryk **Gem browserfil…** for at
gemme en HTML-fil, hvor du vil. I OBS: *Kilder → + → Browser → Lokal fil →* vælg filen, og sæt bredde/højde til dit lærred.
VeyAmux kører en lille HTTP-server, der kun lytter på `127.0.0.1:47920` (indstillingen `obs/port`), og sender det færdige
billede som MJPEG – efter alle forbedringer, i samme tempo som på skærmen (Lossless Scaling's mellembilleder laves
først bagefter i grafikdriverens lag og kommer ikke med i OBS) (`src/ObsStream.cpp`). I den direkte capture-vej skaleres billedet ned og kopieres fra GPU'en (`LxDownscale`,
afsluttet via en Vulkan-event, så det ikke venter på næste skærmopdatering); det sker kun, mens OBS faktisk er tilsluttet.
Siden er gennemsigtig, mens VeyAmux er lukket, og forbinder selv igen. Kun billedet sendes: lyd tages med OBS'
*Application Audio Capture* (VeyAmux) eller direkte fra capture-kortet. Adressen `http://127.0.0.1:47920/` virker også
som URL-browserkilde, og `/snapshot.jpg` giver det seneste billede.

## Opskalering
Menu → *Forbedring* → *Opskalering* (1,5× / 2× / 3×) og *Metode*:
- **Lanczos** (standard) – skarp klassisk skalering billede for billede, ingen ekstra forsinkelse.
- **AMD FSR 1** – EASU (kantadaptiv opskalering) + RCAS (skarphed, 0,5 stop; `VEYAMUX_FSR1_RCAS=<stop>`) fra FidelityFX
  SDK'ens `ffx_fsr1.h`, billede for billede (`shaders/LxFsr1.hlsl`), i gamma-2-domænet. Målt 2×: fine detaljer 32,2 dB /
  detaljegrad 0,93 (Lanczos 32,0 dB / 0,61), naturligt motiv 49,0 / 49,5 dB, vektorgrafik 35,6 / 37,3 dB – synligt
  skarpere uden ringning, men lidt lavere PSNR på skarpe kanter.
- **NVIDIA Image Scaling (NIS)** – NVScaler fra NVIDIAs NIS SDK (MIT, `third_party/nis`), `shaders/LxNis.hlsl`:
  6-taps retningsbestemt skalering med adaptiv skarphed, blok 32×24, 256 tråde, `NIS_HDR_MODE` lineær (kæden kører i
  lineært lys, så også HDR-signaler skaleres korrekt). Koefficient-teksturerne (2×64 RGBA32F) uploades én gang.
  Skarphed 0,4 som standard (`VEYAMUX_NIS_SHARP=0..1`). Virker på alle grafikkort (ren compute), ingen ekstra forsinkelse.
- **AMD FSR 3.1** (eksperimentel) – AMD FidelityFX SDK 1.1.4 (open source). FSR 3.1 er en *temporal* opskalerer: den samler
  detaljer over flere billeder og skal vide, hvordan hver pixel har bevæget sig, ned til brøkdele af en pixel. Spil giver
  den præcise bevægelsesvektorer og rykker kameraet en brøkdel af en pixel hvert billede (jitter); video har ingen af delene.
  VeyAmux måler derfor bevægelsen selv (luma-pyramide + regulariseret Lucas-Kanade fra groft til fint, med udfyldning af flade
  områder; `shaders/LxLuma.hlsl`, `LxLkLevel.hlsl`, `LxMvFill.hlsl`) og kører AMD's RCAS-skarphed bagefter.
  FidelityFX optical flow (som frame generation bruger) blev forsøgt først, men giver kun hele pixels og ingen bevægelse de
  første 6 billeder efter hver nulstilling.
  **Målt resultat** (640×360 → 1280×720, PSNR mod de ægte 1280×720-billeder, panorering med skiftende sub-pixel-forskydning,
  software-GPU): naturligt motiv Lanczos 37,3 dB / FSR 3.1 30,0 dB (33,5 dB selv med perfekt kendt bevægelse); blødt motiv
  36,4 / 32,8 dB; meget detaljeret motiv 26,6 / 25,6 dB (27,2 dB med perfekt bevægelse – det eneste tilfælde, hvor FSR slår
  Lanczos). Konklusion: på video er FSR 3.1 blødere end Lanczos; derfor er Lanczos standard. FSR 4.1.2 findes kun som lukkede
  Windows-DLL'er og kan ikke bruges i et Linux-program.

## Frame generation
Menu → *Frame generation*:
- **Lossless Scaling – Fast 2× (fordobler, standard) / Fast 3× (tredobler)** – kræver at du har købt Lossless Scaling på Steam og har installeret
  [lsfg-vk](https://lsfg-vk.dev) 2.0+. VeyAmux finder selv Lossless Scaling i alle dine Steam-biblioteker ud fra `$HOME`
  (fx `/var/home/<bruger>/.local/share/Steam/steamapps/common/Lossless Scaling/` på Bazzite, også Flatpak-Steam og ekstra
  biblioteker fra `libraryfolders.vdf`), skriver sin egen profil til `~/.config/VeyAmux/lsfg-vk.toml` og starter lsfg-vk for
  sit eget vindue via `LSFGVK_CONFIG`/`LSFGVK_PROFILE`. Din egen lsfg-vk-konfiguration røres ikke.
  - lsfg-vk 2 bruger `lsfg-vk.dll` (fra Lossless Scalings *lsfg-vk*-gren i Steam, fanen Betaer), ikke `Lossless.dll`.
  - **Fast 3×**: tre gange billedfrekvensen (60 → 180 fps). Kræver en skærm med mindst den frekvens.
  - Til/fra-skift kræver genstart (knap i menuen), fordi laget indlæses ved opstart. Crasher VeyAmux under opstart med
    lsfg-vk, slås Lossless Scaling automatisk fra, indtil du trykker *Prøv igen*.
  - VeyAmux indeholder/kopierer ingen lsfg-vk- eller Lossless Scaling-kode (lsfg-vk er CC BY-NC-ND 4.0) og bundler dem ikke.

## HDR-capture (P010)
Live Gamer Ultra 2.1 (og andre kort) kan levere **P010**: 10-bit Y'CbCr 4:2:0 med HDR (BT.2020-farver, PQ/SMPTE ST 2084).
Linux' uvcvideo kender formatet; i VeyAmux' capture-dialog hedder tilstandene "P010 (HDR)". P010 vælges aldrig
automatisk (dobbelt USB-båndbredde; 4K60 P010 er ca. 12 Gbit/s og passer ikke i kortets USB 3.2 Gen 2 – vælg fx 4K30
eller 1440p60, alt efter hvad kortet tilbyder). De 16-bit planer uploades som R16/RG16 og `shaders/LxP010.hlsl` regner
direkte til VeyAmux' lineære arbejdsbillede (ingen 8-bit-mellemtrin): BT.2020 NCL → PQ-afkodning → BT.2390 EETF
(Hermite-skulder i PQ-domænet) og nuance-bevarende gamut-komprimering til BT.709, SDR-hvid = 203 nits (BT.2408) – Veyras
egne HDR-funktioner. *HDR-capture: kildens maks. lysstyrke* (600/1000/2000/4000 nits, standard 1000) skal passe til
spillets/konsollens HDR-kalibrering. Testet med et syntetisk P010-signal (`VEYAMUX_TEST_LIVE=p010`: PQ-gråskala
0–4000 nits, 203-nits farvefelter i BT.2020) i den direkte capture-vej uden Vulkan-valideringsfejl; ikke testet med selve
kortet.

**HDR-videofiler** (HDR10/PQ og HLG, BT.2020): swscale leverer RGBA64 (16 bit pr. kanal) og `shaders/LxHdrRgb.hlsl` laver
samme tonemapping som capture-vejen (HLG: invers OETF + OOTF med γ = 1,2 ved 1000 nits).

## HDR-output (eksperimentel)
Menu → *HDR-output* (kræver genstart). VeyAmux beder Qt Quick om en **HDR10-swapchain** (standard: `QSG_RHI_HDR=hdr10`,
A2B10G10R10 + `VK_COLOR_SPACE_HDR10_ST2084_EXT`) eller en **scRGB-swapchain** (`scrgb`, FP16 +
`VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT`), med instans-udvidelsen `VK_EXT_swapchain_colorspace`
(`VEYAMUX_HDR_MODE=hdr10|scrgb`). Efter første billede
spørger VeyAmux selv overfladen om formatet (samme test som Qt) og slår kun HDR-vejen til, hvis det findes.
- HDR-kilder tonemappes til *skærmens top-lysstyrke* (400/600/1000/1500 nits) i stedet for 203 nits; SDR-kilder vises med
  hvid = 203 nits (BT.2408).
- Det færdige billede (`out8_`) gemmes PQ-kodet (8 bit med trekantet dither, `shaders/LxPqOut.hlsl`). I QML afkoder et
  `layer.effect` på video-elementet PQ → udvidet sRGB-kodning (`shaders/qt/pq_video.frag`), og hele vinduet tegnes i et
  FP16-lag, der omregnes til lineært scRGB (1,0 = 80 nits, SDR-hvid 203 nits) (`shaders/qt/hdr_window.frag`), så menuen ser
  normal ud.
- **Fuld BT.2020-farve**: HDR-kilder tonemappes kun i luminans (BT.2390 til skærmens top) og bliver i BT.2020
  (`HdrToDisplay2020` i `shaders/PqCode.hlsli`); hele kæden arbejder så i lineært BT.2020. SDR-kilder løftes fra
  BT.709 til BT.2020 i `LxPqOut`. `out8_` er altid PQ-kodet BT.2020 i HDR-tilstand. Video-laget dekoder til BT.709
  med fortegn (brede farver = negative komponenter), vindueslaget koder til PQ/BT.2020 (HDR10) eller lineært scRGB.
- OBS, optagelse og skærmbilleder omregnes tilbage til SDR (`PqCodedToSdr`: BT.2020 → BT.709, blød skulder på
  luminansen over 0,75, Veyras `ToneGamut`; CPU-versionen i `PlayerBridge::sdrCopy`). Målt mod SDR-vejen: højst ±15 i
  8-bit-værdi på mættede farvefelter.
- Kræver native Wayland med HDR slået til (KDE Plasma 6 / GNOME 48+, Mesa med Wayland-farvestyring). Gennem XWayland
  findes ingen HDR-swapchain; menuen siger det. Ukendt sammenspil med lsfg-vk (frame generation) på en HDR-swapchain.
- Testet her kun i en software-GPU med en SDR-swapchain (`VEYAMUX_FORCE_HDR_OUT=1`): kæden, lagene og SDR-kopierne virker,
  ingen Vulkan-valideringsfejl. Ikke testet på en ægte HDR-skærm.

## gamescope og Steam
- *Start i gamescope* (kun med `VEYAMUX_GAMESCOPE=1`, eksperimentel – på Bazzite afviste gamescopes WSI-lag
  VeyAmux' swapchain, og OK i fejldialogen kunne fryse skrivebordet; HDR og VRR fås i stedet direkte på Wayland): VeyAmux genstarter sig selv som
  `gamescope -f -W <skærm> -H <skærm> -r <Hz> --adaptive-sync [--hdr-enabled] -- VeyAmux` med `QT_QPA_PLATFORM=xcb`,
  uden `WAYLAND_DISPLAY` og med `ENABLE_GAMESCOPE_WSI=1`. Ikke inde i gamescope i forvejen (Game Mode).
  `VEYAMUX_GAMESCOPE=0` springer det over. Andre swapchain-lag ødelægger gamescopes WSI-lag ("Creating swapchain for
  non-Gamescope swapchain", kan fryse skrivebordet): inde i gamescope fjernes `ENABLE_HDR_WSI`, og lsfg-vk slås fra
  (`DISABLE_LSFGVK=1`). Crash-vagt: kommer gamescope-starten ikke op (mærket `ui/gamescopeLaunching` ryddes efter 5 s),
  slås gamescope-start fra ved næste start.
- *Tilføj til Steam*: skriver en non-Steam-genvej i `userdata/<konto>/config/shortcuts.vdf` (binær KeyValues; eksisterende
  genveje bevares, en tidligere VeyAmux-genvej opdateres, første gang gemmes `shortcuts.vdf.veyamux-backup`). Steam skal
  være lukket. Startindstillinger: `--fullscreen --capture last`. Inde i gamescope starter VeyAmux altid i fuldskærm.

## Signalskift og 120 Hz
- Fejler capture-kortet (frakoblet), forsøger VeyAmux at åbne det igen hvert sekund med samme tilstand. Kommer der intet
  billede i 3 s, genstartes streamen (hvert 3. sekund) – mange UVC-kort går i stå, når konsollen skifter
  opløsning/Hz/HDR.
- Kører capture med ≥ 95 fps (tilstandens fps eller målt indhold) i 1 s, sættes frame generation på pause (lsfg-vk-
  multiplikator 1, indstillingen bevares); under 80 fps i 2 s starter den igen.

## Lyd
Capture-lyden går igennem i blokke på 5 ms (før 20 ms) med ca. 20 ms kø ud (før 50 ms); pipewire-pulse sænker sin
kvantum tilsvarende. `VEYAMUX_AUDIO_MS=<ms>` (2–40) giver større blokke på travle systemer.

## Gratis frame generation (AMD FSR 3.1)
Tilstand 5 (`enh/frameGenMode=5`, vises når Lossless Scaling ikke er fundet): `src/FsrFrameGen.cpp` – FidelityFX optical
flow + frame interpolation (SDK 1.1.4, MIT) på det færdige billede, med VeyAmux' egen bevægelsesestimering som
"spil-bevægelsesvektorer", konstant uendelig dybde, beskyttelse af statiske elementer (`LxStatic`) og skarphed i fine
detaljer (`LxDetail`). Kun capture-vejen: det genererede billede kopieres til et visningsbillede med det samme, det
rigtige et halvt capture-interval senere (`EnhanceChain::recordShowReal`, `QTimer` → `update()`). Testet: et objekt der
flytter 8 px pr. billede placeres præcist 4 px i mellembilledet. 120 Hz-reglen gælder også her.
(Byggefejl rettet: komponenterne skal defineres som `FFX_FI`/`FFX_OF`, ellers er shader-blobbene tomme.)

## Livestream (RTMP)
`Recorder::startStream`: H.264 (VA-API, ellers libx264) med konstant bitrate, FLV over RTMP/RTMPS via en
afsendertråd; er den mere end ~5 s bagud, droppes køen og der fortsættes fra næste keyframe. `rw_timeout` 5 s.
Twitch `rtmp://live.twitch.tv/app/<nøgle>`, YouTube `rtmp://a.rtmp.youtube.com/live2/<nøgle>` eller egen URL.
Kører samtidig med replay/optagelse (samme koder); startes ikke midt i en optagelse med anden koder-indstilling.

## Billedtids-overlay (F3)
Tider for hvert billede sat på skærmen (rigtige og genererede, `LiveVulkan::shownTimes`; filer: `onFrame`), vist som
graf over 2 s med fps, 1%-low (99. percentil), jitter (standardafvigelse), forsinkelse og tabte billeder.

## Automatisk HDR-lysstyrke
`PlayerBridge::edidPeakFromBytes`: CTA-861-udvidelsen i `/sys/class/drm/card*-<stik>/edid`, HDR Static Metadata
(udvidet tag 6), maks. luminans = 50 · 2^(CV/32) nits. Stikket findes via `QScreen::name()`. Testet med en syntetisk EDID.

## Lydudgang og lydstyrkeudligning
Udgang: PulseAudio/PipeWire-sink (`pactl list sinks`), skiftes mens der afspilles. Udligning: RMS over ~0,4 s trækkes mod
−20 dBFS (angreb 0,3 s, slip 3 s, ±12 dB) plus begrænser ved −1 dBFS. Kun det, man hører – optagelser får originalen.

## Start når capture-kortet tilsluttes
`veyamux --watch` (fra `~/.config/autostart/veyamux-watch.desktop`): inotify på `/dev`; når kortet med navnet fra sidst
(`cap/name`, sysfs) dukker op og VeyAmux ikke kører, startes `--fullscreen --capture /dev/videoN`. `--capture last` finder
også kortet igen, hvis det har fået et andet videoN-nummer.

## Instant replay, optagelse og skærmbilleder
**F9** gemmer de sidste 30/60/120 s, **F10** starter/stopper en optagelse, **F12** gemmer et PNG-skærmbillede
(`~/Videos/VeyAmux`, `~/Pictures/VeyAmux`). I den direkte capture-vej konverteres det viste billede på GPU'en til NV12
(`shaders/LxNv12.hlsl`, BT.709 limited, nedskalering til 720p/1080p/1440p) og kopieres asynkront til CPU'en med en
VkEvent pr. slot (som OBS-kopien), så visningen ikke venter. `src/Recorder.cpp` koder med FFmpeg: `hevc_vaapi` →
`h264_vaapi` → `libx264` (≤1440p), GOP 1 s, ingen B-billeder, tidsstempler fra capture-driverens tid (VFR, tidsbase
1/90000); lyd tappes fra lydløkken (48 kHz, før lydstyrke, tidsstemplet efter lydforsinkelsen) og kodes til AAC.
Pakkerne ligger i en ringbuffer (replay) og/eller skrives direkte til en fragmenteret MP4 (optagelse – kan afspilles,
selv hvis programmet lukkes brat). Replay gemmes fra et keyframe i en baggrundstråd. Målt: A/V-synk i testfil 0 ms
afvigelse. libva bundles ikke i AppImage'en (værtens libva finder værtens VA-driver).

## GPU-tid og latens
Vulkan-timestamps pr. trin (input, støjfjerning, kantudjævning, opskalering, finish, output) i hver frame-slot, læst når
slotten genbruges (ingen ventetid), udglattet. Menuen viser GPU-tid, CPU-tid og "i VeyAmux" (capture-driverens
tidsstempel → GPU-arbejde færdigt).

## Profiler og lydforsinkelse
Profiler gemmer billedindstillingerne og lydforsinkelsen under et navn (taster 1–9). Bundet til et signal
(`WxH@fps FOURCC`) vælges profilen automatisk, når det signal åbnes. Lydforsinkelse −300…+300 ms (capture: kun positiv –
lyden forsinkes med en forsinkelseslinje; filer: billedets tidsplan flyttes).

## Opdateringer
AppImage'en har opdateringsinfo `gh-releases-zsync|8BiTw0LF|VeyAmux|latest|VeyAmux-x86_64.AppImage.zsync`
(`VEYAMUX_UPDATE_INFO` ved bygning). Læg både `.AppImage` og `.AppImage.zsync` op i hver GitHub-release; Gear Lever og
AppImageUpdate henter så kun ændringerne.

## VRR-tilstand
Et capture-kort optager altid med fast frekvens (VRR på kortet er kun HDMI-gennemføring), så et 45 fps-spil kommer som
billeder med 1, 1 og 2 signalbilleders afstand (16,7 / 16,7 / 33,3 ms ved 60 Hz) – også på en VRR-skærm ser det hakkende ud.
*Frame generation → Fra → VRR* sorterer gentagne billeder fra og holder hvert nyt billede, til der er gået ét
indholdsinterval (målt billedfrekvens, ellers et løbende gennemsnit) siden det forrige, højst ¾ interval. Simuleret
(45/40/50 fps i 60 Hz, 1 ms USB-jitter): uregelmæssighed i billedafstand 6,7–8,3 ms → 0,8–1,2 ms, ekstra forsinkelse i
gennemsnit 4–5,5 ms. VRR-tilstanden kører med vsync (kræver genstart, knap i menuen), så skærmen viser hvert billede helt,
når det præsenteres. Kræver *Adaptiv synkronisering* i KDE's skærmindstillinger og VeyAmux i fuldskærm. Kan ikke
kombineres med Lossless Scaling (lsfg-vk kræver fast skærmfrekvens).

## Sprog
Engelsk som standard; dansk automatisk når sprog **eller** region er dansk (`da_DK`, `en_DK`, dansk `LC_TIME` …).

## Lav latens (capture)
Samme princip som Windows-versionen: billedet forlader aldrig GPU'en. Rå YUYV/UYVY/NV12-frames uploades, farvekonverteres og
forbedres i Vulkan *direkte i Qt's egen frame* (samme Vulkan-enhed, ingen readback, ingen CPU-konvertering), og V4L2-køen
tømmes så kun det nyeste billede bruges ("mailbox"). Vsync er slået fra som standard (`VEYAMUX_VSYNC=1` slår det til).
MJPEG dekodes stadig på CPU'en.
Start-logik: programmet afprøver Vulkan-UI i en barneproces og gemmer resultatet; på Wayland prøves XWayland, før det
falder tilbage til software. `VEYAMUX_UI=software|vulkan` tvinger valg, `--reset-ui` nulstiller det gemte valg.

## Capture-kort
Tryk **C** (eller kamera-ikonet). Vælg enhed, tilstand (★ = anbefalet, ≥50 fps foretrækkes) og lydkilde. Start direkte fra terminal:
`./VeyAmux-x86_64.AppImage --capture /dev/video0`. Kilden antages at være limited-range (typisk for HDMI-capture).

## Miljøvariabler
`VEYAMUX_HDR_OUT=0` (slå HDR-output fra) · `VEYAMUX_HDR_MODE=hdr10|scrgb` · `VEYAMUX_GAMESCOPE=0` · `VEYAMUX_AUDIO_MS=<ms>` ·
`--fullscreen` · `--capture last` · `VEYAMUX_REC_SOFTWARE=1` (optag med libx264) · `VEYAMUX_FSR1_RCAS=<stop>` ·
`VEYAMUX_UI=software|vulkan` · `VEYAMUX_VSYNC=1` · `--reset-ui` · `VEYAMUX_LSFG=0` (slå lsfg-vk fra) · `VEYAMUX_DEBUG_FG=1` (logger frame generation og opskalering i den direkte capture-vej) · `VEYAMUX_NO_GPU=1` (slå Vulkan-kæden fra) · `VEYAMUX_HWDEC=0` (slå VAAPI fra) · `VEYAMUX_VAAPI_DEVICE=/dev/dri/renderD128` · `VEYAMUX_VK_DEVICE=n` · `VEYAMUX_SHADER_DIR`

## Genvejstaster
Mellemrum afspil/pause · ←/→ ±5 s (Shift ±30 s) · ↑/↓ volumen · F fuldskærm · E forbedring til/fra · P panel · M lyd fra · O åbn ·
C capture-kort · 1–9 profiler · F9 gem replay · F10 optag · F12 skærmbillede

## Arkitektur
`VkContext` (Vulkan, ét universelt descriptor-layout t→0, u→16, b→32, s→48 via DXC `-fvk-*-shift`) → `EnhanceChain` →
`MediaEngine` (demux/video/lyd/render-tråde) → `PlayerBridge`/`VideoItem` (Qt/QML).
Licens: GPL-3.0 (som det oprindelige Veyra-NRVideo, som VeyAmux bygger på). Tredjepart: se `THIRD_PARTY_NOTICES_LINUX.md`.

Testværktøjer i build-mappen: `veyamux-upscale-test <shaders> <in.rgba> <w> <h> <frames> <out.rgba> lanczos|fsr|fsr1|nis [skala]` (hele forbedringskæden, til
PSNR-sammenligning af opskaleringsmetoderne; `VEYAMUX_TEST_NR=tilstand,styrke` slår støjfjerning til).
