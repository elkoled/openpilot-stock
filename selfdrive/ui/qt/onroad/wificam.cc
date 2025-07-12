#include "selfdrive/ui/qt/onroad/wificam.h"

WifiCam::WifiCam(QObject *parent) : QObject(parent) {
  connect(&timer, &QTimer::timeout, this, &WifiCam::fetch);
}

void WifiCam::start(const QString &u) {
  url = u;
  active = true;
  timer.start(100); // ~10 fps
  fetch();
}

void WifiCam::stop() {
  active = false;
  timer.stop();
  if (reply) {
    reply->abort();
    reply->deleteLater();
    reply = nullptr;
  }
}

void WifiCam::fetch() {
  if (!active) return;
  if (reply) return;
  reply = nam.get(QNetworkRequest(QUrl(url)));
  connect(reply, &QNetworkReply::finished, this, &WifiCam::downloaded);
}

void WifiCam::downloaded() {
  if (reply->error() == QNetworkReply::NoError) {
    QByteArray data = reply->readAll();
    qWarning() << "[WifiCam] Got" << data.size() << "bytes";
    if (!pix.loadFromData(data)) {
      qWarning() << "[WifiCam] Failed to load image from data";
    } else {
      qWarning() << "[WifiCam] Image loaded OK";
    }
  } else {
    qWarning() << "[WifiCam] Error:" << reply->errorString();
  }
  reply->deleteLater();
  reply = nullptr;
}