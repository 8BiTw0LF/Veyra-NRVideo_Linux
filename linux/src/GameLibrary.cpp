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
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
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
        // drawn with OpenGL (RetroGl.h), except the DS emulators
        {"n64", "Nintendo 64", "Nintendo - Nintendo 64", "n64,nintendo64,nintendonintendo64", "n64,z64,v64", "n64,z64,v64,zip,7z",
         "mupen64plus_next,parallel_n64", true, ""},
        {"nds", "Nintendo DS", "Nintendo - Nintendo DS", "nds,ds,nintendods,nintendonintendods", "nds", "nds,zip,7z",
         "melondsds,melonds,desmume", false, ""},
        {"dc", "Dreamcast", "Sega - Dreamcast", "dc,dreamcast,segadreamcast", "gdi,cdi", "gdi,cdi,chd,cue,m3u", "flycast", true,
         "dc/dc_boot.bin, dc/dc_flash.bin (optional: Flycast has its own)"},
        {"psp", "PSP", "Sony - PlayStation Portable", "psp,playstationportable,sonyplaystationportable", "cso", "iso,cso,pbp,chd",
         "ppsspp", true, ""},
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
    if (head.contains("SEGA SEGAKATANA")) return "dc";        // Dreamcast
    if (head.contains("PSP GAME")) return "psp";              // the PSP's UMD volume name
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
            if (!listHas(s.folderExts, ext) && ext != "zip" && ext != "7z" && ext != "rar") return {};   // a file the system's games never are (a readme, a cover)
            static const QStringList discSystems = {"psx", "segacd", "saturn", "pcecd", "dc", "psp"};
            if ((ext == "cue" || ext == "chd" || ext == "iso" || ext == "m3u") && !discSystems.contains(QString(s.id))) return {};
            return s.id;
        }
    }
    // the file type alone
    for (const auto& s : gameSystems()) if (listHas(s.exts, ext)) return s.id;
    if (ext == "zip" || ext == "7z" || ext == "rar") return zipSystem(path);
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
        entries_.push_back({g.value("path").toString(), g.value("system").toString(), g.value("title").toString(), g.value("region").toString(),
                            g.value("thumb").toString()});
    }
}

void GameLibrary::save() const {
    QJsonArray a;
    {
        std::lock_guard l(m_);
        for (const auto& e : entries_)
            a.append(QJsonObject{{"path", e.path}, {"system", e.system}, {"title", e.title}, {"region", e.region}, {"thumb", e.thumb}});
    }
    QFile f(dataDir({}) + "/games.json");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(QJsonObject{{"folder", folder_}, {"games", a}}).toJson(QJsonDocument::Compact));
}

void GameLibrary::rescan() {
    QFile::remove(dataDir({}) + "/covers-missing.txt");   // a new look for box art that was not found
    if (folder_.isEmpty()) { chooseFolder(); return; }
    if (working_) return;
    const QString root = folder_;
    setBusy("gamesScanning", {}, -1);
    work([this, root] { scanThread(root); });
}

