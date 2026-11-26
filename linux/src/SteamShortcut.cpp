// VeyAmux for Linux: non-Steam game shortcut (see SteamShortcut.h).
#include "SteamShortcut.h"
#include "Sandbox.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QRadialGradient>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSvgRenderer>

#include <cstdint>
#include <memory>
#include <vector>

namespace veyamux::steam {

namespace {

uint32_t crc32(const QByteArray& d) {
    uint32_t c = 0xffffffffu;
    for (char ch : d) {
        c ^= uint8_t(ch);
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return ~c;
}

// Binary KeyValues node: 0x00 = map, 0x01 = string, 0x02 = int32, 0x07 = uint64; 0x08 ends a map.
struct Node {
    uint8_t type = 0;
    QByteArray key;
    QByteArray str;          // type 1
    uint64_t num = 0;        // type 2 / 7
    std::vector<std::unique_ptr<Node>> kids;   // type 0
    Node* find(const QByteArray& k) {
        for (auto& c : kids) if (c->key.compare(k, Qt::CaseInsensitive) == 0) return c.get();
        return nullptr;
    }
    Node* set(uint8_t t, const QByteArray& k) {
        Node* n = find(k);
        if (!n) { kids.push_back(std::make_unique<Node>()); n = kids.back().get(); n->key = k; }
        n->type = t;
        return n;
    }
    void setStr(const QByteArray& k, const QByteArray& v) { set(1, k)->str = v; }
    void setInt(const QByteArray& k, uint32_t v) { set(2, k)->num = v; }
};

bool readCString(const QByteArray& d, int& pos, QByteArray& out) {
    const int end = d.indexOf('\0', pos);
    if (end < 0) return false;
    out = d.mid(pos, end - pos);
    pos = end + 1;
    return true;
}

bool parseMap(const QByteArray& d, int& pos, Node& map, int depth) {
    if (depth > 16) return false;
    while (pos < d.size()) {
        const uint8_t t = uint8_t(d[pos++]);
        if (t == 0x08) return true;
        auto n = std::make_unique<Node>();
        n->type = t;
        if (!readCString(d, pos, n->key)) return false;
        if (t == 0x00) {
            if (!parseMap(d, pos, *n, depth + 1)) return false;
        } else if (t == 0x01) {
            if (!readCString(d, pos, n->str)) return false;
        } else if (t == 0x02) {
            if (pos + 4 > d.size()) return false;
            n->num = uint32_t(uint8_t(d[pos])) | uint32_t(uint8_t(d[pos + 1])) << 8 | uint32_t(uint8_t(d[pos + 2])) << 16 | uint32_t(uint8_t(d[pos + 3])) << 24;
            pos += 4;
        } else if (t == 0x07) {
            if (pos + 8 > d.size()) return false;
            n->num = 0;
            for (int i = 7; i >= 0; --i) n->num = (n->num << 8) | uint8_t(d[pos + i]);
            pos += 8;
        } else {
            return false;   // unknown type: do not touch the file
        }
        map.kids.push_back(std::move(n));
    }
    return depth == 0;
}

void writeMap(QByteArray& o, const Node& map) {
    for (const auto& c : map.kids) {
        o.append(char(c->type));
        o.append(c->key).append('\0');
        if (c->type == 0x00) { writeMap(o, *c); o.append(char(0x08)); }
        else if (c->type == 0x01) o.append(c->str).append('\0');
        else if (c->type == 0x02) for (int i = 0; i < 4; ++i) o.append(char((c->num >> (8 * i)) & 0xff));
        else if (c->type == 0x07) for (int i = 0; i < 8; ++i) o.append(char((c->num >> (8 * i)) & 0xff));
    }
}

QStringList steamRoots() {
    const QString h = QDir::homePath();
    QStringList out;
    for (const QString& r : {h + "/.local/share/Steam", h + "/.steam/steam", h + "/.var/app/com.valvesoftware.Steam/.local/share/Steam"}) {
        const QString c = QFileInfo(r).canonicalFilePath();
        if (!c.isEmpty() && QFileInfo(c + "/userdata").isDir() && !out.contains(c)) out << c;
    }
    return out;
}

} // namespace

bool steamRunning() {
    for (const QString& pid : QDir("/proc").entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool num = false;
        pid.toInt(&num);
        if (!num) continue;
        QFile f("/proc/" + pid + "/comm");
        if (f.open(QIODevice::ReadOnly) && f.readAll().trimmed() == "steam") return true;
    }
    return false;
}

bool startedBySteam() {
    for (const char* v : {"SteamGameId", "SteamAppId", "STEAM_COMPAT_APP_ID"})
        if (!qEnvironmentVariable(v).isEmpty() && qEnvironmentVariable(v) != "0") return true;
    // Game Mode (gamescope session): Steam is the session itself
    return qEnvironmentVariable("XDG_CURRENT_DESKTOP").compare("gamescope", Qt::CaseInsensitive) == 0;
}

namespace {

// How to talk to the installed Steam: the native "steam" command, or the Flatpak.
bool steamCommand(QString& program, QStringList& args) {
    args.clear();
    const QString native = QStandardPaths::findExecutable("steam");
    const bool flatpakSteam = QFileInfo(QDir::homePath() + "/.var/app/com.valvesoftware.Steam/.local/share/Steam/userdata").isDir();
    // a Flatpak Steam is used when it is the only one, or when it is the one running
    bool flatpakRunning = false;
    if (flatpakSteam) {
        for (const QString& pid : QDir("/proc").entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QFile f("/proc/" + pid + "/comm");
            if (!f.open(QIODevice::ReadOnly) || f.readAll().trimmed() != "steam") continue;
            QFile e("/proc/" + pid + "/environ");
            if (e.open(QIODevice::ReadOnly) && e.readAll().contains("FLATPAK_ID=com.valvesoftware.Steam")) flatpakRunning = true;
        }
    }
    const QString flatpak = QStandardPaths::findExecutable("flatpak");
    if (flatpakSteam && !flatpak.isEmpty() && (native.isEmpty() || flatpakRunning)) {
        program = flatpak;
        args << "run" << "com.valvesoftware.Steam";
        return true;
    }
    if (native.isEmpty()) return false;
    program = native;
    return true;
}

// The environment for Steam: the user's, without what VeyAmux (or the AppImage runtime) set for itself.
QProcessEnvironment cleanEnvironment() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString& k : env.keys()) {
        if (k.startsWith("QT_") || k.startsWith("QSG_") || k.startsWith("QML") || k.startsWith("VEYAMUX_") ||
            k.startsWith("LSFGVK_") || k == "DISABLE_LSFGVK" || k == "LIBVA_DRIVERS_PATH" || k == "APPIMAGE" ||
            k == "APPDIR" || k == "ARGV0" || k == "OWD" || k == "LD_LIBRARY_PATH" || k == "LD_PRELOAD" ||
            k == "GST_PLUGIN_SYSTEM_PATH" || k == "GDK_PIXBUF_MODULE_FILE")
            env.remove(k);
    }
    return env;
}

bool runSteam(const QStringList& extra) {
    QString program;
    QStringList args;
    if (!steamCommand(program, args)) return false;
    QProcess p;
    p.setProgram(program);
    p.setArguments(args + extra);
    p.setProcessEnvironment(cleanEnvironment());
    p.setWorkingDirectory(QDir::homePath());
    p.setStandardInputFile(QProcess::nullDevice());
    p.setStandardOutputFile(QProcess::nullDevice());
    p.setStandardErrorFile(QProcess::nullDevice());
    return p.startDetached();
}

// ---- artwork
void drawLogo(QPainter& p, const QRectF& r) {
    static QSvgRenderer svg(QStringLiteral(":/qml/icons/logo.svg"));
    svg.render(&p, r);
}

void background(QPainter& p, const QSize& s, QPointF glowAt, double glowR) {
    QLinearGradient g(0, 0, s.width(), s.height());
    g.setColorAt(0, QColor("#1a1024"));
    g.setColorAt(1, QColor("#0b0c11"));
    p.fillRect(QRect(QPoint(0, 0), s), g);
    QRadialGradient glow(glowAt, glowR);
    glow.setColorAt(0, QColor(255, 106, 48, 120));
    glow.setColorAt(0.5, QColor(255, 61, 142, 40));
    glow.setColorAt(1, QColor(255, 61, 142, 0));
    p.fillRect(QRect(QPoint(0, 0), s), glow);
    // fine diagonal lines: the X of the logo, repeated faintly
    p.save();
    p.setPen(QPen(QColor(255, 255, 255, 10), 2));
    for (int x = -s.height(); x < s.width(); x += 46) {
        p.drawLine(QPointF(x, 0), QPointF(x + s.height(), s.height()));
        p.drawLine(QPointF(x + s.height(), 0), QPointF(x, s.height()));
    }
    p.restore();
}

// VeyAmux's own copy of Inter (qml/fonts), so the artwork looks the same on every system
QString artFont() {
    static QString family = [] {
        QString f;
        for (const char* file : {":/qml/fonts/Inter-Black.ttf", ":/qml/fonts/Inter-SemiBold.ttf"}) {
            const int id = QFontDatabase::addApplicationFont(QString::fromLatin1(file));
            if (id >= 0 && f.isEmpty()) f = QFontDatabase::applicationFontFamilies(id).value(0);
        }
        return f.isEmpty() ? QStringLiteral("Sans Serif") : f;
    }();
    return family;
}

QFont artFontAt(int px, QFont::Weight w, int spacing = 100) {
    QFont f(artFont());
    f.setPixelSize(px);
    f.setWeight(w);
    f.setLetterSpacing(QFont::PercentageSpacing, spacing);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

// the sunset gradient of the theme, across a rectangle
QLinearGradient sunset(const QRectF& r) {
    QLinearGradient g(r.topLeft(), r.topRight());
    g.setColorAt(0, QColor("#ffa477"));
    g.setColorAt(1, QColor("#ff3d8e"));
    return g;
}

// "VeyAmux" (Amux in the gradient), shrunk until it fits the rectangle's width; a line under it when given
void wordmark(QPainter& p, const QRectF& r, int px, Qt::Alignment align, const QString& sub = {}, int subPx = 0) {
    QFont f = artFontAt(px, QFont::Black, 96);
    const QString vey = "Vey", amux = "Amux";
    for (int i = 0; i < 40 && QFontMetricsF(f).horizontalAdvance(vey + amux) > r.width(); ++i) f.setPixelSize(int(f.pixelSize() * 0.95));
    QFont sf = artFontAt(std::max(8, subPx), QFont::DemiBold, 110);
    for (int i = 0; !sub.isEmpty() && i < 40 && QFontMetricsF(sf).horizontalAdvance(sub) > r.width(); ++i) sf.setPixelSize(std::max(8, int(sf.pixelSize() * 0.95)));
    const QFontMetricsF fm(f);
    const double total = fm.height() + (sub.isEmpty() ? 0 : sf.pixelSize() * 1.6);
    double y = r.top();
    if (align & Qt::AlignVCenter) y = r.center().y() - total / 2;
    else if (align & Qt::AlignBottom) y = r.bottom() - total;
    const double w = fm.horizontalAdvance(vey + amux);
    double x = r.left();
    if (align & Qt::AlignHCenter) x = r.center().x() - w / 2;
    else if (align & Qt::AlignRight) x = r.right() - w;
    const double base = y + fm.ascent();
    QPainterPath path;
    path.addText(QPointF(x, base), f, vey);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#ffffff"));
    p.drawPath(path);
    QPainterPath path2;
    path2.addText(QPointF(x + fm.horizontalAdvance(vey), base), f, amux);
    p.setBrush(sunset(QRectF(x, y, w, fm.height())));
    p.drawPath(path2);
    if (!sub.isEmpty()) {
        p.setFont(sf);
        p.setPen(QColor(255, 226, 212, 210));
        const QRectF sr(r.left(), y + fm.height() + sf.pixelSize() * 0.2, r.width(), sf.pixelSize() * 1.4);
        p.drawText(sr, int(align & Qt::AlignHorizontal_Mask) | Qt::AlignVCenter, sub);
    }
}

// what VeyAmux does, for the library background: a headline and lines with a gradient mark before each
void features(QPainter& p, const QRectF& r) {
    struct Line { QString big, small; };
    const Line lines[] = {
        {"Lossless Scaling", "or free FSR 3.1 frame generation"},
        {"PlayStation Remote Play", "PS5 · PS4"},
        {"Jellyfin · Plex", "films and series, subtitles"},
        {"Retro games", "26 consoles, run-ahead"},
    };
    // headline: 30 -> 60 FPS
    QFont head = artFontAt(92, QFont::Black, 96);
    const QString h1 = "30", arrow = "  →  ", h2 = "60 FPS";
    const QFontMetricsF hm(head);
    double x = r.left();
    const double base = r.top() + hm.ascent();
    QPainterPath a;
    a.addText(QPointF(x, base), head, h1);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 235));
    p.drawPath(a);
    x += hm.horizontalAdvance(h1);
    QFont arrowFont = artFontAt(64, QFont::DemiBold);
    p.setFont(arrowFont);
    p.setPen(QColor(255, 226, 212, 170));
    p.drawText(QRectF(x, r.top(), QFontMetricsF(arrowFont).horizontalAdvance(arrow), hm.height()), Qt::AlignVCenter, arrow);
    x += QFontMetricsF(arrowFont).horizontalAdvance(arrow);
    QPainterPath b;
    b.addText(QPointF(x, base), head, h2);
    p.setPen(Qt::NoPen);
    p.setBrush(sunset(QRectF(x, r.top(), hm.horizontalAdvance(h2), hm.height())));
    p.drawPath(b);
    // the lines
    double y = r.top() + hm.height() + 26;
    const QFont big = artFontAt(34, QFont::Black, 98), small = artFontAt(25, QFont::DemiBold, 102);
    for (const auto& l : lines) {
        p.setPen(Qt::NoPen);
        p.setBrush(sunset(QRectF(r.left(), y, 10, 40)));
        p.drawRoundedRect(QRectF(r.left(), y + 9, 8, 38), 4, 4);
        p.setFont(big);
        p.setPen(QColor("#ffffff"));
        const double bw = QFontMetricsF(big).horizontalAdvance(l.big);
        p.drawText(QRectF(r.left() + 26, y, bw + 4, 56), Qt::AlignVCenter | Qt::AlignLeft, l.big);
        p.setFont(small);
        p.setPen(QColor(255, 226, 212, 175));
        if (QFontMetricsF(p.font()).horizontalAdvance(l.small) < r.width() - bw - 44)   // only when it fits whole
            p.drawText(QRectF(r.left() + 26 + bw + 18, y + 3, r.width() - bw - 44, 56), Qt::AlignVCenter | Qt::AlignLeft, l.small);
        y += 64;
    }
}

