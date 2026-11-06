#pragma once

#include <QByteArray>
#include <QString>
#include <functional>
#include <vector>

namespace app {

class SecretStore;

// One web hit returned by a provider.
struct SurfaceResult {
    QString provider;   // the provider id, e.g. "google_vision"
    QString providerName;  // display name
    QString title;
    QString url;
    QString thumbnail;      // image URL reported by the provider, may be empty
    QByteArray thumbnailData;  // small PNG fetched by the worker; decoded on the GUI thread
    double similarity = 0.0;  // 0-1 where the provider gives a score; -1 when unknown
    QString extra;         // provider-specific note (e.g. trace.moe episode/timestamps)
    QString error;        // non-empty when this provider returned no usable result
};

// Progress callback: (done, total, currentProvider).
using SurfaceProgress = std::function<void(int done, int total, const QString& current)>;

// Runs the reverse-image search across every enabled provider for an image
// file on disk. Calls the progress callback on each provider start/finish.
//
// Network work (provider requests and thumbnail downloads) all happens on the
// calling thread, so callers should run this off the GUI thread.
//
// The returned list is ordered by provider (in the order they are in the
// registry) and within each provider by the provider's own ranking.
class SurfaceScan {
public:
    explicit SurfaceScan(SecretStore* store);

    // `imagePath` must be a local file path.
    // `cancel` is polled before each provider and while thumbnails download;
    // when it returns true the scan stops early and the results collected so
    // far are still returned.
    std::vector<SurfaceResult>
    search(const QString& imagePath,
           const SurfaceProgress& onProgress = nullptr,
           const std::function<bool()>& cancel = nullptr);

private:
    SecretStore* store_;
};

}  // namespace app
