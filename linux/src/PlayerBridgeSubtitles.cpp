// VeyAmux for Linux: PlayerBridge - subtitles (Subtitles.h): the tracks, the one shown, files beside the video and
// from media servers, and the cue that is due (text for QML, or a picture through the "subs" image provider).
#include "PlayerBridge.h"

#include <QDir>
#include <QScreen>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QMutex>
#include <QQuickImageProvider>
#include <QSettings>
#include <QStandardPaths>
#include <QStringDecoder>

namespace {

// the picture of the cue that is due (QML: image://subs/<id>)
QMutex subImageM;
QImage subImage;

class SubtitleImages : public QQuickImageProvider {
public:
    SubtitleImages() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString&, QSize* size, const QSize&) override {
        QMutexLocker l(&subImageM);
        if (size) *size = subImage.size();
        return subImage;
    }
};

// "dan", "da", "Danish", "dansk" -> the language
QLocale::Language languageOf(const QString& code) {
    const QString c = code.trimmed().toLower();
    if (c.isEmpty()) return QLocale::AnyLanguage;
    QLocale::Language l = QLocale::codeToLanguage(c);
    if (l != QLocale::AnyLanguage) return l;
    for (int i = QLocale::C + 1; i <= QLocale::LastLanguage; ++i) {
        const auto lang = QLocale::Language(i);
        if (QLocale::languageToString(lang).toLower() == c || QLocale(lang).nativeLanguageName().toLower() == c) return lang;
    }
    return QLocale::AnyLanguage;
}

QString trackName(const veyamux::SubtitleTrack& t) {
    const QLocale::Language l = languageOf(QString::fromStdString(t.lang));
    QString name = l == QLocale::English ? QStringLiteral("English")
                 : l != QLocale::AnyLanguage ? QLocale(l).nativeLanguageName() : QString::fromStdString(t.lang);
    if (!name.isEmpty()) name[0] = name[0].toUpper();
    QStringList extra;
    if (!t.title.empty() && QString::fromStdString(t.title).compare(name, Qt::CaseInsensitive) != 0) extra << QString::fromStdString(t.title);
    if (t.forced) extra << "forced";
    if (name.isEmpty()) name = extra.isEmpty() ? QStringLiteral("#%1").arg(t.id) : extra.takeFirst();
    return extra.isEmpty() ? name : name + " · " + extra.join(" · ");
}

} // namespace

QQuickImageProvider* PlayerBridge::subtitleImageProvider() { return new SubtitleImages; }

QVariantList PlayerBridge::subtitleTracks() const {
    QVariantList out;
    for (const auto& t : const_cast<PlayerBridge*>(this)->engine_.subtitles().tracks())
        out << QVariantMap{{"id", t.id}, {"name", trackName(t)}, {"lang", QString::fromStdString(t.lang)}};
    return out;
}

int PlayerBridge::subtitleTrack() const { return const_cast<PlayerBridge*>(this)->engine_.subtitles().selected(); }

void PlayerBridge::setSubtitleTrack(int id) {
    auto& subs = engine_.subtitles();
    if (!subs.select(id)) return;
    // the choice is remembered by its language for the next films ("off" too)
    QString lang = "off";
    for (const auto& t : subs.tracks()) if (t.id == id) lang = QString::fromStdString(t.lang);
    QSettings().setValue("subs/lang", lang);
    // a track in the file is decoded as it is read: back to here, so the current line shows
    if (id >= 0 && id < 1000 && engine_.isOpen() && !engine_.isLive()) engine_.seek(engine_.position());
    subCueId_ = ~0ull;
    updateSubtitle();
    emit subtitlesChanged();
}

// Off -> each track -> Off
void PlayerBridge::cycleSubtitles() {
    const auto tracks = engine_.subtitles().tracks();
    if (tracks.empty()) return;
    const int cur = engine_.subtitles().selected();
    int next = -1;
    if (cur < 0) next = tracks.front().id;
    else for (size_t i = 0; i < tracks.size(); ++i) if (tracks[i].id == cur) next = i + 1 < tracks.size() ? tracks[i + 1].id : -1;
    setSubtitleTrack(next);
    QString name = tr("Off");
    for (const auto& t : tracks) if (t.id == next) name = trackName(t);
    showNotice(L("Subtitles: ", "Undertekster: ") + (next < 0 ? L("off", "fra") : name));
}

