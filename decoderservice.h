#ifndef DIA_DECODERSERVICE_H_
#define DIA_DECODERSERVICE_H_

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

#include "decoder.h"

// Owns the FFmpegDecoder and lives in a worker thread. Callers use blocking
// invocations for now; asynchronous seek/preview will be layered on top.
class DecoderService : public QObject {
  Q_OBJECT
 public:
  explicit DecoderService(QObject* parent = nullptr);
  ~DecoderService() override;

 public slots:
  QVariantMap Open(const QString& path, int codec, QVariantList raw_keyframes);
  QVariantMap Close();
  QVariantMap Seek(int frame);
  QVariantMap DecodeNext();

 private:
  std::unique_ptr<FFmpegDecoder> decoder_;
};

#endif  // DIA_DECODERSERVICE_H_
