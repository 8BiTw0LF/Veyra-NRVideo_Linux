// VeyAmux for Linux: VRR state of the screens (see DisplayVrr.h).
#include "DisplayVrr.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>

namespace veyamux::display {

Vrr parseKscreenDoctor(const QString& raw, QString* screens) {
    QString text = raw;
    text.remove(QRegularExpression(QStringLiteral("\\x1B\\[[0-9;]*[A-Za-z]")));   // colour codes
    Vrr state = Vrr::Unknown;
    QStringList on;
    QString current;
    bool enabled = true;
    // Plasma 6 prints each property on its own indented line under "Output: <n> <name>"; older versions print one
    // line per screen. Both are read: a new "Output:" starts a screen, "disabled" switches it off, "Vrr: <policy>".
    auto finish = [&](const QString& policyIn) {
        const QString policy = policyIn.toLower();
        if (!enabled) return;
        if (policy == QLatin1String("automatic") || policy == QLatin1String("always")) { state = Vrr::On; on << current; }
        else if (state == Vrr::Unknown && (policy == QLatin1String("never") || policy == QLatin1String("incapable"))) state = Vrr::Off;
    };
    static const QRegularExpression outputRe(QStringLiteral("Output:\\s*\\d+\\s+(\\S+)"));
    static const QRegularExpression vrrRe(QStringLiteral("\\bVrr:\\s*(\\w+)"), QRegularExpression::CaseInsensitiveOption);
    for (const QString& line : text.split('\n')) {
        const auto m = outputRe.match(line);
        if (m.hasMatch()) { current = m.captured(1); enabled = true; }
        // "disabled" as a screen state (not "HDR: disabled" etc.)
        const QString t = line.trimmed();
        if (t == QLatin1String("disabled") || (m.hasMatch() && line.mid(m.capturedEnd()).trimmed().startsWith(QLatin1String("disabled"))))
            enabled = false;
        const auto v = vrrRe.match(line);
        if (v.hasMatch()) finish(v.captured(1));
    }
    if (screens) *screens = on.join(QStringLiteral(", "));
    return state;
}

void queryVrr(QObject* context, std::function<void(Vrr, QString)> done) {
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("kscreen-doctor"));
    if (!desktop.contains(QStringLiteral("KDE"), Qt::CaseInsensitive) || tool.isEmpty()) { done(Vrr::Unknown, {}); return; }
    auto* p = new QProcess(context);
    // the AppImage's own Qt must not leak into the KDE tool
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const char* k : {"LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QML2_IMPORT_PATH", "QML_IMPORT_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH"})
        env.remove(QString::fromLatin1(k));
    p->setProcessEnvironment(env);
    QObject::connect(p, &QProcess::finished, context, [p, done](int code, QProcess::ExitStatus st) {
        QString screens;
        const Vrr v = (st == QProcess::NormalExit && code == 0) ? parseKscreenDoctor(QString::fromUtf8(p->readAllStandardOutput()), &screens) : Vrr::Unknown;
        p->deleteLater();
        done(v, screens);
    });
    QObject::connect(p, &QProcess::errorOccurred, context, [p, done](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        p->deleteLater();
        done(Vrr::Unknown, {});
    });
    p->start(tool, {QStringLiteral("-o")});
}

} // namespace veyamux::display
