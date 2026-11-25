// VeyAmux for Linux: the Games page (see GameLibrary.h).
#include "GameLibrary.h"
#include "Download.h"
#include "Libretro.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <functional>

namespace veyamux {

// ------------------------------------------------------------------ systems
// folders: the names RetroArch, EmulationStation / ES-DE, EmuDeck, RetroDECK and Batocera use, and the common ones
const std::vector<GameSystem>& gameSystems() {
    static const std::vector<GameSystem> s = {
        {"nes", "NES / Famicom", "Nintendo - Nintendo Entertainment System",
         "nes,famicom,nintendo,nintendoentertainmentsystem,nintendonintendoentertainmentsystem,fc", "nes,unf,unif", "nes,unf,unif,zip,7z",
         "mesen,fceumm,nestopia", false, ""},
        {"fds", "Famicom Disk System", "Nintendo - Family Computer Disk System", "fds,famicomdisksystem,nintendofamilycomputerdisksystem",
         "fds", "fds,zip", "mesen,fceumm,nestopia", false, "disksys.rom"},
        {"snes", "Super Nintendo", "Nintendo - Super Nintendo Entertainment System",
         "snes,supernintendo,superfamicom,sfc,supernes,nintendosupernintendoentertainmentsystem,snesna", "sfc,smc,swc,fig,bs,st",
         "sfc,smc,swc,fig,bs,st,zip,7z", "snes9x,bsnes,snes9x2010", false, ""},
        {"gb", "Game Boy", "Nintendo - Game Boy", "gb,gameboy,nintendogameboy", "gb", "gb,zip,7z", "gambatte,sameboy,mgba", false, ""},
        {"gbc", "Game Boy Color", "Nintendo - Game Boy Color", "gbc,gameboycolor,nintendogameboycolor", "gbc", "gbc,gb,zip,7z",
         "gambatte,sameboy,mgba", false, ""},
        {"gba", "Game Boy Advance", "Nintendo - Game Boy Advance", "gba,gameboyadvance,nintendogameboyadvance", "gba", "gba,zip,7z",
         "mgba,vba_next,gpsp", false, ""},
        {"vb", "Virtual Boy", "Nintendo - Virtual Boy", "vb,virtualboy,nintendovirtualboy", "vb,vboy", "vb,vboy,zip",
         "mednafen_vb", false, ""},
        {"md", "Mega Drive / Genesis", "Sega - Mega Drive - Genesis",
         "md,megadrive,genesis,segagenesis,segamegadrive,segamegadrivegenesis,megadrivejp", "md,gen,smd", "md,gen,smd,bin,zip,7z",
         "genesis_plus_gx,picodrive", false, ""},
        {"sms", "Master System", "Sega - Master System - Mark III", "sms,mastersystem,segamastersystem,segamastersystemmarkiii,markiii",
         "sms", "sms,bin,zip,7z", "genesis_plus_gx,gearsystem,picodrive", false, ""},
        {"gg", "Game Gear", "Sega - Game Gear", "gg,gamegear,segagamegear", "gg", "gg,zip,7z", "genesis_plus_gx,gearsystem", false, ""},
        {"sg1000", "SG-1000", "Sega - SG-1000", "sg1000,sg,segasg1000", "sg", "sg,bin,zip", "genesis_plus_gx,gearsystem", false, ""},
        {"segacd", "Mega-CD / Sega CD", "Sega - Mega-CD - Sega CD", "segacd,megacd,segamegacd,segamegacdsegacd", "", "cue,chd,iso,m3u",
         "genesis_plus_gx,picodrive", false, "bios_CD_U.bin, bios_CD_E.bin, bios_CD_J.bin"},
        {"32x", "32X", "Sega - 32X", "32x,sega32x", "32x", "32x,bin,zip,7z", "picodrive", false, ""},
        {"saturn", "Saturn", "Sega - Saturn", "saturn,segasaturn", "", "cue,chd,ccd,m3u,iso", "mednafen_saturn,yabause", true,
         "sega_101.bin, mpr-17933.bin"},
        {"pce", "PC Engine / TurboGrafx-16", "NEC - PC Engine - TurboGrafx 16",
         "pce,pcengine,tg16,turbografx16,turbografx,necpcengine,necpcengineturbografx16", "pce", "pce,zip,7z",
         "mednafen_pce_fast,mednafen_pce", false, ""},
        {"pcecd", "PC Engine CD / TurboGrafx-CD", "NEC - PC Engine CD - TurboGrafx-CD",
         "pcecd,pcenginecd,tgcd,turbografxcd,necpcenginecd", "", "cue,chd,ccd,m3u", "mednafen_pce_fast,mednafen_pce", false,
         "syscard3.pce"},
        {"psx", "PlayStation", "Sony - PlayStation", "psx,ps1,playstation,playstation1,sonyplaystation,psone", "pbp",
         "cue,chd,pbp,m3u,ccd,iso,exe", "pcsx_rearmed,mednafen_psx,swanstation", true, "scph5501.bin (optional for PCSX ReARMed)"},
        {"a2600", "Atari 2600", "Atari - 2600", "atari2600,2600,a2600,atari", "a26", "a26,bin,zip", "stella,stella2014", false, ""},
        {"a7800", "Atari 7800", "Atari - 7800", "atari7800,7800,a7800", "a78", "a78,bin,zip", "prosystem", false, ""},
        {"lynx", "Atari Lynx", "Atari - Lynx", "lynx,atarilynx", "lnx", "lnx,zip", "handy,mednafen_lynx", false, "lynxboot.img"},
        {"ngp", "Neo Geo Pocket", "SNK - Neo Geo Pocket", "ngp,neogeopocket,snkneogeopocket", "ngp", "ngp,zip", "mednafen_ngp,race",
         false, ""},
        {"ngpc", "Neo Geo Pocket Color", "SNK - Neo Geo Pocket Color", "ngpc,neogeopocketcolor,snkneogeopocketcolor", "ngc",
         "ngc,ngp,zip", "mednafen_ngp,race", false, ""},
        {"ws", "WonderSwan", "Bandai - WonderSwan", "ws,wonderswan,bandaiwonderswan", "ws", "ws,zip", "mednafen_wswan", false, ""},
        {"wsc", "WonderSwan Color", "Bandai - WonderSwan Color", "wsc,wonderswancolor,bandaiwonderswancolor", "wsc", "wsc,ws,zip",
         "mednafen_wswan", false, ""},
        {"arcade", "Arcade", "FBNeo - Arcade Games", "arcade,mame,fbneo,fba,finalburn,finalburnneo,cps1,cps2,cps3,neogeo,mame2003,mameplus",
         "", "zip,7z", "fbneo,mame2003_plus,mame", false, "neogeo.zip (Neo Geo games)"},
    };
    return s;
}

const GameSystem* gameSystem(const QString& id) {
    for (const auto& s : gameSystems()) if (id == QLatin1String(s.id)) return &s;
    return nullptr;
}

namespace {

QString folderKey(const QString& name) {
    QString k;
    for (QChar c : name.toLower()) if (c.isLetterOrNumber()) k += c;
    return k;
}
bool listHas(const char* list, const QString& v) {
    for (const auto& p : QString::fromLatin1(list).split(',', Qt::SkipEmptyParts)) if (p == v) return true;
    return false;
}

// a CD image's own header: which system the disc is for ("" if unknown)
QString sniffDisc(const QString& path, int depth = 0) {
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "pbp") return "psx";
    QString data = path;
    if (ext == "m3u" && depth == 0) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        for (const auto& line : QString::fromUtf8(f.read(65536)).split('\n')) {
            const QString l = line.trimmed();
            if (l.isEmpty() || l.startsWith('#')) continue;
            return sniffDisc(QFileInfo(path).dir().filePath(l), depth + 1);
        }
        return {};
    }
    if (ext == "cue") {
        // the first data track's file
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        QString file;
        data.clear();
        static const QRegularExpression fileRe(R"re(^\s*FILE\s+"?([^"]+?)"?\s+\S+\s*$)re", QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression trackRe(R"(^\s*TRACK\s+\d+\s+(\S+))", QRegularExpression::CaseInsensitiveOption);
        for (const auto& line : QString::fromUtf8(f.read(65536)).split('\n')) {
            if (auto m = fileRe.match(line); m.hasMatch()) file = m.captured(1);
            else if (auto t = trackRe.match(line); t.hasMatch() && !t.captured(1).startsWith("AUDIO", Qt::CaseInsensitive) && !file.isEmpty()) {
                data = QFileInfo(path).dir().filePath(file);
                break;
            }
        }
        if (data.isEmpty()) return {};
    } else if (ext != "iso" && ext != "bin" && ext != "img" && ext != "ccd") {
        return {};
    }
    if (ext == "ccd") data = QFileInfo(path).dir().filePath(QFileInfo(path).completeBaseName() + ".img");
    QFile d(data);
    if (!d.open(QIODevice::ReadOnly)) return {};
    const QByteArray head = d.read(1 << 20);
    if (head.contains("SEGA SEGASATURN")) return "saturn";
    if (head.contains("SEGADISCSYSTEM") || head.contains("SEGA MEGA DRIVE") || head.contains("SEGA GENESIS")) return "segacd";
    if (head.contains("PC Engine CD-ROM SYSTEM")) return "pcecd";
    if (head.contains("PLAYSTATION") || head.contains("Sony Computer Entertainment")) return "psx";
    return {};
}

// files inside a zip: the system of the first one with a known type
QString zipSystem(const QString& path) {
    for (const auto& e : retro::zipList(path.toStdString())) {
        const QString ext = QFileInfo(QString::fromStdString(e.name)).suffix().toLower();
        for (const auto& s : gameSystems()) if (listHas(s.exts, ext)) return s.id;
    }
    return {};
}

} // namespace

QString detectSystem(const QString& path, const QString& root) {
    const QFileInfo fi(path);
    const QString ext = fi.suffix().toLower();
    // the nearest folder that names a system
    QString rel = QDir(root).relativeFilePath(fi.absolutePath());
    QStringList parts = rel == "." ? QStringList() : rel.split('/', Qt::SkipEmptyParts);
    for (int i = int(parts.size()) - 1; i >= 0; --i) {
        const QString k = folderKey(parts[i]);
        for (const auto& s : gameSystems()) {
            if (!listHas(s.folders, k)) continue;
            if (!listHas(s.folderExts, ext)) return {};   // a file the system's games never are (a readme, a cover)
            if ((ext == "cue" || ext == "chd" || ext == "iso" || ext == "m3u") && QString(s.id) != "psx" && QString(s.id) != "segacd"
                && QString(s.id) != "saturn" && QString(s.id) != "pcecd") return {};
            return s.id;
        }
    }
    // the file type alone
    for (const auto& s : gameSystems()) if (listHas(s.exts, ext)) return s.id;
    if (ext == "zip") return zipSystem(path);
    if (ext == "cue" || ext == "iso" || ext == "m3u" || ext == "ccd") return sniffDisc(path);
    return {};
}

QString thumbnailName(const QString& stem) {
    QString n = stem;
    for (QChar& c : n) if (QStringLiteral("&*/:`<>?\\|\"").contains(c)) c = '_';
    return n;
}

QString displayTitle(const QString& stem) {
    static const QRegularExpression tags(R"(\s*[\(\[][^\)\]]*[\)\]])");
    QString t = stem;
    t.remove(tags);
    t = t.trimmed();
    // "Legend of Zelda, The - A Link to the Past" -> "The Legend of Zelda - A Link to the Past"
    static const QRegularExpression art(R"(^(.*?), (The|A|An|Die|Der|Das|Le|La|Les|El|Los)( - .*)?$)");
    if (auto m = art.match(t); m.hasMatch()) t = m.captured(2) + ' ' + m.captured(1) + m.captured(3);
    return t.isEmpty() ? stem : t;
}

namespace {
QString normalTitle(const QString& stem) {
    static const QRegularExpression tags(R"([\(\[][^\)\]]*[\)\]])");
    QString t = stem.toLower();
    t.remove(tags);
    t.replace("&", "and");
    QString k;
    for (QChar c : t) if (c.isLetterOrNumber()) k += c;
    return k;
}
QString normalNoArticle(const QString& stem) {
    QString t = displayTitle(stem).toLower();
    for (const char* a : {"the ", "a ", "an "}) if (t.startsWith(QLatin1String(a))) { t = t.mid(int(qstrlen(a))); break; }
    t.replace("&", "and");
    QString k;
    for (QChar c : t) if (c.isLetterOrNumber()) k += c;
    return k;
}
QStringList regionsOf(const QString& stem) {
    static const QRegularExpression paren(R"(\(([^\)]*)\))");
    QStringList out;
    for (auto it = paren.globalMatch(stem); it.hasNext();)
        for (const auto& p : it.next().captured(1).split(',')) out << p.trimmed().toLower();
    return out;
}
} // namespace

QString matchThumbnail(const QString& stem, const QStringList& names) {
    const QString want = thumbnailName(stem);
    if (names.contains(want)) return want;
    const QString key = normalNoArticle(stem);
    const QStringList regions = regionsOf(stem);
    QString best;
    int bestScore = -1;
    for (const auto& n : names) {
        if (normalNoArticle(n) != key) continue;
        const QStringList r = regionsOf(n);
        int score = 1;
        for (const auto& x : regions) if (r.contains(x)) score += 10;
        if (r.contains("usa")) score += 3;
        else if (r.contains("world")) score += 2;
        else if (r.contains("europe")) score += 1;
        // the plain release beats betas, prototypes and hacks
        for (const auto& x : r) if (x.startsWith("beta") || x.startsWith("proto") || x.contains("hack") || x.startsWith("sample")) score -= 5;
        if (score > bestScore || (score == bestScore && n.size() < best.size())) { best = n; bestScore = score; }
    }
    (void)normalTitle;
    return best;
}

// ------------------------------------------------------------------ GameLibrary

GameLibrary::GameLibrary(QObject* parent) : QObject(parent) {
    folder_ = QSettings().value("games/folder").toString();
    load();
    current_ = QSettings().value("games/system").toString();
    // no list kept for this folder yet: look once the program runs
    bool empty;
    { std::lock_guard l(m_); empty = entries_.empty(); }
    if (empty && !folder_.isEmpty()) QTimer::singleShot(1000, this, [this] { rescan(); });
    // the system shown last, else the first
    bool have = false;
    const auto sys = systems();
    for (const auto& v : sys) if (v.toMap().value("id").toString() == current_) have = true;
    if (!have && !sys.isEmpty()) current_ = sys.first().toMap().value("id").toString();
}

GameLibrary::~GameLibrary() {
    quit_ = true;
    if (worker_.joinable()) worker_.join();
}

QString GameLibrary::dataDir(const QString& sub) const {
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/games/" + sub;
    QDir().mkpath(d);
    return d;
}

QString GameLibrary::biosFolder() const {
    const QString own = QSettings().value("games/biosFolder").toString();
    if (!own.isEmpty()) return own;
    // RetroArch's (Flatpak or native) when it has BIOS files in it, so they need not be copied
    for (const QString& d : {QDir::homePath() + "/.var/app/org.libretro.RetroArch/config/retroarch/system",
                             QDir::homePath() + "/.config/retroarch/system"}) {
        if (QDir(d).exists() && !QDir(d).entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty()) return d;
    }
    return dataDir("bios");
}

void GameLibrary::chooseFolder() {
    const QString d = QFileDialog::getExistingDirectory(nullptr, QString(), folder_.isEmpty() ? QDir::homePath() : folder_);
    if (d.isEmpty()) return;
    folder_ = d;
    QSettings().setValue("games/folder", d);
    emit changed();
    rescan();
}

void GameLibrary::chooseBiosFolder() {
    const QString d = QFileDialog::getExistingDirectory(nullptr, QString(), biosFolder());
    if (d.isEmpty()) return;
    QSettings().setValue("games/biosFolder", d);
    emit changed();
}

void GameLibrary::openBiosFolder() { QDesktopServices::openUrl(QUrl::fromLocalFile(biosFolder())); }

void GameLibrary::setBusy(const QString& key, const QString& arg, int p) {
    {
        std::lock_guard l(m_);
        busy_ = key;
        busyArg_ = arg;
    }
    progress_ = p;
    QMetaObject::invokeMethod(this, [this] { emit busyChanged(); }, Qt::QueuedConnection);
}

void GameLibrary::say(const QString& key, const QString& arg, bool error) {
    QMetaObject::invokeMethod(this, [this, key, arg, error] { emit message(key, arg, error); }, Qt::QueuedConnection);
}

void GameLibrary::work(std::function<void()> fn) {
    if (working_.exchange(true)) return;   // one job at a time
    if (worker_.joinable()) worker_.join();
    quit_ = false;
    worker_ = std::thread([this, fn = std::move(fn)] {
        fn();
        setBusy({});
        working_ = false;
    });
}

// the scan result is kept (games.json), so the page shows at once on the next start
void GameLibrary::load() {
    QFile f(dataDir({}) + "/games.json");
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.value("folder").toString() != folder_) return;
    std::lock_guard l(m_);
    entries_.clear();
    for (const auto& v : o.value("games").toArray()) {
        const QJsonObject g = v.toObject();
        entries_.push_back({g.value("path").toString(), g.value("system").toString(), g.value("title").toString(), g.value("region").toString()});
    }
}

