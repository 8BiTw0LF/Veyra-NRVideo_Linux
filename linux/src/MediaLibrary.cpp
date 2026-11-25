// VeyAmux for Linux: the Media page (see MediaLibrary.h).
#include "MediaLibrary.h"
#include "PlayerBridge.h"
#include <QScreen>

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <memory>

namespace veyamux {

namespace {

const QStringList kVideoExts = {"mp4", "mkv", "webm", "mov", "avi", "m4v", "ts", "m2ts", "mts", "wmv", "flv", "mpg", "mpeg", "vob", "ogv"};
const char* kVersion = "2.1";

QString hashOf(const QString& s) { return QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha1).toHex().left(16)); }
QString minutes(double sec) { return sec > 0 ? QStringLiteral("%1 min").arg(std::max(1, int(std::lround(sec / 60)))) : QString(); }

QString artDir(const QString& source) {
    const QString d = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/media-art/" + source;
    QDir().mkpath(d);
    return d;
}

} // namespace

MediaLibrary::MediaLibrary(PlayerBridge* player, QObject* parent) : QObject(parent), player_(player) {
    net_.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    current_ = QSettings().value("media/current", "local").toString();
    if (source(current_).id.isEmpty()) current_ = "local";
    levels_ = {{QString(), "root", QString(), QString()}};
    imagesChanged_.setSingleShot(true);
    imagesChanged_.setInterval(150);
    connect(&imagesChanged_, &QTimer::timeout, this, [this] { emit imagesChanged(); });
    loginPoll_.setInterval(2000);
    // where the video is, every 10 s (the server's "continue watching"; local files: the settings)
    progress_.setInterval(10000);
    connect(&progress_, &QTimer::timeout, this, [this] { report("progress"); });
    // the video is about to be closed or replaced: its position is still there
    connect(player_, &PlayerBridge::closing, this, [this] { if (!playing_.source.isEmpty() && playing_.started) report("stopped"); });
    connect(player_, &PlayerBridge::mediaChanged, this, [this] {
        if (!playing_.source.isEmpty() && playing_.started && !player_->hasMedia()) report("stopped");
    });
    connect(player_, &PlayerBridge::ended, this, [this] { if (!playing_.source.isEmpty()) report("ended"); });
}

MediaLibrary::~MediaLibrary() = default;

// ------------------------------------------------------------------ sources

QString MediaLibrary::deviceId() const {
    QSettings q;
    QString id = q.value("media/deviceId").toString();
    if (id.isEmpty()) { id = QUuid::createUuid().toString(QUuid::WithoutBraces); q.setValue("media/deviceId", id); }
    return id;
}

QList<MediaLibrary::Source> MediaLibrary::loadSources() const {
    QList<Source> out;
    out << Source{"local", "local", QString(), QString(), QString(), QString(), QString(), {}};
    QSettings q;
    q.beginGroup("media/servers");
    for (const auto& id : q.childGroups()) {
        q.beginGroup(id);
        Source s{id, q.value("kind").toString(), q.value("name").toString(), q.value("url").toString(), q.value("user").toString(),
                 q.value("token").toString(), q.value("machine").toString(), q.value("uris").toStringList()};
        q.endGroup();
        if (!s.kind.isEmpty()) out << s;
    }
    return out;
}

void MediaLibrary::saveSource(const Source& s) {
    QSettings q;
    q.beginGroup("media/servers/" + s.id);
    q.setValue("kind", s.kind);
    q.setValue("name", s.name);
    q.setValue("url", s.url);
    q.setValue("user", s.user);
    q.setValue("token", s.token);
    q.setValue("machine", s.machine);
    q.setValue("uris", s.uris);
    q.endGroup();
    emit sourcesChanged();
}

MediaLibrary::Source MediaLibrary::source(const QString& id) const {
    for (const auto& s : loadSources()) if (s.id == id) return s;
    return {};
}

QVariantList MediaLibrary::sources() const {
    QVariantList out;
    for (const auto& s : loadSources()) out << QVariantMap{{"id", s.id}, {"name", s.name}, {"kind", s.kind}};
    return out;
}

QVariantList MediaLibrary::crumbs() const {
    QVariantList out;
    for (int i = 1; i < levels_.size(); ++i) out << levels_[i].title;
    return out;
}

void MediaLibrary::removeSource(const QString& id) {
    if (id == "local") return;
    QSettings().remove("media/servers/" + id);
    QDir(artDir(id)).removeRecursively();
    if (current_ == id) select("local");
    emit sourcesChanged();
}

void MediaLibrary::addFolder() {
    const QString d = QFileDialog::getExistingDirectory(nullptr, QString(), QDir::homePath());
    if (d.isEmpty()) return;
    QStringList f = QSettings().value("media/folders").toStringList();
    if (!f.contains(d)) f << d;
    QSettings().setValue("media/folders", f);
    if (current_ == "local" && levels_.size() == 1) list();
}

// ------------------------------------------------------------------ browsing

void MediaLibrary::select(const QString& id) {
    if (source(id).id.isEmpty()) return;
    current_ = id;
    QSettings().setValue("media/current", id);
    levels_ = {{QString(), "root", QString(), QString()}};
    emit viewChanged();
    list();
}

void MediaLibrary::refresh() { list(); }

bool MediaLibrary::back() {
    if (levels_.size() <= 1) return false;
    levels_.removeLast();
    emit viewChanged();
    list();
    return true;
}

