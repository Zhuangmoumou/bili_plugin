#include "BiliNetwork.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QJsonParseError>
#include <QNetworkRequest>
#include <iostream>

BiliNetwork *BiliNetwork::s_instance = nullptr;
QMutex BiliNetwork::s_instanceMutex;

BiliNetwork *BiliNetwork::instance() {
  QMutexLocker locker(&s_instanceMutex);
  if (!s_instance) {
    s_instance = new BiliNetwork(qApp); // parent = QApplication，自动清理
  }
  return s_instance;
}

void BiliNetwork::destroyInstance() {
  QMutexLocker locker(&s_instanceMutex);
  if (s_instance) {
    s_instance->cancelAllRequests();
    delete s_instance;
    s_instance = nullptr;
  }
}

BiliNetwork::BiliNetwork(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this)),
      m_apiBase("http://127.0.0.1:8000"), m_online(true),
      m_requestTimeout(15000) {
  std::cout << "[BiliNet] Initialized with API base: "
            << m_apiBase.toStdString() << std::endl;

  // 设置连接池大小
  m_nam->setTransferTimeout(m_requestTimeout);
}

BiliNetwork::~BiliNetwork() { cancelAllRequests(); }

void BiliNetwork::cancelVideoDownload() {
  QPointer<QNetworkReply> replyToAbort;
  {
    QMutexLocker locker(&m_replyMutex);
    replyToAbort = m_videoDownloadReply;
  }

  if (replyToAbort) {
    qDebug() << "[BiliNetwork] Aborting video download...";
    replyToAbort->abort();
  }
}

void BiliNetwork::setApiBase(const QString &base) {
  m_apiBase = base;
  if (m_apiBase.endsWith('/')) {
    m_apiBase.chop(1);
  }
}

QString BiliNetwork::apiBase() const { return m_apiBase; }

void BiliNetwork::setSessionCookie(const QString &sessdata) {
  QMutexLocker locker(&m_cookieMutex);
  m_sessdata = sessdata;
}

QString BiliNetwork::sessionCookie() const {
  QMutexLocker locker(const_cast<QMutex *>(&m_cookieMutex));
  return m_sessdata;
}

bool BiliNetwork::isLoggedIn() const {
  QMutexLocker locker(const_cast<QMutex *>(&m_cookieMutex));
  return !m_sessdata.isEmpty();
}

bool BiliNetwork::isOnline() const { return m_online; }

int BiliNetwork::activeRequestCount() const {
  QMutexLocker locker(const_cast<QMutex *>(&m_replyMutex));
  return m_activeReplies.size();
}

bool BiliNetwork::checkRateLimit() {
  QMutexLocker locker(&m_rateMutex);

  qint64 now = QDateTime::currentMSecsSinceEpoch();

  // 清理 1 秒前的时间戳
  while (!m_requestTimestamps.isEmpty() &&
         (now - m_requestTimestamps.first()) > 1000) {
    m_requestTimestamps.removeFirst();
  }

  if (m_requestTimestamps.size() >= MAX_REQUESTS_PER_SECOND) {
    std::cout << "[BiliNet] Rate limit exceeded!" << std::endl;
    return false;
  }

  m_requestTimestamps.append(now);
  return true;
}

void BiliNetwork::trackReply(QNetworkReply *reply) {
  QMutexLocker locker(&m_replyMutex);
  m_activeReplies.insert(reply);
}

void BiliNetwork::untrackReply(QNetworkReply *reply) {
  QMutexLocker locker(&m_replyMutex);
  m_activeReplies.remove(reply);
}

void BiliNetwork::cancelAllRequests() {
  QMutexLocker locker(&m_replyMutex);
  for (QNetworkReply *reply : qAsConst(m_activeReplies)) {
    if (reply && reply->isRunning()) {
      reply->abort();
    }
  }
  // 不在这里删除，让 finished 信号处理清理
  std::cout << "[BiliNet] Cancelled " << m_activeReplies.size()
            << " active requests" << std::endl;
}

