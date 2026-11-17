// VeyAmux for Linux: non-Steam game shortcut (see SteamShortcut.h).
#include "SteamShortcut.h"
#include "Sandbox.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

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

Result addShortcut(const QString& exe, const QString& icon, const QString& launchOptions) {
    Result r;
    const QStringList roots = steamRoots();
    if (roots.isEmpty()) { r.message = "Steam was not found"; return r; }
    if (steamRunning()) { r.message = "Close Steam first"; return r; }
    const QByteArray exeQ = "\"" + exe.toUtf8() + "\"";
    const QByteArray dirQ = "\"" + QFileInfo(exe).absolutePath().toUtf8() + "/\"";
    const QByteArray name = "VeyAmux";
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
            ++r.accounts;
        }
    }
    r.ok = r.accounts > 0;
    if (r.ok) r.message.clear();
    else if (r.message.isEmpty()) r.message = "No Steam account found (start Steam once and log in)";
    return r;
}

} // namespace veyamux::steam