void MediaLibrary::openItem(int index) {
    if (index < 0 || index >= items_.size()) return;
    const QVariantMap it = items_[index].toMap();
    if (it.value("kind") == "action") {
        if (it.value("key") == "addFolder") addFolder();
        return;
    }
    if (it.value("kind") != "folder") return;
    levels_.append({it.value("title").toString(), it.value("level").toString(), it.value("key").toString(), it.value("extra").toString()});
    emit viewChanged();
    list();
}

void MediaLibrary::setItems(QVariantList items, int gen) {
    if (gen != gen_) return;
    items_ = std::move(items);
    emit itemsChanged();
}

void MediaLibrary::list() {
    ++gen_;
    imageQueue_.clear();
    items_.clear();
    images_.clear();
    emit itemsChanged();
    emit imagesChanged();
    const Source s = source(current_);
    const Level l = levels_.last();
    if (s.kind == "local") listLocal(l);
    else if (s.kind == "jellyfin") listJellyfin(s, l);
    else if (s.kind == "plex") listPlex(s, l);
}

// ---- the user's own folders: sub folders and video files; posters from the files beside them (Kodi / Jellyfin names)
void MediaLibrary::listLocal(const Level& l) {
    QVariantList out;
    auto pictureFor = [](const QDir& dir, const QString& stem) -> QString {
        QStringList names;
        if (!stem.isEmpty()) names << stem + "-poster" << stem + "-thumb" << stem;
        else names << "poster" << "folder" << "cover";
        for (const auto& n : names)
            for (const char* e : {".jpg", ".jpeg", ".png", ".webp"})
                if (QFile::exists(dir.filePath(n + e))) return QUrl::fromLocalFile(dir.filePath(n + e)).toString();
        return {};
    };
    if (l.kind == "root") {
        for (const auto& f : QSettings().value("media/folders").toStringList()) {
            const QDir d(f);
            out << QVariantMap{{"key", f}, {"title", d.dirName()}, {"sub", f}, {"kind", "folder"}, {"level", "dir"},
                               {"image", pictureFor(d, {})}, {"shape", "poster"}, {"removable", true}};
        }
        out << QVariantMap{{"key", "addFolder"}, {"title", ""}, {"kind", "action"}, {"shape", "poster"}};
        setItems(out, gen_);
        return;
    }
    const QDir dir(l.id);
    for (const auto& sub : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase)) {
        if (sub.fileName().startsWith('.')) continue;
        out << QVariantMap{{"key", sub.absoluteFilePath()}, {"title", sub.fileName()}, {"kind", "folder"}, {"level", "dir"},
                           {"image", pictureFor(QDir(sub.absoluteFilePath()), {})}, {"shape", "poster"}};
    }
    QSettings q;
    for (const auto& fi : dir.entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase)) {
        if (!kVideoExts.contains(fi.suffix().toLower())) continue;
        const double resume = q.value("media/resume/" + hashOf(fi.absoluteFilePath())).toDouble();
        const double dur = q.value("media/duration/" + hashOf(fi.absoluteFilePath())).toDouble();
        out << QVariantMap{{"key", fi.absoluteFilePath()}, {"title", fi.completeBaseName()},
                           {"sub", QStringLiteral("%1 MB").arg(fi.size() >> 20)}, {"kind", "video"},
                           {"image", pictureFor(dir, fi.completeBaseName())}, {"shape", "wide"},
                           {"resume", resume}, {"duration", dur}, {"progress", dur > 0 ? resume / dur : 0.0}};
    }
    setItems(out, gen_);
}

// ------------------------------------------------------------------ network

void MediaLibrary::request(QNetworkReply* r, std::function<void(int, const QByteArray&)> done, int timeoutMs) {
    ++pending_;
    emit busyChanged();
    auto* timer = new QTimer(r);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, r, [r] { r->abort(); });
    timer->start(timeoutMs);
    connect(r, &QNetworkReply::finished, this, [this, r, done = std::move(done)] {
        --pending_;
        emit busyChanged();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = r->readAll();
        if (r->error() != QNetworkReply::NoError && status == 0)
            qWarning("Media: %s %s: %s", r->request().url().host().toUtf8().constData(), r->request().url().path().toUtf8().constData(),
                     qPrintable(r->errorString().section(" - ", -1)));
        r->deleteLater();
        done(status, body);
    });
}

QString MediaLibrary::jellyfinAuth(const QString& token) const {
    QString a = QStringLiteral("MediaBrowser Client=\"VeyAmux\", Device=\"%1\", DeviceId=\"%2\", Version=\"%3\"")
                    .arg(QHostInfo::localHostName().remove('"'), deviceId(), kVersion);
    if (!token.isEmpty()) a += QStringLiteral(", Token=\"%1\"").arg(token);
    return a;
}

void MediaLibrary::plexHeaders(QNetworkRequest& rq, const QString& token) const {
    rq.setRawHeader("Accept", "application/json");
    rq.setRawHeader("X-Plex-Product", "VeyAmux");
    rq.setRawHeader("X-Plex-Version", kVersion);
    rq.setRawHeader("X-Plex-Client-Identifier", deviceId().toUtf8());
    rq.setRawHeader("X-Plex-Platform", "Linux");
    rq.setRawHeader("X-Plex-Device-Name", QHostInfo::localHostName().toUtf8());
    if (!token.isEmpty()) rq.setRawHeader("X-Plex-Token", token.toUtf8());
}