// the remembered language, when the new video has it (forced tracks only when nothing else fits)
void PlayerBridge::pickSubtitles() {
    const QString pref = QSettings().value("subs/lang").toString();
    if (pref.isEmpty() || pref == "off") return;
    const QLocale::Language want = languageOf(pref);
    int pick = -1;
    for (int pass = 0; pass < 2 && pick < 0; ++pass)
        for (const auto& t : engine_.subtitles().tracks()) {
            if (t.forced != (pass == 1)) continue;
            const QString l = QString::fromStdString(t.lang);
            if ((want != QLocale::AnyLanguage && languageOf(l) == want) || l.compare(pref, Qt::CaseInsensitive) == 0) { pick = t.id; break; }
        }
    if (pick < 0 || !engine_.subtitles().select(pick)) return;
    // a track in the file: its first packets may already have been read past
    if (pick < 1000 && engine_.isOpen() && !engine_.isLive()) engine_.seek(engine_.position());
}

// a new video: its own tracks, files beside it ("Film.da.srt", "Film.Danish.forced.srt", "Film.srt")
void PlayerBridge::subtitlesOpened(const QString& path) {
    subCueId_ = ~0ull;
    if (!path.isEmpty() && path.startsWith('/')) {
        const QFileInfo fi(path);
        const QString stem = fi.completeBaseName();
        for (const auto& f : fi.dir().entryInfoList({"*.srt", "*.ass", "*.ssa", "*.vtt", "*.sub"}, QDir::Files, QDir::Name)) {
            if (!f.completeBaseName().startsWith(stem, Qt::CaseInsensitive)) continue;
            QString lang;
            QStringList extra;
            for (const auto& part : f.completeBaseName().mid(stem.size()).split('.', Qt::SkipEmptyParts)) {
                if (lang.isEmpty() && languageOf(part) != QLocale::AnyLanguage) lang = part;
                else extra << part;
            }
            QFile file(f.absoluteFilePath());
            if (file.open(QIODevice::ReadOnly) && file.size() < (16 << 20))
                addSubtitleData(file.readAll(), extra.join(' '), lang, f.suffix().toLower(), false);
        }
    }
    pickSubtitles();
    updateSubtitle();
    emit subtitlesChanged();
}

// a subtitle file's contents (a file beside the video, or from Jellyfin / Plex): made UTF-8 (many .srt files are
// Windows-1252) and decoded
void PlayerBridge::addSubtitleData(const QByteArray& data, const QString& title, const QString& lang, const QString& ext, bool announce) {
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString text = utf8.decode(data);
    if (utf8.hasError()) text = QStringDecoder(QStringDecoder::Latin1).decode(data);
    if (text.startsWith(QChar(0xfeff))) text.remove(0, 1);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + "/veyamux-subs";
    QDir().mkpath(dir);
    const QString tmp = QStringLiteral("%1/%2.%3").arg(dir).arg(++subFileNo_).arg(ext.isEmpty() ? "srt" : ext);
    {
        QFile f(tmp);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
        f.write(text.toUtf8());
    }
    std::string err;
    const int id = engine_.subtitles().addFile(tmp.toStdString(), lang.toStdString(), title.toStdString(), err);
    QFile::remove(tmp);
    if (id < 0) { qWarning("Subtitles: %s", err.c_str()); return; }
    if (announce) {
        if (engine_.subtitles().selected() < 0) pickSubtitles();
        emit subtitlesChanged();
    }
}

void PlayerBridge::updateSubtitle() {
    veyamux::SubtitleCue c;
    const bool have = engine_.isOpen() && !engine_.isLive() && engine_.subtitles().at(engine_.position(), c);
    const uint64_t id = have ? c.id : 0;
    if (id == subCueId_) return;
    subCueId_ = id;
    subText_ = have ? QString::fromStdString(c.text) : QString();
    subImageUrl_.clear();
    if (have && !c.rgba.empty() && c.frameW > 0 && c.frameH > 0) {
        QImage img(c.w, c.h, QImage::Format_RGBA8888);
        for (int y = 0; y < c.h; ++y) std::memcpy(img.scanLine(y), c.rgba.data() + size_t(y) * c.w * 4, size_t(c.w) * 4);
        { QMutexLocker l(&subImageM); subImage = img; }
        subBox_ = QRectF(double(c.x) / c.frameW, double(c.y) / c.frameH, double(c.w) / c.frameW, double(c.h) / c.frameH);
        subAspect_ = double(c.frameW) / c.frameH;
        subImageUrl_ = QStringLiteral("image://subs/%1").arg(id);
    }
    emit subtitleChanged();
}
