#pragma once

#include <QObject>
#include <QPixmap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <functional>

class MjpegStream : public QObject {
  Q_OBJECT

public:
  explicit MjpegStream(QObject *parent = nullptr);
  ~MjpegStream();

  void start(const QString &url);
  void stop();
  bool isActive() const { return active; }
  bool hasFrame() const { return !current_frame.isNull(); }
  const QPixmap &frame() const { return current_frame; }
  void setFrameCallback(std::function<void()> callback) { frame_callback = callback; }

private slots:
  void onReadyRead();
  void onFinished();
  void onError(QNetworkReply::NetworkError error);

private:
  void processBuffer();
  void extractFrame(const QByteArray &frame_data);

  QNetworkAccessManager *nam;
  QNetworkReply *reply;
  QPixmap current_frame;
  QByteArray buffer;
  QByteArray boundary;
  bool active;
  bool boundary_found;
  std::function<void()> frame_callback;
};