QNetworkReply* MediaLibrary::get(const Source& s, const QString& path, std::function<void(int, const QByteArray&)> done, int timeoutMs) {
    QString base = s.kind == "plex" ? plexUri_.value(s.id) : s.url;
    QNetworkRequest rq(QUrl(path.startsWith("https://") || path.startsWith("http://") ? path : base + path));
    if (s.kind == "jellyfin") { rq.setRawHeader("Authorization", jellyfinAuth(s.token).toUtf8()); rq.setRawHeader("Accept", "application/json"); }
    else if (s.kind == "plex") plexHeaders(rq, s.token);
    auto* r = net_.get(rq);
    request(r, std::move(done), timeoutMs);
    return r;
}

QNetworkReply* MediaLibrary::post(const Source& s, const QString& path, const QByteArray& json, std::function<void(int, const QByteArray&)> done) {
    QNetworkRequest rq(QUrl(s.url + path));
    rq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (s.kind == "jellyfin") { rq.setRawHeader("Authorization", jellyfinAuth(s.token).toUtf8()); rq.setRawHeader("Accept", "application/json"); }
    auto* r = net_.post(rq, json);
    request(r, std::move(done), 20000);
    return r;
}

// posters: fetched with the token in a header and kept on disk (the cache folder), at most four at a time
void MediaLibrary::loadImage(int gen, const QString& key, const Source& s, const QString& path, const QString& cacheName) {
    const QString file = artDir(s.id) + "/" + cacheName + ".jpg";
    if (QFile::exists(file)) {
        images_.insert(key, QUrl::fromLocalFile(file).toString());
        imagesChanged_.start();
        return;
    }
    imageQueue_.append({gen, key, s, path, file});
    pumpImages();
}

void MediaLibrary::pumpImages() {
    while (imagesRunning_ < 4 && !imageQueue_.isEmpty()) {
        const ImageJob j = imageQueue_.takeFirst();
        if (j.gen != gen_) continue;
        ++imagesRunning_;
        get(j.src, j.path, [this, j](int status, const QByteArray& body) {
            --imagesRunning_;
            if (status == 200 && !body.isEmpty()) {
                QFile f(j.file);
                if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { f.write(body); f.close(); }
                if (j.gen == gen_) {
                    images_.insert(j.key, QUrl::fromLocalFile(j.file).toString());
                    imagesChanged_.start();
                }
            }
            pumpImages();
        }, 30000);
    }
}

// ------------------------------------------------------------------ Jellyfin

namespace {
QVariantMap jellyfinItem(const QJsonObject& o) {
    const QString type = o.value("Type").toString();
    const bool video = type == "Movie" || type == "Episode" || type == "Video" || type == "MusicVideo" || type == "TvChannel";
    const QJsonObject ud = o.value("UserData").toObject();
    const double runtime = o.value("RunTimeTicks").toDouble() / 1e7;
    const double resume = ud.value("PlaybackPositionTicks").toDouble() / 1e7;
    QString sub;
    if (type == "Episode") {
        const int s = o.value("ParentIndexNumber").toInt(-1), e = o.value("IndexNumber").toInt(-1);
        if (o.contains("SeriesName")) sub = o.value("SeriesName").toString() + " · ";
        if (s >= 0 && e >= 0) sub += QStringLiteral("S%1E%2 · ").arg(s).arg(e);
        sub += minutes(runtime);
    } else if (type == "Series") {
        sub = o.contains("ProductionYear") ? QString::number(o.value("ProductionYear").toInt()) : QString();
    } else if (video) {
        QStringList p;
        if (o.contains("ProductionYear")) p << QString::number(o.value("ProductionYear").toInt());
        if (runtime > 0) p << minutes(runtime);
        sub = p.join(" · ");
    } else if (o.contains("ChildCount")) {
        sub = QString::number(o.value("ChildCount").toInt());
    }
    QString collection = o.value("CollectionType").toString();
    // a library of films or series opens on its categories (all, recently added, unwatched, genres)
    const QString level = type == "Series" ? "jf-series" : (collection == "movies" || collection == "tvshows") ? "jf-lib" : "jf-items";
    return QVariantMap{{"key", o.value("Id").toString()}, {"title", o.value("Name").toString()}, {"sub", sub},
                       {"kind", video ? "video" : "folder"}, {"level", level}, {"extra", collection},
                       {"shape", type == "Episode" || type == "Video" || type == "CollectionFolder" || type == "UserView" ? "wide" : "poster"},
                       {"resume", resume}, {"duration", runtime}, {"progress", runtime > 0 ? std::min(1.0, resume / runtime) : 0.0},
                       {"played", ud.value("Played").toBool()}, {"overview", o.value("Overview").toString()},
                       {"imageTag", o.value("ImageTags").toObject().value("Primary").toString()}};
}
} // namespace

// a film or series library's first page: everything, recently added, not watched yet, and its genres
QVariantList MediaLibrary::categoryFolders(const QString& itemsLevel, const QString& genresLevel, const QString& id, const QString& extra) {
    QVariantList out;
    const bool plex = itemsLevel == "plex";
    auto add = [&](const char* title, const QString& level, const QString& key, const QString& x) {
        out << QVariantMap{{"key", key}, {"title", title}, {"kind", "folder"}, {"level", level}, {"extra", x}, {"shape", "wide"},
                           {"virtual", true}, {"icon", title}};
    };
    add("mediaAll", itemsLevel, plex ? id + "/all" : id, extra + ";all");
    add("mediaRecent", itemsLevel, plex ? id + "/recentlyAdded" : id, extra + ";recent");
    add("mediaUnwatched", itemsLevel, plex ? id + "/unwatched" : id, extra + ";unwatched");
    add("mediaGenres", genresLevel, plex ? id + "/genre" : id, extra);
    return out;
}