void BiliNetwork::get(const QString &path, const QMap<QString, QString> &params,
                      SuccessCallback onSuccess, ErrorCallback onError) {
  // 并发限制
  {
    QMutexLocker locker(&m_replyMutex);
    if (m_activeReplies.size() >= MAX_CONCURRENT_REQUESTS) {
      std::cout << "[BiliNet] Too many concurrent requests!" << std::endl;
      if (onError) {
        onError(-10, "请求过于频繁，请稍后重试");
      }
      return;
    }
  }

  // 速率限制
  if (!checkRateLimit()) {
    if (onError) {
      onError(-11, "请求过于频繁");
    }
    return;
  }

  QUrl url(m_apiBase + path);
  QUrlQuery query;
  for (auto it = params.begin(); it != params.end(); ++it) {
    query.addQueryItem(it.key(), it.value());
  }
  url.setQuery(query);

  if (!url.isValid()) {
    std::cout << "[BiliNet] Invalid URL constructed" << std::endl;
    if (onError) {
      onError(-12, "无效的请求地址");
    }
    return;
  }

  std::cout << "[BiliNet] GET " << url.toString().toStdString() << std::endl;

  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
  // 使用正常浏览器 UA，避免风控
  request.setRawHeader("User-Agent",
                       "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
  request.setRawHeader("Referer", "https://www.bilibili.com");

  QNetworkReply *reply = m_nam->get(request);
  if (!reply) {
    if (onError) {
      onError(-13, "创建网络请求失败");
    }
    return;
  }

  trackReply(reply);

  // 超时处理 — 使用 QPointer 防止悬空指针
  QPointer<QNetworkReply> safeReply(reply);
  QTimer *timer = new QTimer(this);
  timer->setSingleShot(true);

  connect(timer, &QTimer::timeout, this, [safeReply, timer, onError]() {
    if (safeReply && safeReply->isRunning()) {
      safeReply->abort();
      std::cout << "[BiliNet] Request timeout!" << std::endl;
      // 不在这里调用 onError，让 finished 处理
    }
    timer->deleteLater();
  });
  timer->start(m_requestTimeout);

  connect(reply, &QNetworkReply::finished, this,
          [this, reply, onSuccess, onError, timer]() {
            timer->stop();
            timer->deleteLater();
            untrackReply(reply);
            handleReply(reply, onSuccess, onError);
          });
}

void BiliNetwork::getWithAuth(const QString &path,
                              const QMap<QString, QString> &params,
                              SuccessCallback onSuccess,
                              ErrorCallback onError) {
  QString sessdata;
  {
    QMutexLocker locker(&m_cookieMutex);
    sessdata = m_sessdata;
  }

  if (sessdata.isEmpty()) {
    if (onError) {
      onError(-2, "未登录，请先扫码登录");
    }
    return;
  }

  // 并发和速率限制
  {
    QMutexLocker locker(&m_replyMutex);
    if (m_activeReplies.size() >= MAX_CONCURRENT_REQUESTS) {
      if (onError)
        onError(-10, "请求过于频繁");
      return;
    }
  }
  if (!checkRateLimit()) {
    if (onError)
      onError(-11, "请求过于频繁");
    return;
  }

  QUrl url(m_apiBase + path);
  QUrlQuery query;
  for (auto it = params.begin(); it != params.end(); ++it) {
    query.addQueryItem(it.key(), it.value());
  }
  url.setQuery(query);

  if (!url.isValid()) {
    if (onError)
      onError(-12, "无效的请求地址");
    return;
  }

  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
  // 使用正常浏览器 UA，避免风控
  request.setRawHeader("User-Agent",
                       "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
  request.setRawHeader("Referer", "https://www.bilibili.com");
  request.setRawHeader("Cookie", QString("SESSDATA=%1").arg(sessdata).toUtf8());

  qDebug() << "[BiliNet] GET (auth)" << url.toString();

  QNetworkReply *reply = m_nam->get(request);
  if (!reply) {
    if (onError)
      onError(-13, "创建网络请求失败");
    return;
  }

  trackReply(reply);

  QPointer<QNetworkReply> safeReply(reply);
  QTimer *timer = new QTimer(this);
  timer->setSingleShot(true);
  connect(timer, &QTimer::timeout, this, [safeReply, timer]() {
    if (safeReply && safeReply->isRunning()) {
      safeReply->abort();
    }
    timer->deleteLater();
  });
  timer->start(m_requestTimeout);

  connect(reply, &QNetworkReply::finished, this,
          [this, reply, onSuccess, onError, timer]() {
            timer->stop();
            timer->deleteLater();
            untrackReply(reply);
            handleReply(reply, onSuccess, onError);
          });
}

void BiliNetwork::downloadImage(const QUrl &url, RawCallback onSuccess,
                                ErrorCallback onError) {
  if (!url.isValid()) {
    if (onError)
      onError(-3, "无效的图片地址");
    return;
  }

  QNetworkRequest request(url);
  request.setRawHeader("Referer", "https://www.bilibili.com");
  // 使用正常浏览器 UA，避免风控
  request.setRawHeader("User-Agent",
                       "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
  request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);

  QNetworkReply *reply = m_nam->get(request);
  if (!reply) {
    if (onError)
      onError(-13, "创建请求失败");
    return;
  }

  trackReply(reply);

  // 限制下载大小
  constexpr qint64 MAX_IMAGE_SIZE = 10 * 1024 * 1024; // 10MB

  connect(reply, &QNetworkReply::downloadProgress,
          [reply](qint64 received, qint64 total) {
            Q_UNUSED(total)
            if (received > MAX_IMAGE_SIZE) {
              reply->abort();
            }
          });

  QPointer<QNetworkReply> safeReply(reply);
  QTimer *timer = new QTimer(this);
  timer->setSingleShot(true);
  connect(timer, &QTimer::timeout, this, [safeReply, timer]() {
    if (safeReply && safeReply->isRunning()) {
      safeReply->abort();
    }
    timer->deleteLater();
  });
  timer->start(10000);

  connect(reply, &QNetworkReply::finished, this,
          [this, reply, onSuccess, onError, timer]() {
            timer->stop();
            timer->deleteLater();
            untrackReply(reply);

            if (reply->error() == QNetworkReply::NoError) {
              QByteArray data = reply->readAll();
              if (data.isEmpty()) {
                if (onError)
                  onError(-4, "图片数据为空");
              } else {
                if (onSuccess)
                  onSuccess(data);
              }
            } else {
              if (onError) {
                onError(reply->error(), reply->errorString());
              }
            }
            reply->deleteLater();
          });
}

void BiliNetwork::downloadVideo(const QString &url, const QString &targetPath,
                                std::function<void(const QString &path)> onSuccess,
                                std::function<void(int code, const QString &msg)> onError,
                                std::function<void(qint64 received, qint64 total)> onProgress) {
  QUrl downloadUrl(url);
  if (!downloadUrl.isValid()) {
    if (onError)
      onError(-3, "无效的视频地址");
    return;
  }

  QNetworkRequest request(downloadUrl);
  request.setRawHeader("Referer", "https://www.bilibili.com");
  // 使用正常浏览器 UA，避免风控
  request.setRawHeader("User-Agent",
                       "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
  request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);

  QNetworkReply *reply = m_nam->get(request);
  if (!reply) {
    if (onError)
      onError(-13, "创建请求失败");
    return;
  }

  // 记录下载任务
  {
    QMutexLocker locker(&m_replyMutex);
    m_videoDownloadReply = reply;
  }

  trackReply(reply);

  // 创建临时文件
  QFile *file = new QFile(targetPath, this);
  if (!file->open(QIODevice::WriteOnly)) {
    std::cout << "[BiliNet] Failed to create temp file: "
              << targetPath.toStdString() << std::endl;
    if (onError)
      onError(-15, "无法创建临时文件");
    
    // 清理记录
    {
      QMutexLocker locker(&m_replyMutex);
      m_videoDownloadReply = nullptr;
    }
    
    reply->abort();
    reply->deleteLater();
    delete file;
    return;
  }

  std::cout << "[BiliNet] Downloading video to: "
            << targetPath.toStdString() << std::endl;

  // 连接下载进度
  connect(reply, &QNetworkReply::downloadProgress,
          [onProgress](qint64 received, qint64 total) {
            if (onProgress) {
              onProgress(received, total);
            }
          });

  // 在数据可读时写入文件
  connect(reply, &QNetworkReply::readyRead, [file, reply]() {
    if (file && file->isOpen() && reply) {
        if (reply->bytesAvailable() > 0) {
            QByteArray data = reply->readAll();
            if (!data.isEmpty()) {
                file->write(data);
            }
        }
    }
  });

  QPointer<QNetworkReply> safeReply(reply);
  QTimer *timer = new QTimer(this);
  timer->setSingleShot(true);
  connect(timer, &QTimer::timeout, this, [safeReply, timer]() {
    if (safeReply && safeReply->isRunning()) {
      safeReply->abort();
    }
    timer->deleteLater();
  });
  // 视频下载超时时间设为 5 分钟
  timer->start(300000);

  connect(reply, &QNetworkReply::finished, this,
          [this, reply, file, targetPath, onSuccess, onError, timer]() {
            timer->stop();
            timer->deleteLater();
            untrackReply(reply);

            // 确保在 finished 信号处理结束时，清理对 reply 的跟踪
            {
                QMutexLocker locker(&m_replyMutex);
                if (m_videoDownloadReply == reply) {
                    m_videoDownloadReply = nullptr;
                }
            }

            // 写入剩余数据
            if (file && file->isOpen()) {
                if (reply->bytesAvailable() > 0) {
                    QByteArray data = reply->readAll();
                    if (!data.isEmpty()) {
                        file->write(data);
                    }
                }
                file->close();
            }

            if (reply->error() == QNetworkReply::NoError) {
              std::cout << "[BiliNet] Video downloaded successfully: "
                        << targetPath.toStdString() << std::endl;
              if (onSuccess) {
                onSuccess(targetPath);
              }
            } else {
              std::cout << "[BiliNet] Download failed: "
                        << reply->errorString().toStdString() << std::endl;
              // 删除失败的文件
              file->remove();
              if (onError) {
                onError(reply->error(), reply->errorString());
              }
            }
            reply->deleteLater();
            file->deleteLater();
          });
}

void BiliNetwork::handleReply(QNetworkReply *reply, SuccessCallback onSuccess,
                              ErrorCallback onError) {
  if (!reply) {
    if (onError)
      onError(-99, "无效的网络回复");
    return;
  }

  // 确保 reply 最终被删除
  struct ReplyGuard {
    QNetworkReply *r;
    ~ReplyGuard() {
      if (r)
        r->deleteLater();
    }
  } guard{reply};

  // 网络层错误
  if (reply->error() != QNetworkReply::NoError) {
    QString errorMsg;
    int errorCode = reply->error();

    switch (reply->error()) {
    case QNetworkReply::OperationCanceledError:
    case QNetworkReply::TimeoutError:
      errorMsg = "网络请求超时";
      break;
    case QNetworkReply::ConnectionRefusedError:
      errorMsg = "无法连接到服务器";
      break;
    case QNetworkReply::HostNotFoundError:
      errorMsg = "服务器地址未找到";
      break;
    case QNetworkReply::ContentNotFoundError:
      errorMsg = "请求的内容不存在";
      break;
    default:
      errorMsg = QString("网络错误：%1").arg(reply->errorString());
      break;
    }

    std::cout << "[BiliNet] Error: " << errorCode << " "
              << errorMsg.toStdString() << std::endl;

    if (onError) {
      onError(errorCode, errorMsg);
    }
    emit networkError(errorMsg);
    return;
  }

  // 读取数据，限制最大大小
  constexpr qint64 MAX_RESPONSE_SIZE = 50 * 1024 * 1024; // 50MB
  QByteArray rawData = reply->readAll();

  std::cout << "[BiliNet] Response received: " << rawData.size() << " bytes"
            << std::endl;

  if (rawData.isEmpty()) {
    if (onError)
      onError(-5, "服务器返回空数据");
    return;
  }

  if (rawData.size() > MAX_RESPONSE_SIZE) {
    if (onError)
      onError(-14, "响应数据过大");
    return;
  }

  // JSON 解析
  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(rawData, &parseError);

  if (parseError.error != QJsonParseError::NoError) {
    std::cout << "[BiliNet] JSON parse error: "
              << parseError.errorString().toStdString() << std::endl;
    if (onError) {
      onError(-6, QString("数据解析错误：%1").arg(parseError.errorString()));
    }
    return;
  }

  if (!doc.isObject()) {
    if (onError)
      onError(-7, "服务器返回格式异常");
    return;
  }

  QJsonObject root = doc.object();

  // Bilibili API 业务层错误检查
  int code = root.value("code").toInt(-999);
  QString message = root.value("message").toString("Unknown error");

  if (code != 0) {
    std::cout << "[BiliNet] API error: " << code << " " << message.toStdString()
              << std::endl;
    if (onError) {
      onError(code, message);
    }
    return;
  }

  // 成功：提取 data 字段
  QJsonObject data = root.value("data").toObject();
  if (onSuccess) {
    onSuccess(data);
  }
}