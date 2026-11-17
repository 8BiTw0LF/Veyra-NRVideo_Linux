pragma Singleton
import QtQuick

// VeyAmux colours, shapes and fonts. Everything in the UI takes its look from here. Four themes, chosen on the start
// screen (`name`, saved in the settings as ui/theme):
//   vice    - the original: deep violet night with a hot-pink to sunset-orange gradient
//   code    - black with the green phosphor of old terminals, monospace text
//   inferno - near-black with blood red and glowing ember, classical capitals (Cinzel) for headings
//   retro   - an 8-bit console: greys, black and red, square buttons with a hard edge, pixel font (Press Start 2P)
QtObject {
    id: theme
    property string name: "vice"
    readonly property var names: ["vice", "code", "inferno", "retro"]

    readonly property FontLoader pixelFont: FontLoader { source: "fonts/PressStart2P-Regular.ttf" }
    readonly property FontLoader capitalsFont: FontLoader { source: "fonts/Cinzel-Variable.ttf" }

    readonly property var palettes: ({
        vice: {
            title: "Vice",
            bg: "#0d0716", panel: "#f2140b22", surface: "#231736", surfaceHover: "#33224d", surfaceDeep: "#1a1029",
            border: "#2effffff", line: "#2e1f46",
            accent: "#ff3d8e", accentHover: "#ff63a6", accent2: "#ff9d3d", accent3: "#3fe0d0",
            accentSoft: "#33ff3d8e", accentLine: "#66ff3d8e", accentInk: "#1a0711",
            heading: "#ffffff", text: "#f2e9ff", label: "#cdb9ea", muted: "#a28dc4", dim: "#7a689e",
            warn: "#ffb36b", ok: "#5ff0c8", okLine: "#555ff0c8", toast: "#e61a1029", live: "#ff3d6e",
            g0: "#ff3d8e", g1: "#ff9d3d", g0h: "#ff63a6", g1h: "#ffb25e",
            round: 1.0, edge: 0, font: "", headFont: "", headScale: 1.0, headCaps: false
        },
        code: {
            title: "Code",
            bg: "#000000", panel: "#f2010701", surface: "#06190b", surfaceHover: "#0b2a13", surfaceDeep: "#030f06",
            border: "#3300ff41", line: "#0c2b14",
            accent: "#00ff41", accentHover: "#5cff7f", accent2: "#00b82e", accent3: "#9dffb5",
            accentSoft: "#2600ff41", accentLine: "#6600ff41", accentInk: "#001405",
            heading: "#d4ffde", text: "#b6f7c5", label: "#6fe08f", muted: "#3fa85c", dim: "#2a7a41",
            warn: "#d8ff3d", ok: "#00ff41", okLine: "#5500ff41", toast: "#e6030f06", live: "#ff3b3b",
            g0: "#00ff41", g1: "#00b82e", g0h: "#5cff7f", g1h: "#1fd94a",
            round: 0.45, edge: 0, font: "monospace", headFont: "monospace", headScale: 0.9, headCaps: false
        },
        inferno: {
            title: "Inferno",
            bg: "#0a0404", panel: "#f2140706", surface: "#2a0d0a", surfaceHover: "#3f140f", surfaceDeep: "#1a0806",
            border: "#33ffb08a", line: "#3a1410",
            accent: "#d6181e", accentHover: "#f0292e", accent2: "#ff7a1a", accent3: "#ffb347",
            accentSoft: "#33d6181e", accentLine: "#66d6181e", accentInk: "#fff3ea",
            heading: "#fff3ea", text: "#f2ddd2", label: "#d9a796", muted: "#a87768", dim: "#7a5045",
            warn: "#ffb347", ok: "#ffb347", okLine: "#55ffb347", toast: "#e61a0806", live: "#ff3030",
            g0: "#a3080c", g1: "#e8550f", g0h: "#c41015", g1h: "#ff6a1c",
            round: 0.6, edge: 0, font: "", headFont: "Cinzel", headScale: 1.0, headCaps: false
        },
        retro: {
            title: "Retro",
            bg: "#141414", panel: "#2b2b2b", surface: "#4a4a4a", surfaceHover: "#5c5c5c", surfaceDeep: "#1e1e1e",
            border: "#000000", line: "#3c3c3c",
            accent: "#e3262e", accentHover: "#ff3a40", accent2: "#e3262e", accent3: "#b8b8b8",
            accentSoft: "#40e3262e", accentLine: "#99e3262e", accentInk: "#ffffff",
            heading: "#ffffff", text: "#ececec", label: "#c4c4c4", muted: "#9a9a9a", dim: "#6e6e6e",
            warn: "#ffd23f", ok: "#7cdc5a", okLine: "#557cdc5a", toast: "#2b2b2b", live: "#e3262e",
            g0: "#e3262e", g1: "#e3262e", g0h: "#ff3a40", g1h: "#ff3a40",
            round: 0.0, edge: 2, font: "", headFont: "Press Start 2P", headScale: 0.55, headCaps: true
        }
    })
    readonly property var p: palettes[name] || palettes.vice
    readonly property string title: p.title

    readonly property color bg: p.bg                     // window / empty state
    readonly property color panel: p.panel               // menu background (slightly see-through)
    readonly property color surface: p.surface           // buttons, segments, fields
    readonly property color surfaceHover: p.surfaceHover
    readonly property color surfaceDeep: p.surfaceDeep   // inset boxes
    readonly property color border: p.border
    readonly property color line: p.line                 // separators

    readonly property color accent: p.accent
    readonly property color accentHover: p.accentHover
    readonly property color accent2: p.accent2
    readonly property color accent3: p.accent3
    readonly property color accentSoft: p.accentSoft
    readonly property color accentLine: p.accentLine
    readonly property color accentInk: p.accentInk       // text on accent fills

    readonly property color heading: p.heading
    readonly property color text: p.text
    readonly property color label: p.label               // field labels
    readonly property color muted: p.muted               // hints
    readonly property color dim: p.dim

    readonly property color warn: p.warn
    readonly property color ok: p.ok
    readonly property color okLine: p.okLine
    readonly property color toast: p.toast
    readonly property color live: p.live
    readonly property color overlay: Qt.rgba(Qt.color(p.bg).r, Qt.color(p.bg).g, Qt.color(p.bg).b, 0.78)

    // shapes and fonts
    readonly property real round: p.round                // corner radius factor (retro: square)
    readonly property int edge: p.edge                   // hard button edge in px (retro), 0 = none
    readonly property string font: p.font !== "" ? p.font : Qt.application.font.family
    readonly property string headFont: p.headFont === "Press Start 2P" ? (pixelFont.status === FontLoader.Ready ? pixelFont.font.family : "monospace")
                                     : p.headFont === "Cinzel" ? (capitalsFont.status === FontLoader.Ready ? capitalsFont.font.family : "serif")
                                     : (p.headFont !== "" ? p.headFont : font)
    readonly property real headScale: p.headScale        // the pixel font is about twice as wide
    readonly property bool headCaps: p.headCaps

    // the accent gradient, for selected / primary elements
    readonly property Gradient sunset: Gradient {
        orientation: Gradient.Horizontal
        GradientStop { position: 0.0; color: theme.p.g0 }
        GradientStop { position: 1.0; color: theme.p.g1 }
    }
    readonly property Gradient sunsetHover: Gradient {
        orientation: Gradient.Horizontal
        GradientStop { position: 0.0; color: theme.p.g0h }
        GradientStop { position: 1.0; color: theme.p.g1h }
    }

    // the logo for this theme
    readonly property string logo: "icons/logo-" + (palettes[name] ? name : "vice") + ".svg"
}