void MediaLibrary::listJellyfin(const Source& s, const Level& l) {
    const int gen = gen_;
    const QString fields = "Fields=Overview,ChildCount&EnableImageTypes=Primary&ImageTypeLimit=1";
    auto fill = [this, s, gen](const QByteArray& body, QVariantList head) {
        if (gen != gen_) return;
        const QJsonDocument d = QJsonDocument::fromJson(body);
        const QJsonArray arr = d.isArray() ? d.array() : d.object().value("Items").toArray();
        QVariantList out = std::move(head);
        for (const auto& v : arr) out << jellyfinItem(v.toObject());
        setItems(out, gen);
        for (int i = 0; i < items_.size(); ++i) {
            const QVariantMap it = items_[i].toMap();
            const QString tag = it.value("imageTag").toString();
            if (tag.isEmpty()) continue;
            const bool wide = it.value("shape") == "wide";
            loadImage(gen, it.value("key").toString(), s, QStringLiteral("/Items/%1/Images/Primary?%2=%3&quality=85&tag=%4")
                                     .arg(it.value("key").toString(), wide ? "fillWidth" : "fillHeight", wide ? "480" : "420", tag),
                      it.value("key").toString() + "_" + tag);
        }
    };
    auto failed = [this, gen](int status) {
        if (gen != gen_) return;
        emit message(status == 401 ? "mediaSignInAgain" : "mediaServerUnreachable", QString(), true);
    };
    const QString uid = s.user;
    if (l.kind == "root") {
        // "Continue watching", then the libraries
        get(s, "/UserViews?userId=" + uid, [this, s, uid, fill, failed, gen](int st, const QByteArray& body) {
            auto views = [this, s, uid, fill, gen](const QByteArray& b) {
                if (gen != gen_) return;
                QVariantList head;
                head << QVariantMap{{"key", "resume"}, {"title", "mediaResume"}, {"kind", "folder"}, {"level", "jf-resume"}, {"shape", "wide"},
                                    {"virtual", true}};
                fill(b, head);
            };
            if (st == 200) { views(body); return; }
            if (st == 404) {   // before Jellyfin 10.9
                get(s, "/Users/" + uid + "/Views", [views, failed](int st2, const QByteArray& b2) { if (st2 == 200) views(b2); else failed(st2); });
                return;
            }
            failed(st);
        });
    } else if (l.kind == "jf-resume") {
        get(s, "/UserItems/Resume?userId=" + uid + "&Limit=40&MediaTypes=Video&" + fields, [this, s, uid, fill, failed, fields](int st, const QByteArray& b) {
            if (st == 200) { fill(b, {}); return; }
            if (st == 404) {
                get(s, "/Users/" + uid + "/Items/Resume?Limit=40&MediaTypes=Video&" + fields,
                    [fill, failed](int st2, const QByteArray& b2) { if (st2 == 200) fill(b2, {}); else failed(st2); });
                return;
            }
            failed(st);
        });
    } else if (l.kind == "jf-series") {
        get(s, "/Shows/" + l.id + "/Seasons?userId=" + uid + "&" + fields, [fill, failed](int st, const QByteArray& b) { if (st == 200) fill(b, {}); else failed(st); });
    } else if (l.kind == "jf-lib") {
        setItems(categoryFolders("jf-items", "jf-genres", l.id, l.extra), gen);
    } else if (l.kind == "jf-genres") {
        const QString types = l.extra == "tvshows" ? "Series" : "Movie";
        get(s, "/Genres?userId=" + uid + "&ParentId=" + l.id + "&IncludeItemTypes=" + types + "&SortBy=SortName",
            [this, s, l, gen, failed](int st, const QByteArray& b) {
                if (gen != gen_) return;
                if (st != 200) { failed(st); return; }
                QVariantList out;
                for (const auto& v : QJsonDocument::fromJson(b).object().value("Items").toArray()) {
                    const QJsonObject o = v.toObject();
                    out << QVariantMap{{"key", l.id}, {"title", o.value("Name").toString()}, {"kind", "folder"}, {"level", "jf-items"},
                                       {"extra", l.extra + ";genre:" + o.value("Id").toString()}, {"shape", "wide"}, {"genre", true}};
                }
                setItems(out, gen);
            });
    } else {
        // a library of films or series lists them all (they may lie in sub folders); anything else its own children.
        // extra: "<collection>;<view>" (all, recent, unwatched, genre:<id>)
        const QString collection = l.extra.section(';', 0, 0), view = l.extra.section(';', 1);
        QString q = "/Items?userId=" + uid + "&ParentId=" + l.id + "&" + fields;
        if (collection == "movies") q += "&Recursive=true&IncludeItemTypes=Movie";
        else if (collection == "tvshows") q += "&Recursive=true&IncludeItemTypes=Series";
        else if (collection == "music" || collection == "books" || collection == "photos") { setItems({}, gen); return; }
        if (view == "recent") q += "&SortBy=DateCreated&SortOrder=Descending&Limit=60";
        else q += "&SortBy=SortName&SortOrder=Ascending";
        if (view == "unwatched") q += "&Filters=IsUnplayed";
        if (view.startsWith("genre:")) q += "&GenreIds=" + view.mid(6);
        get(s, q, [fill, failed](int st, const QByteArray& b) { if (st == 200) fill(b, {}); else failed(st); });
    }
}