void GameLibrary::save() const {
    QJsonArray a;
    {
        std::lock_guard l(m_);
        for (const auto& e : entries_) a.append(QJsonObject{{"path", e.path}, {"system", e.system}, {"title", e.title}, {"region", e.region}});
    }
    QFile f(dataDir({}) + "/games.json");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(QJsonObject{{"folder", folder_}, {"games", a}}).toJson(QJsonDocument::Compact));
}

void GameLibrary::rescan() {
    if (folder_.isEmpty()) { chooseFolder(); return; }
    if (working_) return;
    const QString root = folder_;
    setBusy("gamesScanning", {}, -1);
    work([this, root] { scanThread(root); });
}

void GameLibrary::scanThread(QString root) {
    std::vector<Entry> found;
    QSet<QString> skip;   // tracks a .cue or .m3u lists (shown once, by the .cue / .m3u)
    QStringList files;
    QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
    while (it.hasNext() && !quit_) {
        files << it.next();
        if (files.size() % 500 == 0) setBusy("gamesScanning", QString::number(files.size()), -1);
    }
    static const QRegularExpression fileRe(R"re(^\s*FILE\s+"?([^"]+?)"?\s+\S+\s*$)re", QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
    for (const auto& p : files) {
        const QString ext = QFileInfo(p).suffix().toLower();
        if (ext == "cue" || ext == "m3u") {
            QFile f(p);
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QString text = QString::fromUtf8(f.read(65536));
            const QDir dir = QFileInfo(p).dir();
            if (ext == "cue") {
                for (auto m = fileRe.globalMatch(text); m.hasNext();) skip.insert(QDir::cleanPath(dir.filePath(m.next().captured(1))));
            } else {
                for (const auto& line : text.split('\n'))
                    if (const QString l = line.trimmed(); !l.isEmpty() && !l.startsWith('#')) skip.insert(QDir::cleanPath(dir.filePath(l)));
            }
        }
    }
    int n = 0;
    for (const auto& p : files) {
        if (quit_) return;
        if (++n % 200 == 0) setBusy("gamesScanning", QString::number(n) + " / " + QString::number(files.size()), n * 100 / int(files.size()));
        if (skip.contains(QDir::cleanPath(p))) continue;
        const QString sys = detectSystem(p, root);
        if (sys.isEmpty()) continue;
        const QString stem = QFileInfo(p).completeBaseName();
        const QStringList r = regionsOf(stem);
        QString region;
        for (const auto& x : r)
            if (x == "usa" || x == "europe" || x == "japan" || x == "world" || x == "germany" || x == "france" || x == "spain"
                || x == "italy" || x == "sweden" || x == "korea" || x == "brazil" || x == "australia") { region = x; break; }
        found.push_back({p, sys, displayTitle(stem), region});
    }
    std::sort(found.begin(), found.end(), [](const Entry& a, const Entry& b) {
        const int c = QString::compare(a.title, b.title, Qt::CaseInsensitive);
        return c != 0 ? c < 0 : a.path < b.path;
    });
    {
        std::lock_guard l(m_);
        entries_ = std::move(found);
    }
    save();
    QMetaObject::invokeMethod(this, [this] {
        const auto sys = systems();
        bool have = false;
        for (const auto& v : sys) if (v.toMap().value("id").toString() == current_) have = true;
        if (!have && !sys.isEmpty()) current_ = sys.first().toMap().value("id").toString();
        emit changed();
        int count = 0;
        { std::lock_guard l(m_); count = int(entries_.size()); }
        emit message(count ? "gamesFound" : "gamesNoneFound", QString::number(count), count == 0);
    }, Qt::QueuedConnection);
}

QVariantList GameLibrary::systems() const {
    QHash<QString, int> counts;
    {
        std::lock_guard l(m_);
        for (const auto& e : entries_) counts[e.system]++;
    }
    QVariantList out;
    const QStringList recent = QSettings().value("games/recent").toStringList();
    if (!recent.isEmpty()) out << QVariantMap{{"id", "recent"}, {"name", ""}, {"count", recent.size()}};
    for (const auto& s : gameSystems())
        if (counts.value(s.id)) out << QVariantMap{{"id", s.id}, {"name", s.name}, {"count", counts.value(s.id)}};
    return out;
}

QString GameLibrary::coverPath(const QString& systemId, const QString& romPath) const {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/games/covers/" + systemId + "/";
    const QString stem = QFileInfo(romPath).completeBaseName();
    if (QFile::exists(base + stem + ".user.png")) return base + stem + ".user.png";
    return base + thumbnailName(stem) + ".png";
}

QVariantMap GameLibrary::itemFor(const Entry& e) const {
    const QString cover = coverPath(e.system, e.path);
    const GameSystem* s = gameSystem(e.system);
    return QVariantMap{{"key", e.path}, {"title", e.title},
                       {"sub", QString(s ? s->name : "") + (e.region.isEmpty() ? QString()
                                : " · " + (e.region == "usa" ? QStringLiteral("USA") : e.region.left(1).toUpper() + e.region.mid(1)))},
                       {"image", QFile::exists(cover) ? QUrl::fromLocalFile(cover).toString() : QString()},
                       {"kind", "game"}, {"system", e.system}};
}

QVariantList GameLibrary::items() const {
    QVariantList out;
    if (current_ == "recent") {
        const QStringList recent = QSettings().value("games/recent").toStringList();
        std::lock_guard l(m_);
        for (const auto& p : recent)
            for (const auto& e : entries_) if (e.path == p) { out << itemFor(e); break; }
        return out;
    }
    std::lock_guard l(m_);
    for (const auto& e : entries_) if (e.system == current_) out << itemFor(e);
    return out;
}

void GameLibrary::select(const QString& systemId) {
    current_ = systemId;
    QSettings().setValue("games/system", systemId);
    emit changed();
}

QString GameLibrary::biosHint(const QString& systemId) const {
    const GameSystem* s = gameSystem(systemId);
    return s ? QString::fromLatin1(s->bios) : QString();
}

// the cores' folders: VeyAmux's own downloads, then RetroArch's (Flatpak, native, Steam), then the system's
QString GameLibrary::coreFor(const QString& systemId, QString& error, bool download) {
    const GameSystem* s = gameSystem(systemId);
    if (!s) { error = "gamesUnknownSystem"; return {}; }
    const QString own = dataDir("cores");
    const QStringList dirs = {own,
                              QDir::homePath() + "/.var/app/org.libretro.RetroArch/config/retroarch/cores",
                              QDir::homePath() + "/.config/retroarch/cores",
                              QDir::homePath() + "/.local/share/Steam/steamapps/common/RetroArch/cores",
                              "/usr/lib64/libretro", "/usr/lib/libretro", "/usr/lib/x86_64-linux-gnu/libretro", "/usr/local/lib/libretro"};
    const QStringList cores = QString::fromLatin1(s->cores).split(',');
    for (const auto& c : cores)
        for (const auto& d : dirs) {
            const QString p = d + "/" + c + "_libretro.so";
            if (!QFile::exists(p)) continue;
            if (!retro::coreLoads(p.toStdString())) continue;   // does not load here (built for another system)
            return p;
        }
    if (!download) { error = "gamesNoCore"; return {}; }
    // the first choice from libretro's build server (nightly builds, the same ones RetroArch's core downloader gets)
    const QString arch = QSysInfo::currentCpuArchitecture() == "arm64" ? "aarch64" : "x86_64";
    for (const auto& c : cores) {
        const QString url = QStringLiteral("https://buildbot.libretro.com/nightly/linux/%1/latest/%2_libretro.so.zip").arg(arch, c);
        const QString zip = own + "/" + c + ".download.zip";
        QString err;
        setBusy("gamesFetchingCore", c, 0);
        if (!httpsDownload(url, zip, 128ll << 20, [this, c](int p) { setBusy("gamesFetchingCore", c, p); }, err)) {
            QFile::remove(zip);
            error = "gamesCoreDownloadFailed";
            qWarning("Games: could not fetch the %s core: %s", qPrintable(c), qPrintable(err));
            continue;
        }
        std::vector<uint8_t> so;
        std::string e;
        const bool ok = retro::zipRead(zip.toStdString(), (c + "_libretro.so").toStdString(), so, e);
        QFile::remove(zip);
        if (!ok) { error = "gamesCoreDownloadFailed"; qWarning("Games: %s", e.c_str()); continue; }
        const QString p = own + "/" + c + "_libretro.so";
        QFile f(p + ".tmp");
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(reinterpret_cast<const char*>(so.data()), qint64(so.size())) != qint64(so.size())) {
            error = "gamesCoreDownloadFailed";
            continue;
        }
        f.close();
        QFile::remove(p);
        QFile::rename(p + ".tmp", p);
        qInfo("Games: fetched the %s core (%lld KB)", qPrintable(c), qlonglong(so.size() / 1024));
        if (retro::coreLoads(p.toStdString())) { error.clear(); return p; }
        error = "gamesCoreBroken";
    }
    return {};
}

void GameLibrary::play(const QString& path) {
    QString sys, title;
    {
        std::lock_guard l(m_);
        for (const auto& e : entries_) if (e.path == path) { sys = e.system; title = e.title; }
    }
    if (sys.isEmpty()) return;
    // most recent first, at most 12
    QStringList recent = QSettings().value("games/recent").toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > 12) recent.removeLast();
    QSettings().setValue("games/recent", recent);
    QString err;
    const QString core = coreFor(sys, err, false);
    if (!core.isEmpty()) { emit launch(core, path, title, sys); emit changed(); return; }
    // fetched in the background, then started
    if (working_) return;
    setBusy("gamesFetchingCore", {}, 0);
    work([this, sys, path, title] {
        QString e;
        const QString c = coreFor(sys, e, true);
        QMetaObject::invokeMethod(this, [this, c, e, path, title, sys] {
            if (c.isEmpty()) { emit message(e, QString::fromLatin1(gameSystem(sys)->cores).section(',', 0, 0), true); return; }
            emit launch(c, path, title, sys);
            emit changed();
        }, Qt::QueuedConnection);
    });
}

