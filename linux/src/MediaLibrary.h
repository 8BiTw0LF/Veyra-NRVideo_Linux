// VeyAmux for Linux: the Media page. Films and series from the user's own folders, Jellyfin servers and Plex servers,
// shown the same way (posters, folders, "continue watching"), played through VeyAmux's normal file path with every
// enhancement. Server streams are played as they are stored (no transcoding on the server): FFmpeg decodes them here.
//
//   Jellyfin: signed in with a user name and password, or with Quick Connect (a code confirmed in another Jellyfin app)
//   Plex:     signed in on plex.tv in the browser (a PIN); every server of the account is added. Outside the home
//             network Plex wants Plex Pass or a Remote Watch Pass; at home it is free.
//
// Tokens are kept in the settings under keys containing "token" (the debug report hides those), and never put into
// a URL: they go in HTTP headers, also to FFmpeg.
#pragma once
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <vector>

class PlayerBridge;
class QNetworkReply;

namespace veyamux {

class MediaLibrary : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList sources READ sources NOTIFY sourcesChanged)   // [{id, name, kind}]
    Q_PROPERTY(QString current READ current NOTIFY viewChanged)
    Q_PROPERTY(QVariantList crumbs READ crumbs NOTIFY viewChanged)        // titles below the source
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    // the posters as they arrive: image(key) after a change of imageVersion (one lookup per card; a map handed to QML
    // was copied whole for every card on every change, which froze the page with large libraries)
    Q_PROPERTY(int imageVersion READ imageVersion NOTIFY imagesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // signing in: {kind: "quick" | "plex", code, url} while a code waits to be confirmed, else empty
    Q_PROPERTY(QVariantMap login READ login NOTIFY loginChanged)
    Q_PROPERTY(bool playingFromLibrary READ playingFromLibrary NOTIFY playbackChanged)
    // while a video from a server plays: its intro and credits (Jellyfin's media segments, Plex's markers) as
    // [{kind: "intro" | "credits", start, end}], the next episode's title ("" when there is none), and the quality it
    // streams in (0: the file as it is, else the most Mbit/s - the server converts it)
    Q_PROPERTY(QVariantList segments READ segments NOTIFY segmentsChanged)
    Q_PROPERTY(QString nextTitle READ nextTitle NOTIFY segmentsChanged)
    Q_PROPERTY(int quality READ quality WRITE setQuality NOTIFY qualityChanged)
    Q_PROPERTY(bool streaming READ streaming NOTIFY playbackChanged)   // from Jellyfin or Plex (not a file of your own)
    // pictures for the seek bar (Jellyfin's trickplay, Plex's index pictures): thumbAt() after thumbsChanged
    Q_PROPERTY(bool hasThumbs READ hasThumbs NOTIFY thumbsChanged)
    Q_PROPERTY(bool canSearch READ canSearch NOTIFY viewChanged)
public:
    explicit MediaLibrary(PlayerBridge* player, QObject* parent = nullptr);
    ~MediaLibrary() override;

    QVariantList sources() const;
    QString current() const { return current_; }
    QVariantList crumbs() const;
    QVariantList items() const { return items_; }
    int imageVersion() const { return imageVersion_; }
    Q_INVOKABLE QString image(const QString& key) const { return images_.value(key); }
    // a card came on screen: its poster is fetched next (the ones out of sight wait until they are scrolled to)
    Q_INVOKABLE void want(const QString& key);
    bool busy() const { return pending_ > 0; }
    QVariantMap login() const { return login_; }
    bool playingFromLibrary() const { return !playing_.source.isEmpty(); }
    QVariantList segments() const { return playing_.segments; }
    QString nextTitle() const { return playing_.next.value("title").toString(); }
    int quality() const;
    void setQuality(int mbps);
    bool streaming() const { return playing_.kind == "jellyfin" || playing_.kind == "plex"; }
    bool hasThumbs() const { return playing_.trickCount > 0; }
    bool canSearch() const;
    Q_INVOKABLE void search(const QString& text);
    Q_INVOKABLE void playNext();
    // the seek bar's picture at a time: {url, x, y, w, h} (a part of a larger picture when w > 0), {} while it is fetched
    Q_INVOKABLE QVariantMap thumbAt(double seconds);

    Q_INVOKABLE void select(const QString& sourceId);
    Q_INVOKABLE void openItem(int index);                // a folder: into it
    Q_INVOKABLE bool back();                             // one level up; false at the top
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void play(int index, bool fromStart);
    Q_INVOKABLE void addFolder();
    Q_INVOKABLE void removeSource(const QString& id);
    Q_INVOKABLE void addJellyfin(const QString& url, const QString& user, const QString& password);
    Q_INVOKABLE void jellyfinQuickConnect(const QString& url);
    Q_INVOKABLE void addPlex();
    Q_INVOKABLE void cancelLogin();
    Q_INVOKABLE void stopPlayback();                     // reports the position and closes the video

signals:
    void sourcesChanged();
    void viewChanged();
    void itemsChanged();
    void imagesChanged();
    void busyChanged();
    void loginChanged();
    void playbackChanged();
    void segmentsChanged();
    void qualityChanged();
    void thumbsChanged();
    void message(const QString& key, const QString& arg, bool error);   // a Strings.js key and its %1

private:
    struct Level { QString title, kind, id, extra; };   // kind: what the id is (root, folder, jf-items, plex-key, ...)
    struct Source { QString id, kind, name, url, user, token, machine; QStringList uris; };
    struct Playing {
        QString source, item, kind;
        double duration = 0;
        double lastPos = -1;
        bool started = false;
        QVariantMap it, next;        // what plays, and the episode after it
        QVariantList segments;
        QString session;             // a converted stream's session (Jellyfin's PlaySessionId, Plex's session)
        // seek bar pictures: Jellyfin's tiles (tileW x tileH pictures of w x h each), or Plex's BIF file
        int trickCount = 0, trickIntervalMs = 0, trickW = 0, trickH = 0, tileW = 1, tileH = 1, trickWidth = 0;
        QByteArray bif;
        std::vector<quint32> bifOffsets;
        QSet<QString> trickFetching;
    };

    // sources
    QList<Source> loadSources() const;
    void saveSource(const Source& s);
    Source source(const QString& id) const;
    QString deviceId() const;
    // listing
    void list();
    void listLocal(const Level& l);
    void listJellyfin(const Source& s, const Level& l);
    void listPlex(const Source& s, const Level& l);
    void setItems(QVariantList items, int gen);
    static QVariantList categoryFolders(const QString& itemsLevel, const QString& genresLevel, const QString& id, const QString& extra);
    // network
    QNetworkReply* get(const Source& s, const QString& path, std::function<void(int status, const QByteArray& body)> done, int timeoutMs = 20000);
    QNetworkReply* post(const Source& s, const QString& path, const QByteArray& json, std::function<void(int status, const QByteArray& body)> done);
    void request(QNetworkReply* r, std::function<void(int, const QByteArray&)> done, int timeoutMs);
    QString jellyfinAuth(const QString& token) const;
    void plexHeaders(class QNetworkRequest& rq, const QString& token) const;
    void plexConnect(const Source& s, std::function<void(bool)> done);
    void loadImage(int gen, const QString& key, const Source& s, const QString& path, const QString& cacheName);
    void pumpImages();
    // playback
    void startPlayback(const Source& s, const QVariantMap& item, bool fromStart);
    void fetchSubtitles(const Source& s, const QString& item);
    void fetchExtras(const Source& s, const QVariantMap& it);   // intro / credits, the next episode, seek bar pictures
    QString streamUrl(const Source& s, const QVariantMap& it, QString& session) const;
    void report(const QString& state);
    void jellyfinSignedIn(const QString& url, const QString& name, const QByteArray& authBody);

    PlayerBridge* player_;
    QNetworkAccessManager net_;
    QString current_;
    QList<Level> levels_;
    QVariantList items_;
    QHash<QString, QString> images_;
    int imageVersion_ = 0;
    int gen_ = 0;                    // listing generation: late answers for an old listing are dropped
    int pending_ = 0;
    QVariantMap login_;
    QTimer loginPoll_;
    QHash<QString, QString> plexUri_;   // server -> the connection that answered
    struct ImageJob { int gen; QString key; Source src; QString path, file; };
    QList<ImageJob> imageQueue_;                 // wanted (on screen), the last wanted first
    QHash<QString, ImageJob> imageWaiting_;      // not on screen yet
    int imagesRunning_ = 0;
    QTimer imagesChanged_;
    Playing playing_;
    QTimer progress_;
};

} // namespace veyamux
