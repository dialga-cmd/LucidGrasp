#pragma once

#include <QImage>
#include <QString>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Ort {

class Session;

}  // namespace Ort

namespace core {

// BiRefNet-general background removal via ONNX Runtime.
class BackgroundRemover {
public:
  BackgroundRemover();
  ~BackgroundRemover();

  BackgroundRemover(const BackgroundRemover &) = delete;
  BackgroundRemover &operator=(const BackgroundRemover &) = delete;

  // Loads the ONNX model from disk. Safe to call again to replace the model.
  bool loadModel(const QString &modelPath, QString *error = nullptr);

  bool isLoaded() const;
  QString modelPath() const;

  // Preprocessing runs before any session exists, so it can be tested even
  // without a model file. Returns false on unsupported input.
  static bool loadInput(const QImage &image, int modelSize,
                        std::vector<float> *tensor);

  // Removes the background of `image` using the loaded model and returns the
  // cutout as an RGBA image. Only one inference may run at a time.
  bool removeBackground(const QImage &image, QImage *cutout,
                        QString *error = nullptr);

private:
  mutable std::mutex mutex_;
  std::unique_ptr<Ort::Session> session_;
  QString modelPath_;
  std::string inputName_;
  std::string outputName_;
  int inputWidth_ = 1024;
  int inputHeight_ = 1024;
};

// Longest edge preserved when loading a source image; anything bigger is
// downscaled during decode so it stays far below Qt's default 128 MB
// allocation guard. The model only ever consumes a fixed-size copy, so this
// costs no model fidelity; cutouts of oversized sources are capped here.
constexpr int kBackgroundSourceMaxDim = 4096;

// Loads an image from disk with EXIF orientation applied, downscaling very
// large sources (e.g. ones PNG handlers cannot decode scaled) to at most
// kBackgroundSourceMaxDim. Returns a null image when the file cannot be read.
QImage loadImageForBackground(const QString &path);

// Composites a cutout onto a flat mid-gray field. Without this, fully
// transparent pixels still carry their original background RGB, which would
// leak into search features; flat gray also adds no ORB corners and lands in
// a single achromatic histogram bin. Returns an empty image for a null input.
QImage matteOf(const QImage &cutout);

// Name of the background model file inside the models directory.
QString modelFileName();

// Where the engine expects to find the model file, or an empty string when
// none of the known locations holds one.
QString defaultModelPath();

}  // namespace core