QImage canvas(int w, int h, bool transparent) {
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(transparent ? Qt::transparent : QColor("#0b0c11"));
    return img;
}

} // namespace

QMap<QString, QImage> artwork() {
    const QString tag = QStringLiteral("CAPTURE  ·  REMOTE PLAY  ·  MEDIA  ·  RETRO GAMES");
    QMap<QString, QImage> out;
    {   // portrait cover 600x900
        QImage img = canvas(600, 900, false);
        QPainter p(&img);
        p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        background(p, img.size(), QPointF(300, 360), 420);
        drawLogo(p, QRectF(150, 190, 300, 300));
        wordmark(p, QRectF(40, 540, 520, 200), 104, Qt::AlignHCenter | Qt::AlignTop, tag, 17);
        // what it does, small, at the bottom
        p.setFont(artFontAt(21, QFont::DemiBold, 104));
        p.setPen(QColor(255, 226, 212, 150));
        p.drawText(QRectF(40, 760, 520, 90), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                   QStringLiteral("Upscaling · frame generation · 30 → 60 fps\nJellyfin · Plex · 26 retro consoles"));
        out.insert("p", img);
    }
    {   // wide cover 920x430
        QImage img = canvas(920, 430, false);
        QPainter p(&img);
        p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        background(p, img.size(), QPointF(200, 215), 330);
        drawLogo(p, QRectF(60, 105, 220, 220));
        wordmark(p, QRectF(320, 0, 560, 430), 100, Qt::AlignLeft | Qt::AlignVCenter, tag, 17);
        out.insert("", img);
    }
    {   // background 1920x620. Steam puts the logo over its left part, so what VeyAmux does stands on the right, inside
        // the part every view of the library keeps
        QImage img = canvas(1920, 620, false);
        QPainter p(&img);
        p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        background(p, img.size(), QPointF(1500, 300), 820);
        p.setOpacity(0.05);
        drawLogo(p, QRectF(1580, 40, 540, 540));   // the mark, faint, behind the text
        p.setOpacity(1.0);
        features(p, QRectF(1000, 120, 860, 440));
        out.insert("_hero", img);
    }
    {   // logo 1280x400, transparent
        QImage img = canvas(1280, 400, true);
        QPainter p(&img);
        p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        drawLogo(p, QRectF(20, 40, 320, 320));
        wordmark(p, QRectF(380, 0, 880, 400), 190, Qt::AlignLeft | Qt::AlignVCenter);
        out.insert("_logo", img);
    }
    return out;
}