void MediaLibrary::jellyfinSignedIn(const QString& url, const QString& name, const QByteArray& authBody) {
    const QJsonObject o = QJsonDocument::fromJson(authBody).object();
    Source s;
    s.id = "jf-" + hashOf(url + o.value("User").toObject().value("Id").toString());
    s.kind = "jellyfin";
    s.url = url;
    s.name = name.isEmpty() ? QUrl(url).host() : name;
    s.user = o.value("User").toObject().value("Id").toString();
    s.token = o.value("AccessToken").toString();
    if (s.token.isEmpty() || s.user.isEmpty()) { emit message("mediaSignInFailed", QString(), true); return; }
    saveSource(s);
    login_.clear();
    loginPoll_.stop();
    emit loginChanged();
    emit message("mediaAdded", s.name, false);
    select(s.id);
}

namespace {
QString normaliseUrl(QString u) {
    u = u.trimmed();
    if (!u.startsWith("http://") && !u.startsWith("https://")) u = "http://" + u;
    while (u.endsWith('/')) u.chop(1);
    if (u.endsWith("/web")) u.chop(4);   // the address of Jellyfin's web page
    return u;
}
} // namespace

void MediaLibrary::addJellyfin(const QString& rawUrl, const QString& user, const QString& password) {
    const QString url = normaliseUrl(rawUrl);
    Source s{"", "jellyfin", "", url, "", "", "", {}};
    get(s, "/System/Info/Public", [this, s, url, user, password](int st, const QByteArray& b) {
        if (st != 200) { emit message("mediaServerUnreachable", url, true); return; }
        const QString name = QJsonDocument::fromJson(b).object().value("ServerName").toString();
        const QByteArray body = QJsonDocument(QJsonObject{{"Username", user}, {"Pw", password}}).toJson(QJsonDocument::Compact);
        post(s, "/Users/AuthenticateByName", body, [this, url, name](int st2, const QByteArray& b2) {
            if (st2 != 200) { emit message(st2 == 401 ? "mediaWrongPassword" : "mediaSignInFailed", QString(), true); return; }
            jellyfinSignedIn(url, name, b2);
        });
    }, 8000);
}

void MediaLibrary::jellyfinQuickConnect(const QString& rawUrl) {
    const QString url = normaliseUrl(rawUrl);
    Source s{"", "jellyfin", "", url, "", "", "", {}};
    get(s, "/System/Info/Public", [this, s, url](int st, const QByteArray& b) {
        if (st != 200) { emit message("mediaServerUnreachable", url, true); return; }
        const QString name = QJsonDocument::fromJson(b).object().value("ServerName").toString();
        auto started = [this, s, url, name](int st2, const QByteArray& b2) {
            if (st2 == 401 || st2 == 403) { emit message("mediaQuickConnectOff", QString(), true); return; }
            if (st2 != 200) { emit message("mediaSignInFailed", QString(), true); return; }
            const QJsonObject o = QJsonDocument::fromJson(b2).object();
            const QString secret = o.value("Secret").toString();
            login_ = QVariantMap{{"kind", "quick"}, {"code", o.value("Code").toString()}, {"url", url}};
            emit loginChanged();
            loginPoll_.disconnect();
            connect(&loginPoll_, &QTimer::timeout, this, [this, s, url, name, secret] {
                get(s, "/QuickConnect/Connect?Secret=" + secret, [this, s, url, name, secret](int st3, const QByteArray& b3) {
                    if (login_.value("kind") != "quick") return;
                    if (st3 != 200) { cancelLogin(); emit message("mediaSignInFailed", QString(), true); return; }
                    if (!QJsonDocument::fromJson(b3).object().value("Authenticated").toBool()) return;
                    loginPoll_.stop();
                    post(s, "/Users/AuthenticateWithQuickConnect", QJsonDocument(QJsonObject{{"Secret", secret}}).toJson(QJsonDocument::Compact),
                         [this, url, name](int st4, const QByteArray& b4) {
                             if (st4 != 200) { emit message("mediaSignInFailed", QString(), true); return; }
                             jellyfinSignedIn(url, name, b4);
                         });
                });
            });
            loginPoll_.start();
        };
        // POST since Jellyfin 10.9, GET before
        post(s, "/QuickConnect/Initiate", QByteArray(), [this, s, started](int st2, const QByteArray& b2) {
            if (st2 == 404 || st2 == 405) get(s, "/QuickConnect/Initiate", started);
            else started(st2, b2);
        });
    }, 8000);
}

void MediaLibrary::cancelLogin() {
    loginPoll_.stop();
    loginPoll_.disconnect();
    login_.clear();
    emit loginChanged();
}

// ------------------------------------------------------------------ Plex

