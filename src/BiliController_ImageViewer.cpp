#include "BiliController.h"
#include "BiliNetwork.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QUrl>

static QString biliViewerImageExtFromUrl(const QString &url) {
  QString path = QUrl(url).path().toLower();
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return ".jpg";
  if (path.endsWith(".png")) return ".png";
  if (path.endsWith(".webp")) return ".webp";
  if (path.endsWith(".gif")) return ".gif";
  if (path.endsWith(".bmp")) return ".bmp";
  return ".jpg";
}

void BiliController::prepareImageForViewer(const QString &url) {
  QString u = url.trimmed();
  if (u.isEmpty()) {
    emit toastMessage("图片地址为空");
    return;
  }

  if (u.startsWith("//")) u = "https:" + u;
  if (!u.startsWith("http://") && !u.startsWith("https://") && !u.startsWith("file://") && !QFileInfo(u).isAbsolute()) {
    u = "https://" + u;
  }

  // 本地文件直接交给查看器
  if (u.startsWith("file://")) {
    emit commentImageReadyForViewer(QUrl(u).toLocalFile());
    return;
  }
  if (QFileInfo(u).isAbsolute() && QFileInfo(u).exists()) {
    emit commentImageReadyForViewer(u);
    return;
  }

  QUrl qurl(u);
  if (!qurl.isValid()) {
    emit toastMessage("无效的图片地址");
    return;
  }

  QDir dir("/tmp/bili_plugin_images");
  if (!dir.exists() && !dir.mkpath(".")) {
    emit toastMessage("无法创建图片缓存目录");
    return;
  }

  QByteArray hash = QCryptographicHash::hash(u.toUtf8(), QCryptographicHash::Md5).toHex();
  QString path = dir.absoluteFilePath(QString::fromLatin1(hash) + biliViewerImageExtFromUrl(u));

  if (QFileInfo(path).exists() && QFileInfo(path).size() > 0) {
    emit commentImageReadyForViewer(path);
    return;
  }

  emit toastMessage("正在打开图片...");

  QPointer<BiliController> self(this);
  m_network->downloadImage(
      qurl,
      [self, path](const QByteArray &data) {
        if (!self) return;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
          emit self->toastMessage("图片缓存失败");
          return;
        }
        file.write(data);
        file.close();
        emit self->commentImageReadyForViewer(path);
      },
      [self](int, const QString &message) {
        if (!self) return;
        emit self->toastMessage("图片打开失败：" + message);
      });
}
