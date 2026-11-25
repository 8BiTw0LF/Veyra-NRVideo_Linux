// VeyAmux for Linux: one HTTPS download into a file (libcurl), shared by the add-on installers (lsfg-vk 2, FSR 4).
#pragma once
#include <QString>
#include <functional>

namespace veyamux {

// HTTPS only (also after redirects), at most maxBytes, progress 0..100 when the size is known. Blocking.
bool httpsDownload(const QString& url, const QString& path, qint64 maxBytes, const std::function<void(int)>& progress, QString& error);
// SHA-256 of a file in hex (empty if it cannot be read)
QString fileSha256(const QString& path);

} // namespace veyamux
