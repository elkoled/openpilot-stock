#include "selfdrive/ui/qt/onroad/mjpeg_stream.h"
#include <QDebug>
#include <QRegularExpression>
#include <QTimer>
#include <QMetaObject>

MjpegStream::MjpegStream(QObject *parent)
  : QObject(parent), nam(new QNetworkAccessManager(this)), reply(nullptr),
    active(false), boundary_found(false), content_length(0), reading_headers(true),
    frame_callback(nullptr) {
}

MjpegStream::~MjpegStream() {
  stop();
}

void MjpegStream::start(const QString &url) {
  if (active) {
    stop();
  }

  qDebug() << "[MjpegStream] Starting stream from:" << url;

  active = true;
  boundary_found = false;
  reading_headers = true;
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

  qDebug() << "[MjpegStream] Stopping stream";
  active = false;

  if (reply) {
    reply->abort();
    reply->deleteLater();
    reply = nullptr;
  }

  buffer.clear();
  boundary.clear();
  boundary_found = false;
  reading_headers = true;
}

void MjpegStream::onReadyRead() {
  if (!reply || !active) return;

  QByteArray data = reply->readAll();
  buffer.append(data);

  // Extract boundary from content-type header if not found yet
  if (!boundary_found) {
    QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    QRegularExpression re("boundary=([^;\\s]+)");
    QRegularExpressionMatch match = re.match(contentType);
    if (match.hasMatch()) {
      boundary = ("--" + match.captured(1)).toUtf8();
      boundary_found = true;
      qDebug() << "[MjpegStream] Found boundary:" << boundary;
    }
  }

  if (boundary_found) {
    processBuffer();
  }
}

void MjpegStream::processBuffer() {
  while (true) {
    if (reading_headers) {
      // Look for boundary
      int boundary_pos = buffer.indexOf(boundary);
      if (boundary_pos == -1) break;

      // Remove everything before boundary
      buffer = buffer.mid(boundary_pos + boundary.length());

      // Look for end of headers (double CRLF)
      int header_end = buffer.indexOf("\r\n\r\n");
      if (header_end == -1) break;

      // Extract headers
      QByteArray headers = buffer.left(header_end);
      buffer = buffer.mid(header_end + 4);

      // Parse Content-Length
      QRegularExpression re("Content-Length:\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption);
      QRegularExpressionMatch match = re.match(QString::fromUtf8(headers));
      if (match.hasMatch()) {
        content_length = match.captured(1).toInt();
        reading_headers = false;
      } else {
        qWarning() << "[MjpegStream] No Content-Length found in headers";
        continue;
      }
    } else {
      // Reading frame data
      if (buffer.length() < content_length) break;

      // Extract frame
      QByteArray frame_data = buffer.left(content_length);
      buffer = buffer.mid(content_length);

      extractFrame(frame_data);
      reading_headers = true;
    }
  }
}

void MjpegStream::extractFrame(const QByteArray &frame_data) {
  if (!active) return;

  QPixmap new_frame;
  if (new_frame.loadFromData(frame_data, "JPEG")) {
    current_frame = new_frame;
    if (frame_callback) {
      frame_callback();
    }
  } else {
    qWarning() << "[MjpegStream] Failed to load JPEG frame, data size:" << frame_data.size();
  }
}

void MjpegStream::onFinished() {
  qDebug() << "[MjpegStream] Stream finished";
  if (active) {
    // Try to reconnect after a short delay
    QTimer::singleShot(1000, this, [this]() {
      if (active && reply) {
        QString url = reply->url().toString();
        start(url);
      }
    });
  }
}

void MjpegStream::onError(QNetworkReply::NetworkError error) {
  qWarning() << "[MjpegStream] Network error:" << error << reply->errorString();
}