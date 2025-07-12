#ifndef WIFICAM_H
#define WIFICAM_H

#include <QPixmap>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class WifiCam : public QObject {
  Q_OBJECT
public:
  explicit WifiCam(QObject *parent = nullptr);
  void start(const QString &url);
  void stop();
  bool isActive() const { return active; }
  bool hasFrame() const { return !pix.isNull(); }
  const QPixmap &frame() const { return pix; }

private slots:
  void fetch();
  void downloaded();

private:
  QNetworkAccessManager nam;
  QTimer timer;
  QString url;
  QPixmap pix;
  QNetworkReply *reply = nullptr;
  bool active = false;
};

#endif // WIFICAM_H