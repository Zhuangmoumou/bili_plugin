#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <functional>

class BiliNetwork : public QObject {
  Q_OBJECT

public:
  // 回调类型定义
  using SuccessCallback = std::function<void(const QJsonObject &data)>;
  using ErrorCallback = std::function<void(int code, const QString &message)>;
  using RawCallback = std::function<void(const QByteArray &data)>;

  static BiliNetwork *instance();
  static void destroyInstance();

  // 基础请求
  void get(const QString &path, const QMap<QString, QString> &params,
           SuccessCallback onSuccess, ErrorCallback onError = nullptr);

  // 带 Cookie 请求
  void getWithAuth(const QString &path, const QMap<QString, QString> &params,
                   SuccessCallback onSuccess, ErrorCallback onError = nullptr);

  // 下载图片
  void downloadImage(const QUrl &url, RawCallback onSuccess,
                     ErrorCallback onError = nullptr);

  // 下载视频到临时文件
  void downloadVideo(const QString &url, const QString &targetPath,
                     std::function<void(const QString &path)> onSuccess,
                     std::function<void(int code, const QString &msg)> onError,
                     std::function<void(qint64 received, qint64 total)> onProgress = nullptr);

  // 设置/获取 API 地址
  void setApiBase(const QString &base);
  QString apiBase() const;

  // 登录 Cookie 管理
  void setSessionCookie(const QString &sessdata);
  QString sessionCookie() const;
  bool isLoggedIn() const;

  // 网络状态
  bool isOnline() const;

  // 取消所有正在进行的请求
  Q_INVOKABLE void cancelAllRequests();
  // 单独取消视频下载
  void cancelVideoDownload();

  // 并发请求限制
  int activeRequestCount() const;

signals:
  void networkError(const QString &message);
  void onlineStateChanged(bool online);

private:
  explicit BiliNetwork(QObject *parent = nullptr);
  ~BiliNetwork();

  // 禁止拷贝
  BiliNetwork(const BiliNetwork &) = delete;
  BiliNetwork &operator=(const BiliNetwork &) = delete;

  void handleReply(QNetworkReply *reply, SuccessCallback onSuccess,
                   ErrorCallback onError);

  bool checkRateLimit();
  void trackReply(QNetworkReply *reply);
  void untrackReply(QNetworkReply *reply);

  QNetworkAccessManager *m_nam;
  QString m_apiBase;
  QString m_sessdata;
  bool m_online;
  int m_requestTimeout;

  // 请求跟踪（用于取消和防止泄漏）
  QMutex m_replyMutex;
  QSet<QNetworkReply *> m_activeReplies;
  QPointer<QNetworkReply> m_videoDownloadReply;

  // 速率限制
  QMutex m_rateMutex;
  QVector<qint64> m_requestTimestamps;
  static constexpr int MAX_REQUESTS_PER_SECOND = 10;
  static constexpr int MAX_CONCURRENT_REQUESTS = 20;

  // Cookie 线程安全
  mutable QMutex m_cookieMutex;

  static BiliNetwork *s_instance;
  static QMutex s_instanceMutex;
};