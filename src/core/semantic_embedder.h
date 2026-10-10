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

// DINOv2-small (int8) semantic embedding via ONNX Runtime. An embedding is a
// compact feature vector that captures *what* is in the image rather than its
// pixels; cosine similarity between two embeddings measures how related the
// contents are. Backgrounds barely matter for an embedding, so they are
// computed on the plain photo and no background removal is involved.
class SemanticEmbedder {
public:
  static constexpr int kDim = 384;
  static constexpr int kInputSize = 224;

  SemanticEmbedder();
  ~SemanticEmbedder();

  SemanticEmbedder(const SemanticEmbedder &) = delete;
  SemanticEmbedder &operator=(const SemanticEmbedder &) = delete;

  // Loads the ONNX model from disk. Safe to call again to replace the model.
  bool loadModel(const QString &modelPath, QString *error = nullptr);

  // Releases the loaded model and frees its memory.
  void unload();

  bool isLoaded() const;
  QString modelPath() const;

  // Center-crop-to-224 resize plus ImageNet normalization, laid out in the
  // "channel, height, width" order the model consumes. Preprocessing runs
  // before any session exists, so it can be tested without a model file.
  // Returns false on unsupported input.
  static bool loadInput(const QImage &image, std::vector<float> *tensor);

  // Embeds `image` into a length-normalized vector of kDim floats. Only one
  // inference may run at a time. Returns false when nothing comes out.
  bool embed(const QImage &image, std::vector<float> *out,
             QString *error = nullptr) const;

private:
  mutable std::mutex mutex_;
  std::unique_ptr<Ort::Session> session_;
  QString modelPath_;
  std::string inputName_;
  std::string outputName_;
};

// Name of the semantic model file inside the models directory.
QString semanticModelFileName();

// Where the engine expects to find the semantic model file, or an empty
// string when none of the known locations holds one. Honors the
// LUCIDGRASP_SEMANTIC_MODEL environment variable.
QString defaultSemanticModelPath();

}  // namespace core