void MediaLibrary::addPlex() {
    QNetworkRequest rq(QUrl("https://plex.tv/api/v2/pins?strong=true"));
    plexHeaders(rq, {});
    auto* r = net_.post(rq, QByteArray());
    request(r, [this](int st, const QByteArray& b) {
        if (st != 200 && st != 201) { emit message("mediaPlexUnreachable", QString(), true); return; }
        const QJsonObject o = QJsonDocument::fromJson(b).object();
        const QString id = QString::number(o.value("id").toVariant().toLongLong()), code = o.value("code").toString();
        const QUrl auth = QUrl::fromEncoded(QStringLiteral("https://app.plex.tv/auth#?clientID=%1&code=%2&context%5Bdevice%5D%5Bproduct%5D=VeyAmux")
                                                .arg(deviceId(), code).toUtf8());
        login_ = QVariantMap{{"kind", "plex"}, {"code", code}, {"url", auth.toString()}};
        emit loginChanged();
        QDesktopServices::openUrl(auth);
        loginPoll_.disconnect();
        auto started = std::make_shared<QElapsedTimer>();
        started->start();
        connect(&loginPoll_, &QTimer::timeout, this, [this, id, started] {
            if (started->elapsed() > 10 * 60 * 1000) { cancelLogin(); return; }
            QNetworkRequest pr(QUrl("https://plex.tv/api/v2/pins/" + id));
            plexHeaders(pr, {});
            request(net_.get(pr), [this](int st2, const QByteArray& b2) {
                if (login_.value("kind") != "plex" || st2 != 200) return;
                const QString token = QJsonDocument::fromJson(b2).object().value("authToken").toString();
                if (token.isEmpty()) return;
                loginPoll_.stop();
                QSettings().setValue("media/plexAccountToken", token);
                // every server of the account, with its own token and its addresses (the home network's first)
                QNetworkRequest rr(QUrl("https://plex.tv/api/v2/resources?includeHttps=1&includeRelay=1"));
                plexHeaders(rr, token);
                request(net_.get(rr), [this](int st3, const QByteArray& b3) {
                    cancelLogin();
                    if (st3 != 200) { emit message("mediaPlexUnreachable", QString(), true); return; }
                    QString first;
                    int added = 0;
                    for (const auto& v : QJsonDocument::fromJson(b3).array()) {
                        const QJsonObject o = v.toObject();
                        if (!o.value("provides").toString().split(',').contains("server")) continue;
                        Source s;
                        s.kind = "plex";
                        s.machine = o.value("clientIdentifier").toString();
                        s.id = "plex-" + hashOf(s.machine);
                        s.name = o.value("name").toString();
                        s.token = o.value("accessToken").toString();
                        QStringList local, remote, relay;
                        for (const auto& c : o.value("connections").toArray()) {
                            const QJsonObject co = c.toObject();
                            const QString uri = co.value("uri").toString();
                            if (co.value("relay").toBool()) relay << uri;
                            else if (co.value("local").toBool()) local << uri;
                            else remote << uri;
                        }
                        s.uris = local + remote + relay;
                        if (s.uris.isEmpty() || s.token.isEmpty()) continue;
                        saveSource(s);
                        if (first.isEmpty()) first = s.id;
                        ++added;
                    }
                    if (!added) { emit message("mediaPlexNoServers", QString(), true); return; }
                    emit message("mediaAdded", QString::number(added), false);
                    select(first);
                }, 20000);
            }, 10000);
        });
        loginPoll_.start();
    }, 15000);
}

// the first of the server's addresses that answers (the home network's before the internet's, the relay last)
void MediaLibrary::plexConnect(const Source& s, std::function<void(bool)> done) {
    if (plexUri_.contains(s.id)) { done(true); return; }
    auto tryAt = std::make_shared<std::function<void(int)>>();
    *tryAt = [this, s, done, tryAt](int i) {
        if (i >= s.uris.size()) { done(false); return; }
        QNetworkRequest rq(QUrl(s.uris[i] + "/identity"));
        plexHeaders(rq, s.token);
        request(net_.get(rq), [this, s, done, tryAt, i](int st, const QByteArray&) {
            if (st == 200) { plexUri_.insert(s.id, s.uris[i]); done(true); return; }
            (*tryAt)(i + 1);
        }, 4000);
    };
    (*tryAt)(0);
}

namespace {
QVariantMap plexItem(const QJsonObject& o) {
    const QString type = o.value("type").toString();
    // a section (Directory) has "key" = its number; an item's children are at its "key" (".../children")
    const bool section = o.contains("agent") || (!o.contains("ratingKey") && o.contains("key"));
    const bool video = !section && (type == "movie" || type == "episode" || type == "clip");
    const double dur = o.value("duration").toDouble() / 1000.0;
    const double resume = o.value("viewOffset").toDouble() / 1000.0;
    QString sub;
    if (type == "episode") {
        if (o.contains("grandparentTitle")) sub = o.value("grandparentTitle").toString() + " · ";
        if (o.contains("parentIndex")) sub += QStringLiteral("S%1E%2 · ").arg(o.value("parentIndex").toInt()).arg(o.value("index").toInt());
        sub += minutes(dur);
    } else if (video) {
        QStringList p;
        if (o.contains("year")) p << QString::number(o.value("year").toInt());
        if (dur > 0) p << minutes(dur);
        sub = p.join(" · ");
    } else if (o.contains("leafCount")) {
        sub = QString::number(o.value("leafCount").toInt());
    } else if (o.contains("year")) {
        sub = QString::number(o.value("year").toInt());
    }
    QString part;
    const QJsonArray media = o.value("Media").toArray();
    if (!media.isEmpty()) {
        const QJsonArray parts = media.first().toObject().value("Part").toArray();
        if (!parts.isEmpty()) part = parts.first().toObject().value("key").toString();
    }
    const QString thumb = o.value("thumb").toString();
    return QVariantMap{{"key", section ? "/library/sections/" + o.value("key").toString() : o.value("key").toString()},
                       {"rating", o.value("ratingKey").toString()}, {"title", o.value("title").toString()}, {"sub", sub},
                       {"kind", video ? "video" : "folder"}, {"level", section ? "plex-section" : "plex"}, {"part", part},
                       {"shape", type == "episode" || type == "clip" || section ? "wide" : "poster"},
                       {"resume", resume}, {"duration", dur}, {"progress", dur > 0 ? std::min(1.0, resume / dur) : 0.0},
                       {"played", o.value("viewCount").toInt() > 0}, {"overview", o.value("summary").toString()}, {"thumb", thumb}};
}
} // namespace