// Arcade games are named by their short ROM set name (sf2.zip). FinalBurn Neo's list (its DAT, fetched once from
// libretro's GitHub and kept as a small table) gives the full title, which is also the name of the box art, and tells
// which zips are no games (BIOS and device sets such as neogeo.zip).
namespace {
struct ArcadeName { QString title; bool bios = false; };
QHash<QString, ArcadeName> arcadeNames(const QString& dir, bool download) {
    QHash<QString, ArcadeName> out;
    const QString table = dir + "/arcade-names.tsv";
    QFile f(table);
    if (!f.exists() && download) {
        const QString dat = dir + "/fbneo.dat.part";
        QString err;
        if (httpsDownload("https://raw.githubusercontent.com/libretro/FBNeo/master/dats/"
                          "FinalBurn%20Neo%20(ClrMame%20Pro%20XML%2C%20Arcade%20only).dat", dat, 64ll << 20, {}, err)) {
            QFile d(dat);
            if (d.open(QIODevice::ReadOnly)) {
                const QString xml = QString::fromUtf8(d.readAll());
                static const QRegularExpression game(R"re(<game name="([^"]+)"([^>]*)>\s*<description>([^<]*)</description>)re");
                QStringList lines;
                for (auto m = game.globalMatch(xml); m.hasNext();) {
                    const auto g = m.next();
                    QString title = g.captured(3);
                    title.replace("&amp;", "&").replace("&apos;", "'").replace("&quot;", "\"").replace("&lt;", "<").replace("&gt;", ">");
                    const bool bios = g.captured(2).contains("isbios=\"yes\"") || g.captured(2).contains("isdevice=\"yes\"");
                    lines << g.captured(1) + '\t' + title + '\t' + (bios ? "b" : "");
                }
                if (lines.size() > 1000 && f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { f.write(lines.join('\n').toUtf8()); f.close(); }
            }
        } else {
            qWarning("Games: could not fetch FinalBurn Neo's game list: %s", qPrintable(err));
        }
        QFile::remove(dat);
    }
    if (f.open(QIODevice::ReadOnly)) {
        for (const auto& line : QString::fromUtf8(f.readAll()).split('\n')) {
            const QStringList p = line.split('\t');
            if (p.size() >= 2) out.insert(p[0], {p[1], p.size() > 2 && p[2] == "b"});
        }
    }
    return out;
}
} // namespace

bool GameLibrary::isBiosFile(const QString& path, const QString& root) {
    const QString stem = QFileInfo(path).completeBaseName().toLower();
    if (stem.contains("[bios]") || stem == "bios" || stem.endsWith(" bios") || stem.endsWith("_bios")) return true;
    if (stem.startsWith("bios") && stem.size() > 4 && !stem[4].isLetter()) return true;   // bios_E, bios-u, bios (Japan)
    static const QRegularExpression names(
        R"re(^(syscard\d*[uj]?|gecard|gexpress|sgb\d?(\b.*)?|super game boy.*|game ?genie.*|gamegenie.*|disksys|lynxboot|scph\d+.*|ps1_rom|)re"
        R"re(mpr-\d+.*|sega_101|saturn_bios|kick\d.*|firmware.*|dmg_boot|cgb_boot|gba_bios|gb_bios|gbc_bios|sgb_bios|.*_bios|)re"
        R"re(.*\(bios\).*|bios_cd_[euj]|bios[_ ][euj]|bios_md|bios_ms|bios_gg|segacd_.*bios.*|mpr-17933|sega cd bios.*)$)re");
    if (names.match(stem).hasMatch()) return true;
    // a folder of BIOS files inside the games folder
    for (const auto& part : QDir(root).relativeFilePath(QFileInfo(path).absolutePath()).toLower().split('/'))
        if (part == "bios" || part == "bioses" || part == "firmware") return true;
    return false;
}

// BIOS files found among the games: copied to the BIOS folder when it does not have them yet
void GameLibrary::copyBios(const QStringList& files) {
    const QString dir = biosFolder();
    for (const auto& f : files) {
        const QFileInfo fi(f);
        if (fi.size() > (64ll << 20)) continue;
        const QString dest = dir + "/" + fi.fileName();
        if (!QFile::exists(dest) && QFile::copy(f, dest)) qInfo("Games: BIOS file %s copied to the BIOS folder", qPrintable(fi.fileName()));
    }
}

// Games on several discs ("Metal Gear Solid (USA) (Disc 1).cue", "(Disc 2)") become one game: VeyAmux writes a
// playlist (.m3u) with the discs in order, which the emulator opens; the disc is changed from the menu.
void GameLibrary::groupDiscs(std::vector<Entry>& found) {
    static const QRegularExpression disc(R"re(\s*[\(\[]\s*(?:disc|disk|cd)\s*(\d+)(?:\s*of\s*\d+)?\s*[\)\]])re",
                                         QRegularExpression::CaseInsensitiveOption);
    struct Group { std::vector<std::pair<int, size_t>> discs; };
    QHash<QString, Group> groups;
    for (size_t i = 0; i < found.size(); ++i) {
        const QString stem = QFileInfo(found[i].path).completeBaseName();
        const auto m = disc.match(stem);
        if (!m.hasMatch() || QFileInfo(found[i].path).suffix().toLower() == "m3u") continue;
        QString base = stem;
        base.remove(m.capturedStart(), m.capturedLength());
        groups[found[i].system + '\n' + QFileInfo(found[i].path).absolutePath().section('/', 0, -1) + '\n' + base.trimmed()]
            .discs.push_back({m.captured(1).toInt(), i});
    }
    std::vector<bool> drop(found.size(), false);
    const QString m3uDir = dataDir("playlists");
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        auto& d = it.value().discs;
        if (d.size() < 2) continue;
        std::sort(d.begin(), d.end());
        const QString base = it.key().section('\n', 2);
        const QString sys = it.key().section('\n', 0, 0);
        QDir().mkpath(m3uDir + "/" + sys);
        const QString m3u = m3uDir + "/" + sys + "/" + base + ".m3u";
        QStringList lines;
        for (const auto& [num, idx] : d) lines << found[idx].path;
        QFile f(m3u);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) continue;
        f.write((lines.join('\n') + '\n').toUtf8());
        f.close();
        Entry e = found[d.front().second];
        e.path = m3u;
        e.title = displayTitle(base);
        // box art: libretro names it after the first disc ("Metal Gear Solid (USA) (Disc 1) (Rev 1)")
        if (e.thumb.isEmpty()) e.thumb = QFileInfo(found[d.front().second].path).completeBaseName();
        for (const auto& [num, idx] : d) drop[idx] = true;
        found.push_back(e);
    }
    std::vector<Entry> keep;
    for (size_t i = 0; i < found.size(); ++i) if (i >= drop.size() || !drop[i]) keep.push_back(found[i]);
    found = std::move(keep);
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
    QStringList biosFiles;
    QHash<QString, ArcadeName> arcade;
    bool arcadeLoaded = false;
    for (const auto& p : files) {
        if (quit_) return;
        if (++n % 200 == 0) setBusy("gamesScanning", QString::number(n) + " / " + QString::number(files.size()), n * 100 / int(files.size()));
        if (skip.contains(QDir::cleanPath(p))) continue;
        // a BIOS folder inside the games folder: its files go to the BIOS folder too
        if (const QString rel = QDir(root).relativeFilePath(p).toLower(); rel.startsWith("bios/") || rel.contains("/bios/")
                                                                           || rel.startsWith("firmware/") || rel.contains("/firmware/")) {
            const QString ext = QFileInfo(p).suffix().toLower();
            if (ext != "txt" && ext != "md" && ext != "pdf" && ext != "jpg" && ext != "png" && ext != "nfo") biosFiles << p;
            continue;
        }
        const QString sys = detectSystem(p, root);
        if (sys.isEmpty()) continue;
        const QString stem = QFileInfo(p).completeBaseName();
        // BIOS and add-on files (bios_E.sms, syscard3.pce, SGB1.sfc, gamegenie.nes, "[BIOS] ...") are no games:
        // they go to the BIOS folder instead, where the emulators look for them
        if (isBiosFile(p, root)) { biosFiles << p; continue; }
        const QStringList r = regionsOf(stem);
        QString region;
        for (const auto& x : r)
            if (x == "usa" || x == "europe" || x == "japan" || x == "world" || x == "germany" || x == "france" || x == "spain"
                || x == "italy" || x == "sweden" || x == "korea" || x == "brazil" || x == "australia") { region = x; break; }
        if (sys == "arcade") {
            // BIOS sets that are in other lists than FinalBurn Neo's arcade one
            static const QSet<QString> bioses = {"neogeo", "neocdz", "pgm", "skns", "cps3", "decocass", "isgsm", "nmk004", "ym2608",
                                                 "qsound", "hng64", "naomi", "awbios", "stvbios", "coleco", "msx", "spectrum"};
            if (bioses.contains(stem.toLower())) continue;
            if (!arcadeLoaded) { arcade = arcadeNames(dataDir({}), true); arcadeLoaded = true; }
            const auto it = arcade.constFind(stem.toLower());
            if (it != arcade.constEnd()) {
                if (it->bios) continue;   // a BIOS or device set, no game
                found.push_back({p, sys, displayTitle(it->title), region, it->title});
                continue;
            }
        }
        found.push_back({p, sys, displayTitle(stem), region, QString()});
    }
    copyBios(biosFiles);
    groupDiscs(found);
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

// PNG, JPEG or WebP (box art fetched by an older debug build can be a link's text instead)
bool GameLibrary::isPicture(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray h = f.read(12);
    return h.startsWith("\x89PNG") || h.startsWith("\xff\xd8\xff") || (h.startsWith("RIFF") && h.mid(8, 4) == "WEBP");
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
                       {"image", isPicture(cover) ? QUrl::fromLocalFile(cover).toString() : QString()},
                       {"kind", "game"}, {"system", e.system},
                       // quit before: "Continue" from where it was
                       {"canResume", QFile::exists(QString::fromStdString(retro::autoStatePath(
                            (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/games/states/" + e.system).toStdString(),
                            e.path.toStdString())))}};
}

QVariantList GameLibrary::recentGames() const {
    QVariantList out;
    const QStringList recent = QSettings().value("games/recent").toStringList();
    std::lock_guard l(m_);
    for (const auto& p : recent) {
        for (const auto& e : entries_) if (e.path == p) { out << itemFor(e); break; }
        if (out.size() >= 6) break;
    }
    return out;
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
    QStringList cores = QString::fromLatin1(s->cores).split(',');
    // the one chosen for this system in the menu goes first (games/<system>/core)
    if (const QString chosen = QSettings().value("games/" + systemId + "/core").toString(); cores.contains(chosen)) {
        cores.removeAll(chosen);
        cores.prepend(chosen);
    }
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

void GameLibrary::play(const QString& path, bool resume) {
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
    if (!core.isEmpty()) { emit launch(core, path, title, sys, resume); emit changed(); return; }
    // fetched in the background, then started
    if (working_) return;
    setBusy("gamesFetchingCore", {}, 0);
    work([this, sys, path, title, resume] {
        QString e;
        const QString c = coreFor(sys, e, true);
        QMetaObject::invokeMethod(this, [this, c, e, path, title, sys, resume] {
            if (c.isEmpty()) { emit message(e, QString::fromLatin1(gameSystem(sys)->cores).section(',', 0, 0), true); return; }
            emit launch(c, path, title, sys, resume);
            emit changed();
        }, Qt::QueuedConnection);
    });
}

// ------------------------------------------------------------------ box art

void GameLibrary::downloadCovers() {
    std::vector<Entry> todo;
    {
        std::lock_guard l(m_);
        for (const auto& e : entries_) if (!isPicture(coverPath(e.system, e.path))) todo.push_back(e);
    }
    // games libretro has no box art for were looked up before: not again (until the folder is scanned again)
    const QString missFile = dataDir({}) + "/covers-missing.txt";
    QSet<QString> missing;
    {
        QFile f(missFile);
        if (f.open(QIODevice::ReadOnly)) for (const auto& l : QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts)) missing.insert(l);
    }
    todo.erase(std::remove_if(todo.begin(), todo.end(), [&](const Entry& e) { return missing.contains(e.path); }), todo.end());
    if (todo.empty()) { emit message("gamesCoversDone", "0", false); return; }
    if (working_) return;
    setBusy("gamesFetchingCovers", QStringLiteral("0 / %1").arg(todo.size()), 0);
    work([this, todo, missFile] {
        int got = 0, n = 0;
        QStringList missed, unsure;
        QHash<QString, QStringList> indexes;   // per system: every box art name libretro has
        const QString cache = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/games/covers/";
        for (const auto& e : todo) {
            if (quit_) return;
            ++n;
            setBusy("gamesFetchingCovers", QStringLiteral("%1 / %2").arg(n).arg(todo.size()), n * 100 / int(todo.size()));
            const GameSystem* s = gameSystem(e.system);
            if (!s) continue;
            // arcade box art is also in MAME's set
            const QStringList sets = e.system == "arcade" ? QStringList{QString::fromLatin1(s->thumbs), "MAME"} : QStringList{QString::fromLatin1(s->thumbs)};
            QString thumbs = sets.first();
            const QString stem = e.thumb.isEmpty() ? QFileInfo(e.path).completeBaseName() : e.thumb;
            const QString dest = coverPath(e.system, e.path);
            QDir().mkpath(QFileInfo(dest).absolutePath());
            // a downloaded file is kept only when it is a picture. On GitHub many box art files are symbolic links
            // to another name ("Star Fox (USA) (Rev 2).png" -> "Star Fox (USA).png"), which come back as that name in
            // a few bytes of text: then that file is fetched instead.
            auto take = [&](const QString& url, int depth, auto&& self) -> bool {
                QString err;
                if (!httpsDownload(url, dest + ".part", 8 << 20, {}, err)) { QFile::remove(dest + ".part"); return false; }
                if (isPicture(dest + ".part")) { QFile::remove(dest); return QFile::rename(dest + ".part", dest); }
                QFile f(dest + ".part");
                QString link;
                if (f.open(QIODevice::ReadOnly) && f.size() < 512) link = QString::fromUtf8(f.readAll()).trimmed();
                f.close();
                QFile::remove(dest + ".part");
                if (depth > 0 || link.isEmpty() || !link.endsWith(".png", Qt::CaseInsensitive) || link.contains('\n')) return false;
                const QString base = url.left(url.lastIndexOf('/') + 1);
                QString target = link.section('/', -1);
                return self(base + QString::fromUtf8(QUrl::toPercentEncoding(target)), depth + 1, self);
            };
            auto fetch = [&](const QString& name) {
                const QString url = "https://thumbnails.libretro.com/" + QString::fromUtf8(QUrl::toPercentEncoding(thumbs)) + "/Named_Boxarts/"
                                    + QString::fromUtf8(QUrl::toPercentEncoding(name)) + ".png";
                if (take(url, 0, take)) return true;
                // the same files on GitHub (libretro-thumbnails), when the thumbnail server is down
                QString repo = thumbs;
                repo.replace(' ', '_');
                const QString gh = "https://raw.githubusercontent.com/libretro-thumbnails/" + QString::fromUtf8(QUrl::toPercentEncoding(repo))
                                   + "/master/Named_Boxarts/" + QString::fromUtf8(QUrl::toPercentEncoding(name)) + ".png";
                return take(gh, 0, take);
            };
            bool done = false;
            for (const auto& set : sets) { thumbs = set; if ((done = fetch(thumbnailName(stem)))) break; }
            thumbs = sets.first();
            if (done) { ++got; continue; }
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
                    // the thumbnail server's list is not there: the same list from GitHub (two calls of its API)
                    if (names.isEmpty()) {
                        QString repo = thumbs;
                        repo.replace(' ', '_');
                        const QString api = "https://api.github.com/repos/libretro-thumbnails/" + QString::fromUtf8(QUrl::toPercentEncoding(repo)) + "/git/trees/";
                        QString sha;
                        if (httpsDownload(api + "master", tmp, 4 << 20, {}, err)) {
                            QFile f(tmp);
                            if (f.open(QIODevice::ReadOnly))
                                for (const auto& v : QJsonDocument::fromJson(f.readAll()).object().value("tree").toArray())
                                    if (v.toObject().value("path").toString() == "Named_Boxarts") sha = v.toObject().value("sha").toString();
                        }
                        QFile::remove(tmp);
                        if (!sha.isEmpty() && httpsDownload(api + sha, tmp, 64 << 20, {}, err)) {
                            QFile f(tmp);
                            if (f.open(QIODevice::ReadOnly))
                                for (const auto& v : QJsonDocument::fromJson(f.readAll()).object().value("tree").toArray()) {
                                    const QString p = v.toObject().value("path").toString();
                                    if (p.endsWith(".png")) names << p.chopped(4);
                                }
                        }
                        QFile::remove(tmp);
                    }
                    if (!names.isEmpty()) {
                        QFile f(idx);
                        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(names.join('\n').toUtf8());
                    }
                }
                indexes.insert(e.system, names);
            }
            const QString best = matchThumbnail(stem, indexes.value(e.system));
            if (!best.isEmpty() && fetch(best)) ++got;
            else if (!indexes.value(e.system).isEmpty()) missed << e.path;   // (only when the list was there to look in)
            else unsure << e.path;
        }
        if (got > 0) missed += unsure;   // the network worked: not found is not found
        if (!missed.isEmpty()) {
            QFile f(missFile);
            if (f.open(QIODevice::Append)) f.write((missed.join('\n') + '\n').toUtf8());
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

// ------------------------------------------------------------------ RetroAchievements

QString GameLibrary::raUser() const { return QSettings().value("games/ra/user").toString(); }

// the password goes to retroachievements.org once; the token it answers with is kept (the settings key has "token"
// in it, so reports leave it out) and given to the emulator with each game
void GameLibrary::raSignIn(const QString& user, const QString& password) {
    if (user.trimmed().isEmpty() || password.isEmpty() || raBusy_) return;
    if (!net_) net_ = new QNetworkAccessManager(this);
    raBusy_ = true;
    emit raChanged();
    QNetworkRequest rq(QUrl("https://retroachievements.org/dorequest.php"));
    rq.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    rq.setHeader(QNetworkRequest::UserAgentHeader, "VeyAmux/2.1");
    rq.setTransferTimeout(20000);
    QUrlQuery form;
    form.addQueryItem("r", "login2");
    form.addQueryItem("u", user.trimmed());
    form.addQueryItem("p", password);
    QNetworkReply* r = net_->post(rq, form.toString(QUrl::FullyEncoded).toUtf8());
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        raBusy_ = false;
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        if (o.value("Success").toBool() && !o.value("Token").toString().isEmpty()) {
            QSettings q;
            q.setValue("games/ra/user", o.value("User").toString());
            q.setValue("games/ra/token", o.value("Token").toString());
            emit message("raSignedIn", o.value("User").toString(), false);
        } else {
            const QString why = o.value("Error").toString();
            emit message("raSignInFailed", why.isEmpty() ? r->errorString() : why, true);
        }
        emit raChanged();
    });
}

void GameLibrary::raSignOut() {
    QSettings q;
    q.remove("games/ra/user");
    q.remove("games/ra/token");
    emit raChanged();
}

} // namespace veyamux

