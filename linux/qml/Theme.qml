pragma Singleton
import QtQuick

// VeyAmux colours: a warm "sunset over the ocean" palette - deep violet night, hot pink and orange sunset accents,
// a turquoise sea for status. Everything in the UI takes its colours from here.
QtObject {
    readonly property color bg: "#0d0716"            // window / empty state
    readonly property color panel: "#f2140b22"       // menu background (slightly see-through)
    readonly property color surface: "#231736"       // buttons, segments, fields
    readonly property color surfaceHover: "#33224d"
    readonly property color surfaceDeep: "#1a1029"   // inset boxes
    readonly property color border: "#2effffff"
    readonly property color line: "#2e1f46"          // separators

    readonly property color accent: "#ff3d8e"        // hot pink
    readonly property color accentHover: "#ff63a6"
    readonly property color accent2: "#ff9d3d"       // sunset orange
    readonly property color accent3: "#3fe0d0"       // turquoise
    readonly property color accentSoft: "#33ff3d8e"
    readonly property color accentLine: "#66ff3d8e"
    readonly property color accentInk: "#1a0711"      // text on accent fills

    readonly property color heading: "#ffffff"
    readonly property color text: "#f2e9ff"
    readonly property color label: "#cdb9ea"         // field labels
    readonly property color muted: "#a28dc4"         // hints
    readonly property color dim: "#7a689e"

    readonly property color warn: "#ffb36b"
    readonly property color ok: "#5ff0c8"
    readonly property color okLine: "#555ff0c8"
    readonly property color toast: "#e61a1029"
    readonly property color live: "#ff3d6e"

    // pink -> orange, for selected / primary elements
    readonly property Gradient sunset: Gradient {
        orientation: Gradient.Horizontal
        GradientStop { position: 0.0; color: "#ff3d8e" }
        GradientStop { position: 1.0; color: "#ff9d3d" }
    }
    readonly property Gradient sunsetHover: Gradient {
        orientation: Gradient.Horizontal
        GradientStop { position: 0.0; color: "#ff63a6" }
        GradientStop { position: 1.0; color: "#ffb25e" }
    }
}