void MediaLibrary::listPlex(const Source& s, const Level& l) {
    const int gen = gen_;
    plexConnect(s, [this, s, l, gen](bool ok) {
        if (gen != gen_) return;
        if (!ok) { emit message("mediaServerUnreachable", s.name, true); return; }
        auto fill = [this, s, gen](const QByteArray& body, QVariantList head) {
            if (gen != gen_) return;
            const QJsonObject mc = QJsonDocument::fromJson(body).object().value("MediaContainer").toObject();
            QVariantList out = std::move(head);
            for (const char* k : {"Directory", "Metadata"})
                for (const auto& v : mc.value(k).toArray()) {
                    const QJsonObject o = v.toObject();
                    const QString t = o.value("type").toString();
                    if (t == "artist" || t == "album" || t == "track" || t == "photo") continue;   // music and photos: not here
                    out << plexItem(o);
                }
            setItems(out, gen);
            for (int i = 0; i < items_.size(); ++i) {
                const QVariantMap it = items_[i].toMap();
                const QString thumb = it.value("thumb").toString();
                if (thumb.isEmpty()) continue;
                const bool wide = it.value("shape") == "wide";
                loadImage(gen, it.value("key").toString(), s, QStringLiteral("/photo/:/transcode?width=%1&height=%2&minSize=1&upscale=1&url=%3")
                                         .arg(wide ? 480 : 300).arg(wide ? 270 : 450).arg(QString::fromUtf8(QUrl::toPercentEncoding(thumb))),
                          hashOf(thumb));
            }
        };
        auto failed = [this, gen](int st) { if (gen == gen_) emit message(st == 401 ? "mediaSignInAgain" : "mediaServerUnreachable", QString(), true); };
        if (l.kind == "root") {
            get(s, "/library/sections", [fill, failed](int st, const QByteArray& b) {
                if (st != 200) { failed(st); return; }
                QVariantList head;
                head << QVariantMap{{"key", "/library/onDeck"}, {"title", "mediaResume"}, {"kind", "folder"}, {"level", "plex"},
                                    {"shape", "wide"}, {"virtual", true}};
                // only film and series libraries
                QJsonObject root = QJsonDocument::fromJson(b).object();
                QJsonObject mc = root.value("MediaContainer").toObject();
                QJsonArray keep;
                for (const auto& v : mc.value("Directory").toArray())
                    if (const QString t = v.toObject().value("type").toString(); t == "movie" || t == "show") keep.append(v);
                mc["Directory"] = keep;
                root["MediaContainer"] = mc;
                fill(QJsonDocument(root).toJson(QJsonDocument::Compact), head);
            });
        } else if (l.kind == "plex-section") {
            setItems(categoryFolders("plex", "plex-genres", l.id, QString()), gen);
        } else if (l.kind == "plex-genres") {
            // id: /library/sections/<n>/genre; each genre's films: /library/sections/<n>/all?genre=<key>
            const QString section = l.id.section('/', 0, -2);
            get(s, l.id, [this, gen, section, failed](int st, const QByteArray& b) {
                if (gen != gen_) return;
                if (st != 200) { failed(st); return; }
                QVariantList out;
                for (const auto& v : QJsonDocument::fromJson(b).object().value("MediaContainer").toObject().value("Directory").toArray()) {
                    const QJsonObject o = v.toObject();
                    out << QVariantMap{{"key", section + "/all?genre=" + o.value("key").toString()}, {"title", o.value("title").toString()},
                                       {"kind", "folder"}, {"level", "plex"}, {"shape", "wide"}, {"genre", true}};
                }
                setItems(out, gen);
            });
        } else {
            get(s, l.id, [fill, failed](int st, const QByteArray& b) { if (st == 200) fill(b, {}); else failed(st); });
        }
    });
}

// ------------------------------------------------------------------ playing

void MediaLibrary::play(int index, bool fromStart) {
    if (index < 0 || index >= items_.size()) return;
    const QVariantMap it = items_[index].toMap();
    if (it.value("kind") != "video") return;
    startPlayback(source(current_), it, fromStart);
}

void MediaLibrary::startPlayback(const Source& s, const QVariantMap& it, bool fromStart) {
    if (!playing_.source.isEmpty() && player_->hasMedia()) report("stopped");
    playing_ = {};
    const double resume = fromStart ? 0.0 : it.value("resume").toDouble();
    bool ok = false;
    if (s.kind == "local") {
        ok = player_->openPath(it.value("key").toString());
        playing_.item = it.value("key").toString();
    } else if (s.kind == "jellyfin") {
        playing_.item = it.value("key").toString();
        ok = player_->openStream(s.url + "/Videos/" + playing_.item + "/stream?static=true",
                                 "Authorization: " + jellyfinAuth(s.token) + "\r\n", it.value("title").toString());
    } else if (s.kind == "plex") {
        const QString part = it.value("part").toString();
        if (part.isEmpty()) { emit message("mediaCannotPlay", QString(), true); return; }
        playing_.item = it.value("rating").toString();
        ok = player_->openStream(plexUri_.value(s.id) + part,
                                 "X-Plex-Token: " + s.token + "\r\nX-Plex-Client-Identifier: " + deviceId() + "\r\nX-Plex-Product: VeyAmux\r\n",
                                 it.value("title").toString());
    }
    if (!ok) { playing_ = {}; emit playbackChanged(); return; }
    playing_.source = s.id;
    playing_.kind = s.kind;
    playing_.duration = it.value("duration").toDouble();
    playing_.started = true;
    if (resume > 5) player_->seek(resume);
    if (s.kind != "local") fetchSubtitles(s, playing_.item);
    report("start");
    progress_.start();
    emit playbackChanged();
}

