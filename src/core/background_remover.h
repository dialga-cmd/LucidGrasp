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

// Name of the background model file inside the models directory.
QString modelFileName();

// Where the engine expects to find the model file, or an empty string when
// none of the known locations holds one.
QString defaultModelPath();

}  // namespace core