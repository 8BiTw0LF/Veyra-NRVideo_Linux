// veyamux-update-test [install]  - asks GitHub for the newest release (and with "install" replaces $APPIMAGE with it)
#include "AppUpdate.h"
#include <QCoreApplication>
#include <cstdio>
#include <cstring>
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    update::Release r;
    QString err;
    if (!update::latest(r, err)) { std::printf("error: %s\n", qPrintable(err)); return 1; }
    std::printf("tag %s version %s\nappimage %s (%lld bytes)\nzsync %s\nsha256 %s\nnewer than 1.5: %d, than 9.9: %d\n",
                qPrintable(r.tag), qPrintable(r.version), qPrintable(r.appImageUrl), (long long)r.size, qPrintable(r.zsyncUrl),
                qPrintable(r.sha256.isEmpty() ? QString("(none)") : r.sha256), update::newer(r.version, "1.5"), update::newer(r.version, "9.9"));
    if (argc > 1 && std::strcmp(argv[1], "install") == 0) {
        QString msg;
        const bool ok = update::install(r, [](int pc) { if (pc % 20 == 0) std::printf("  %d %%\n", pc); }, msg);
        std::printf("install: %d %s\n", ok, qPrintable(msg));
        return ok ? 0 : 1;
    }
    return 0;
}
