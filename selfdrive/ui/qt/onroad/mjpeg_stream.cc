#include "selfdrive/ui/qt/onroad/mjpeg_stream.h"
#include <QDebug>
#include <QRegularExpression>
#include <QTimer>

MjpegStream::MjpegStream(QObject *parent)
  : QObject(parent), nam(new QNetworkAccessManager(this)), reply(nullptr),
    active(false), boundary_found(false), frame_callback(nullptr) {
}

MjpegStream::~MjpegStream() {
  stop();
}

void MjpegStream::start(const QString &url) {
  if (active) stop();

  active = true;
  boundary_found = false;
  buffer.clear();
  boundary.clear();

  QNetworkRequest request;
  request.setUrl(QUrl(url));
  request.setHeader(QNetworkRequest::UserAgentHeader, "openpilot-mjpeg/1.0");
  request.setRawHeader("Connection", "keep-alive");

  reply = nam->get(request);
  connect(reply, &QNetworkReply::readyRead, this, &MjpegStream::onReadyRead);
  connect(reply, &QNetworkReply::finished, this, &MjpegStream::onFinished);
  connect(reply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::error),
          this, &MjpegStream::onError);
}

void MjpegStream::stop() {
  if (!active) return;

  active = false;
  if (reply) {
    reply->abort();
    reply->deleteLater();
    reply = nullptr;
  }
  buffer.clear();
  boundary.clear();
  boundary_found = false;
}

void MjpegStream::onReadyRead() {
  if (!reply || !active) return;

  buffer.append(reply->readAll());

  if (!boundary_found) {
    QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    QRegularExpression re("boundary=([^;\\s]+)");
    QRegularExpressionMatch match = re.match(contentType);
    boundary = match.hasMatch() ?
      ("--" + match.captured(1).trimmed()).toUtf8() :
      QByteArray("--boundarydonotcross");
    boundary_found = true;
  }

  processBuffer();
}

void MjpegStream::processBuffer() {
  if (!boundary_found) return;

  while (true) {
    int start = buffer.indexOf(boundary);
    if (start == -1) return;

    int next = buffer.indexOf(boundary, start + boundary.size());
    if (next == -1) return;

    QByteArray part = buffer.mid(start + boundary.size(), next - (start + boundary.size()));
    buffer = buffer.mid(next);

    if (part.startsWith("\r\n")) part = part.mid(2);

    int header_end = part.indexOf("\r\n\r\n");
    if (header_end == -1) continue;

    QByteArray frame_data = part.mid(header_end + 4);
    extractFrame(frame_data);
  }
}

void MjpegStream::extractFrame(const QByteArray &frame_data) {
  if (!active || frame_data.isEmpty()) return;

  QPixmap new_frame;
  if (new_frame.loadFromData(frame_data, "JPEG")) {
    current_frame = new_frame;
    if (frame_callback) frame_callback();
  }
}

void MjpegStream::onFinished() {
  if (active) {
    QTimer::singleShot(1000, this, [this]() {
      if (active && reply) {
        start(reply->url().toString());
      }
    });
  }
}

void MjpegStream::onError(QNetworkReply::NetworkError error) {
  qWarning() << "[MjpegStream] Network error:" << error;
}