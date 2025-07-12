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

  qWarning() << "[MjpegStream] Requesting stream from:" << url;

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

  connect(reply, &QNetworkReply::metaDataChanged, this, [this]() {
    qWarning() << "[MjpegStream] Headers received:";
    const auto headers = reply->rawHeaderPairs();
    for (const auto &h : headers) {
      qWarning() << " " << h.first << ":" << h.second;
    }
  });

  connect(reply, &QNetworkReply::readyRead, this, &MjpegStream::onReadyRead);
  connect(reply, &QNetworkReply::finished, this, &MjpegStream::onFinished);
  connect(reply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::error),
          this, &MjpegStream::onError);
}

void MjpegStream::stop() {
  if (!active) return;

  qWarning() << "[MjpegStream] Stopping stream";
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

  buffer.append(reply->readAll());

  if (!boundary_found) {
    QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    QRegularExpression re("boundary=([^;\\s]+)");
    QRegularExpressionMatch match = re.match(contentType);
    if (match.hasMatch()) {
      boundary = "--" + match.captured(1).trimmed().toUtf8();
    } else {
      boundary = "--boundarydonotcross";  // fallback for mjpg-streamer
    }
    boundary_found = true;
    qWarning() << "[MjpegStream] Using boundary:" << boundary;
  }
  qWarning() << "[MjpegStream] onReadyRead(), buffer size now:" << buffer.size();

  processBuffer();
}
void MjpegStream::processBuffer() {
  while (true) {
    if (!boundary_found) {
      qWarning() << "[MjpegStream] Boundary not found yet.";
      return;
    }

    int start = buffer.indexOf(boundary);
    if (start == -1) {
      qWarning() << "[MjpegStream] No starting boundary found.";
      return;
    }

    int next = buffer.indexOf(boundary, start + boundary.size());
    if (next == -1) {
      qWarning() << "[MjpegStream] No next boundary found yet. Waiting for more data.";
      return;
    }

    QByteArray part = buffer.mid(start + boundary.size(), next - (start + boundary.size()));
    buffer = buffer.mid(next);  // trim used data

    // Remove leading \r\n
    if (part.startsWith("\r\n")) part = part.mid(2);

    int header_end = part.indexOf("\r\n\r\n");
    if (header_end == -1) {
      qWarning() << "[MjpegStream] Incomplete part headers.";
      continue;
    }

    QByteArray headers = part.left(header_end);
    QByteArray frame_data = part.mid(header_end + 4);

    qWarning() << "[MjpegStream] Got headers:\n" << headers;
    qWarning() << "[MjpegStream] Got frame of size:" << frame_data.size();

    extractFrame(frame_data);
  }
}




void MjpegStream::extractFrame(const QByteArray &frame_data) {
  if (!active) return;

  QPixmap new_frame;
  if (new_frame.loadFromData(frame_data, "JPEG")) {
    current_frame = new_frame;
    if (frame_callback) frame_callback();
    qWarning() << "[MjpegStream] Frame OK, size:" << frame_data.size();
  } else {
    qWarning() << "[MjpegStream] Failed to decode JPEG, size:" << frame_data.size();
  }
}



void MjpegStream::onFinished() {
  qWarning() << "[MjpegStream] Stream finished";
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