bool shutdownSteam() { return runSteam({"-shutdown"}); }

bool startSteam() { return runSteam({}); }

Result addShortcut(const QString& exe, const QString& icon, const QString& launchOptions, const QMap<QString, QImage>& grid) {
    Result r;
    const QStringList roots = steamRoots();
    if (roots.isEmpty()) { r.message = "Steam was not found"; return r; }
    if (steamRunning()) { r.message = "Close Steam first"; return r; }
    const QByteArray exeQ = "\"" + exe.toUtf8() + "\"";
    const QByteArray dirQ = "\"" + QFileInfo(exe).absolutePath().toUtf8() + "/\"";
    // VeyAmux Debug gets its own shortcut next to VeyAmux's
#ifdef VEYAMUX_DEBUG_BUILD
    const QByteArray name = "VeyAmux Debug";
#else
    const QByteArray name = "VeyAmux";
#endif
    // Steam's id for a non-Steam shortcut: CRC32 of exe + name with the top bit set
    const QByteArray idSrc = exeQ + name;
    const uint32_t appid = crc32(idSrc) | 0x80000000u;
    for (const QString& root : roots) {
        for (const QString& acct : QDir(root + "/userdata").entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (acct == "0" || acct == "ac" || !QFileInfo(root + "/userdata/" + acct + "/config").isDir()) continue;
            const QString path = root + "/userdata/" + acct + "/config/shortcuts.vdf";
            Node top;
            QFile in(path);
            if (in.exists()) {
                if (!in.open(QIODevice::ReadOnly)) { r.message = "Cannot read " + path; continue; }
                const QByteArray d = in.readAll();
                int pos = 0;
                if (!d.isEmpty() && !parseMap(d, pos, top, 0)) { r.message = "Unexpected format in " + path + " (left unchanged)"; continue; }
                QFile::copy(path, path + ".veyamux-backup");   // one backup, the first time
            }
            Node* list = top.find("shortcuts");
            if (!list) { list = top.set(0, "shortcuts"); }
            // an existing VeyAmux entry (same name, or the same AppImage) is updated
            Node* e = nullptr;
            int next = 0;
            for (auto& c : list->kids) {
                next = std::max(next, c->key.toInt() + 1);
                Node* n = c->find("AppName");
                Node* x = c->find("Exe");
                if ((n && n->str == name) || (x && x->str == exeQ)) e = c.get();
            }
            if (!e) {
                list->kids.push_back(std::make_unique<Node>());
                e = list->kids.back().get();
                e->type = 0;
                e->key = QByteArray::number(next);
            }
            e->setInt("appid", appid);
            e->setStr("AppName", name);
            e->setStr("Exe", exeQ);
            e->setStr("StartDir", dirQ);
            e->setStr("icon", icon.toUtf8());
            if (!e->find("ShortcutPath")) e->setStr("ShortcutPath", "");
            e->setStr("LaunchOptions", launchOptions.toUtf8());
            if (!e->find("IsHidden")) e->setInt("IsHidden", 0);
            if (!e->find("AllowDesktopConfig")) e->setInt("AllowDesktopConfig", 1);
            if (!e->find("AllowOverlay")) e->setInt("AllowOverlay", 1);
            if (!e->find("OpenVR")) e->setInt("OpenVR", 0);
            if (!e->find("Devkit")) e->setInt("Devkit", 0);
            if (!e->find("DevkitGameID")) e->setStr("DevkitGameID", "");
            if (!e->find("DevkitOverrideAppID")) e->setInt("DevkitOverrideAppID", 0);
            if (!e->find("LastPlayTime")) e->setInt("LastPlayTime", 0);
            if (!e->find("FlatpakAppID")) e->setStr("FlatpakAppID", "");
            if (!e->find("tags")) e->set(0, "tags");
            QByteArray o;
            writeMap(o, top);
            o.append(char(0x08));
            QSaveFile out(path);
            if (!out.open(QIODevice::WriteOnly) || out.write(o) != o.size() || !out.commit()) { r.message = "Cannot write " + path; continue; }
            // library artwork: userdata/<account>/config/grid/<appid><suffix>.png (replaces earlier VeyAmux artwork,
            // never artwork the user chose for VeyAmux: that one is kept when it is not a .png we wrote)
            const QString gridDir = root + "/userdata/" + acct + "/config/grid";
            if (!grid.isEmpty() && QDir().mkpath(gridDir)) {
                for (auto it = grid.cbegin(); it != grid.cend(); ++it) {
                    const QString base = gridDir + "/" + QString::number(appid) + it.key();
                    bool own = false;
                    for (const char* ext : {".jpg", ".jpeg", ".webp"}) own = own || QFileInfo::exists(base + ext);
                    if (own) continue;   // the user set their own picture in Steam
                    it.value().save(base + ".png", "PNG");
                }
            }
            ++r.accounts;
        }
    }
    r.ok = r.accounts > 0;
    if (r.ok) r.message.clear();
    else if (r.message.isEmpty()) r.message = "No Steam account found (start Steam once and log in)";
    return r;
}

} // namespace veyamux::steam