// subtitle files the server keeps beside the video (the ones inside the file come with the stream)
void MediaLibrary::fetchSubtitles(const Source& s, const QString& item) {
    auto add = [this, s, item](const QString& path, const QString& title, const QString& lang, const QString& ext) {
        get(s, path, [this, item, title, lang, ext](int st, const QByteArray& b) {
            if (st == 200 && !b.isEmpty() && playing_.item == item) player_->addSubtitleData(b, title, lang, ext);
        });
    };
    if (s.kind == "jellyfin") {
        get(s, "/Items/" + item + "?userId=" + s.user, [this, s, item, add](int st, const QByteArray& b) {
            if (st != 200 || playing_.item != item) return;
            const QJsonArray sources = QJsonDocument::fromJson(b).object().value("MediaSources").toArray();
            if (sources.isEmpty()) return;
            const QJsonObject ms = sources.first().toObject();
            for (const auto& v : ms.value("MediaStreams").toArray()) {
                const QJsonObject o = v.toObject();
                if (o.value("Type").toString() != "Subtitle" || !o.value("IsExternal").toBool() || !o.value("IsTextSubtitleStream").toBool(true)) continue;
                // Jellyfin hands any text subtitle out as SubRip
                add(QStringLiteral("/Videos/%1/%2/Subtitles/%3/0/Stream.srt").arg(item, ms.value("Id").toString()).arg(o.value("Index").toInt()),
                    o.value("Title").toString(), o.value("Language").toString(), "srt");
            }
        });
    } else if (s.kind == "plex") {
        get(s, "/library/metadata/" + item, [this, item, add](int st, const QByteArray& b) {
            if (st != 200 || playing_.item != item) return;
            const QJsonArray md = QJsonDocument::fromJson(b).object().value("MediaContainer").toObject().value("Metadata").toArray();
            if (md.isEmpty()) return;
            const QJsonArray media = md.first().toObject().value("Media").toArray();
            if (media.isEmpty()) return;
            const QJsonArray parts = media.first().toObject().value("Part").toArray();
            if (parts.isEmpty()) return;
            for (const auto& v : parts.first().toObject().value("Stream").toArray()) {
                const QJsonObject o = v.toObject();
                const QString codec = o.value("codec").toString();
                if (o.value("streamType").toInt() != 3 || !o.contains("key") || (codec != "srt" && codec != "ass" && codec != "ssa" && codec != "vtt" && codec != "webvtt")) continue;
                add(o.value("key").toString(), o.value("title").toString(), o.value("languageCode").toString(), codec == "webvtt" ? "vtt" : codec);
            }
        });
    }
}

void MediaLibrary::stopPlayback() {
    if (!playing_.source.isEmpty() && player_->hasMedia()) report("stopped");
    player_->closeMedia();
}

void MediaLibrary::report(const QString& state) {
    if (playing_.source.isEmpty()) return;
    const bool end = state == "stopped" || state == "ended";
    double pos = state == "ended" ? playing_.duration : player_->position();
    double dur = player_->duration() > 0 ? player_->duration() : playing_.duration;
    if (pos <= 0 && playing_.lastPos > 0 && end) pos = playing_.lastPos;   // the player was closed before this ran
    if (pos > 0) playing_.lastPos = pos;
    if (dur > 0) playing_.duration = dur;
    const bool finished = state == "ended" || (dur > 0 && pos > dur * 0.92);
    const Source s = source(playing_.source);
    if (s.kind == "local") {
        QSettings q;
        const QString h = hashOf(playing_.item);
        if (finished) q.remove("media/resume/" + h);
        else if (pos > 5) q.setValue("media/resume/" + h, pos);
        if (dur > 0) q.setValue("media/duration/" + h, dur);
    } else if (s.kind == "jellyfin") {
        const qint64 ticks = qint64((finished ? dur : pos) * 1e7);
        QJsonObject body{{"ItemId", playing_.item}, {"PositionTicks", ticks}, {"CanSeek", true}, {"PlayMethod", "DirectStream"},
                         {"IsPaused", !player_->playing()}};
        const QString path = state == "start" ? "/Sessions/Playing" : end ? "/Sessions/Playing/Stopped" : "/Sessions/Playing/Progress";
        post(s, path, QJsonDocument(body).toJson(QJsonDocument::Compact), [](int, const QByteArray&) {});
        if (finished && end)
            post(s, "/UserPlayedItems/" + playing_.item + "?userId=" + s.user, QByteArray(), [](int, const QByteArray&) {});
    } else if (s.kind == "plex") {
        const QString st = end ? "stopped" : player_->playing() ? "playing" : "paused";
        get(s, QStringLiteral("/:/timeline?ratingKey=%1&key=%2&state=%3&time=%4&duration=%5")
                   .arg(playing_.item, QString::fromUtf8(QUrl::toPercentEncoding("/library/metadata/" + playing_.item)), st)
                   .arg(qint64(pos * 1000)).arg(qint64(dur * 1000)), [](int, const QByteArray&) {});
        if (finished && end)
            get(s, "/:/scrobble?identifier=com.plexapp.plugins.library&key=" + playing_.item, [](int, const QByteArray&) {});
    }
    if (end) {
        progress_.stop();
        playing_ = {};
        emit playbackChanged();
        // the page shows the new positions when it comes back
        QTimer::singleShot(800, this, [this] { list(); });
    }
}

} // namespace veyamux