// ------------------------------------------------------------------ box art

void GameLibrary::downloadCovers() {
    std::vector<Entry> todo;
    {
        std::lock_guard l(m_);
        for (const auto& e : entries_) if (!QFile::exists(coverPath(e.system, e.path))) todo.push_back(e);
    }
    if (todo.empty()) { emit message("gamesCoversDone", "0", false); return; }
    if (working_) return;
    setBusy("gamesFetchingCovers", QStringLiteral("0 / %1").arg(todo.size()), 0);
    work([this, todo] {
        int got = 0, n = 0;
        QHash<QString, QStringList> indexes;   // per system: every box art name libretro has
        const QString cache = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/games/covers/";
        for (const auto& e : todo) {
            if (quit_) return;
            ++n;
            setBusy("gamesFetchingCovers", QStringLiteral("%1 / %2").arg(n).arg(todo.size()), n * 100 / int(todo.size()));
            const GameSystem* s = gameSystem(e.system);
            if (!s) continue;
            const QString thumbs = QString::fromLatin1(s->thumbs);
            const QString stem = QFileInfo(e.path).completeBaseName();
            const QString dest = coverPath(e.system, e.path);
            QDir().mkpath(QFileInfo(dest).absolutePath());
            auto fetch = [&](const QString& name) {
                const QString url = "https://thumbnails.libretro.com/" + QString::fromUtf8(QUrl::toPercentEncoding(thumbs)) + "/Named_Boxarts/"
                                    + QString::fromUtf8(QUrl::toPercentEncoding(name)) + ".png";
                QString err;
                if (httpsDownload(url, dest + ".part", 8 << 20, {}, err)) { QFile::remove(dest); return QFile::rename(dest + ".part", dest); }
                // the same files on GitHub (libretro-thumbnails), when the thumbnail server is down
                QString repo = thumbs;
                repo.replace(' ', '_');
                const QString gh = "https://raw.githubusercontent.com/libretro-thumbnails/" + QString::fromUtf8(QUrl::toPercentEncoding(repo))
                                   + "/master/Named_Boxarts/" + QString::fromUtf8(QUrl::toPercentEncoding(name)) + ".png";
                if (httpsDownload(gh, dest + ".part", 8 << 20, {}, err)) { QFile::remove(dest); return QFile::rename(dest + ".part", dest); }
                QFile::remove(dest + ".part");
                return false;
            };
            if (fetch(thumbnailName(stem))) { ++got; continue; }
            // not under the file's own name: the closest name in libretro's list (the list is fetched once per system
            // and kept for a week)
            if (!indexes.contains(e.system)) {
                QStringList names;
                const QString idx = cache + e.system + "/index.txt";
                const QFileInfo ii(idx);
                if (ii.exists() && ii.lastModified().daysTo(QDateTime::currentDateTime()) < 7) {
                    QFile f(idx);
                    if (f.open(QIODevice::ReadOnly)) names = QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
                }
                if (names.isEmpty()) {
                    QString err;
                    const QString tmp = idx + ".part";
                    const QString url = "https://thumbnails.libretro.com/" + QString::fromUtf8(QUrl::toPercentEncoding(thumbs)) + "/Named_Boxarts/";
                    if (httpsDownload(url, tmp, 32 << 20, {}, err)) {
                        QFile f(tmp);
                        if (f.open(QIODevice::ReadOnly)) {
                            static const QRegularExpression href(R"re(href="([^"/?]+)\.png")re");
                            const QString html = QString::fromUtf8(f.readAll());
                            for (auto m = href.globalMatch(html); m.hasNext();)
                                names << QUrl::fromPercentEncoding(m.next().captured(1).toUtf8()).replace("&amp;", "&");
                        }
                    }
                    QFile::remove(tmp);
                    if (!names.isEmpty()) {
                        QFile f(idx);
                        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(names.join('\n').toUtf8());
                    }
                }
                indexes.insert(e.system, names);
            }
            const QString best = matchThumbnail(stem, indexes.value(e.system));
            if (!best.isEmpty() && fetch(best)) ++got;
        }
        QMetaObject::invokeMethod(this, [this, got] {
            ++coverVersion_;
            emit coversChanged();
            emit changed();
            emit message("gamesCoversDone", QString::number(got), false);
        }, Qt::QueuedConnection);
    });
}

void GameLibrary::setCover(const QString& path) {
    QString sys;
    {
        std::lock_guard l(m_);
        for (const auto& e : entries_) if (e.path == path) sys = e.system;
    }
    if (sys.isEmpty()) return;
    const QString pic = QFileDialog::getOpenFileName(nullptr, QString(), QDir::homePath(), "Images (*.png *.jpg *.jpeg *.webp)");
    if (pic.isEmpty()) return;
    const QImage img(pic);
    if (img.isNull()) return;
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/games/covers/" + sys + "/";
    QDir().mkpath(base);
    img.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(base + QFileInfo(path).completeBaseName() + ".user.png");
    ++coverVersion_;
    emit coversChanged();
    emit changed();
}

} // namespace veyamux
