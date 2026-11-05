#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>
#include <vector>

class QPixmap;

namespace app {

class SecretStore;

// One web hit returned by a provider.
struct SurfaceResult {
    QString provider;   // the provider id, e.g. "google_vision"
    QString providerName;  // display name
    QString title;
    QString url;
    QString thumbnail;     // image URL, may be empty
    double similarity = 0.0;  // 0-1 where the provider gives a score; -1 when unknown
    QString extra;         // provider-specific note (e.g. trace.moe episode/timestamps)
    QString error;        // non-empty when this provider returned no usable result
};

// Progress callback: (done, total, currentProvider).
using SurfaceProgress = std::function<void(int done, int total, const QString& current)>;

// Runs the reverse-image search across every enabled provider for an image
// file on disk. Calls the progress callback on each provider start/finish.
//
// The returned list is ordered by provider (in the order they are in the
// registry) and within each provider by the provider's own ranking.
class SurfaceScan {
public:
    explicit SurfaceScan(SecretStore* store);

    // `imagePath` must be a local file path.
    // `cancel` is polled before each provider; when it returns true the scan
    // stops early and the results collected so far are still returned.
    std::vector<SurfaceResult>
    search(const QString& imagePath,
           const SurfaceProgress& onProgress = nullptr,
           const std::function<bool()>& cancel = nullptr);

    // Fetches an image from a URL and returns it as a pixmap suitable for the
    // results grid. Synchronous and bounded by a timeout, so it is safe to
    // call from the GUI thread for a handful of thumbnails. Returns a null
    // pixmap when the URL is empty, unreachable, or not a decodable image.
    static QPixmap thumbnailPixmap(const QString& url, int maxSize = 128);

private:
    SecretStore* store_;
};

}  // namespace app
