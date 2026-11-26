// VeyAmux for Linux: the Games page. One folder of games (with any sub folders) is scanned; every game is sorted under
// its system by the folder it lies in ("snes", "Mega Drive", "psx", ...), else by its file type, else (CD images) by
// the disc's own header. Playing one picks the emulator core by itself: one already installed (RetroArch's, the
// system's) or one fetched from libretro's build server the first time. Box art comes from libretro's thumbnails.
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class QNetworkAccessManager;

namespace veyamux {

struct GameSystem {
    const char* id;
    const char* name;      // shown
    const char* thumbs;    // libretro's name for the system (thumbnails)
    const char* folders;   // folder names that mean this system (lower case, letters and digits only), comma separated
    const char* exts;      // file types that mean this system without a folder
    const char* folderExts;// file types accepted inside a folder of this system
    const char* cores;     // libretro cores, best first
    bool analog;           // has analog sticks (else the left stick is a D-pad as well)
    const char* bios;      // BIOS files the cores want (shown when a game does not start), or ""
};
const std::vector<GameSystem>& gameSystems();
const GameSystem* gameSystem(const QString& id);
// the system a file belongs to, from its path below the games folder ("" when it is not a game)
QString detectSystem(const QString& path, const QString& root);
// libretro's thumbnail name rule, and the title shown ("Super Mario World (USA)" -> "Super Mario World")
QString thumbnailName(const QString& stem);
QString displayTitle(const QString& stem);
// the best box art from a list of names for a game (empty when none fits)
QString matchThumbnail(const QString& stem, const QStringList& names);

class GameLibrary : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString folder READ folder NOTIFY changed)
    Q_PROPERTY(QString biosFolder READ biosFolder NOTIFY changed)
    Q_PROPERTY(QVariantList systems READ systems NOTIFY changed)        // [{id, name, count}] with games; "recent" first
    Q_PROPERTY(QString current READ current NOTIFY changed)
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)            // the games of the current system
    Q_PROPERTY(QVariantList recentGames READ recentGames NOTIFY changed)   // the last played (at most 6), for the start screen
    // what runs in the background: a Strings.js key ("" when idle) and its %1
    Q_PROPERTY(QString busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString busyArg READ busyArg NOTIFY busyChanged)
    Q_PROPERTY(int progress READ progress NOTIFY busyChanged)           // 0..100, -1 unknown
    Q_PROPERTY(int coverVersion READ coverVersion NOTIFY coversChanged)
    // RetroAchievements: the signed-in user ("" when none); signing in sends the password once, only the token is kept
    Q_PROPERTY(QString raUser READ raUser NOTIFY raChanged)
    Q_PROPERTY(bool raBusy READ raBusy NOTIFY raChanged)
public:
    explicit GameLibrary(QObject* parent = nullptr);
    ~GameLibrary() override;

    QString folder() const { return folder_; }
    QString biosFolder() const;
    QVariantList systems() const;
    QString current() const { return current_; }
    QVariantList items() const;
    QVariantList recentGames() const;
    QString busy() const { std::lock_guard l(m_); return busy_; }
    QString busyArg() const { std::lock_guard l(m_); return busyArg_; }
    int progress() const { return progress_.load(); }
    int coverVersion() const { return coverVersion_; }

    Q_INVOKABLE void chooseFolder();
    Q_INVOKABLE void chooseBiosFolder();
    Q_INVOKABLE void openBiosFolder();
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void refresh() { emit changed(); }   // the page shown again (Continue may have come)
    Q_INVOKABLE void select(const QString& systemId);
    Q_INVOKABLE void play(const QString& path, bool resume = false);   // resume: from the state written when it was quit
    Q_INVOKABLE void downloadCovers();          // box art for every game that has none
    Q_INVOKABLE void setCover(const QString& path);   // a picture of the user's own
    Q_INVOKABLE QString biosHint(const QString& systemId) const;
    QString raUser() const;
    bool raBusy() const { return raBusy_; }
    Q_INVOKABLE void raSignIn(const QString& user, const QString& password);
    Q_INVOKABLE void raSignOut();

    // for the start of a game: the core to use (found or fetched), "" with the reason in error
    QString coreFor(const QString& systemId, QString& error, bool download);
    QString coverPath(const QString& systemId, const QString& romPath) const;

signals:
    void changed();
    void busyChanged();
    void coversChanged();
    void raChanged();
    // ready to play: PlayerBridge::openGame
    void launch(const QString& core, const QString& rom, const QString& title, const QString& systemId, bool resume);
    // a Strings.js key and its %1
    void message(const QString& key, const QString& arg, bool error);

private:
    struct Entry { QString path, system, title, region, thumb; };   // thumb: the name for box art (arcade: the full title)
    void load();
    void save() const;
    void scanThread(QString root);
    static bool isBiosFile(const QString& path, const QString& root);
    static bool isPicture(const QString& path);
    void copyBios(const QStringList& files);
    void groupDiscs(std::vector<Entry>& found);
    void setBusy(const QString& key, const QString& arg = {}, int p = -1);
    void say(const QString& key, const QString& arg, bool error);
    void work(std::function<void()> fn);
    QString dataDir(const QString& sub) const;
    QVariantMap itemFor(const Entry& e) const;

    QString folder_, current_;
    std::vector<Entry> entries_;
    mutable std::mutex m_;
    QString busy_, busyArg_;
    std::atomic<int> progress_{-1};
    std::thread worker_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> working_{false};
    int coverVersion_ = 0;
    bool raBusy_ = false;
    QNetworkAccessManager* net_ = nullptr;
};

} // namespace veyamux
