#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QFile>
#include <QStandardPaths>
#include <QNetworkReply>
#include <QVariantList>

#include "BiliModels.h"

class VideoPartListModel;
class VideoListModel;
class CommentListModel;
class HotSearchModel;
class SearchResultModel;
class QStringListModel;
class FavoriteFolderModel;
class BiliNetwork;

class BiliController : public QObject {
  Q_OBJECT

  // 当前页面状态
  Q_PROPERTY(QString currentPage READ currentPage WRITE setCurrentPage NOTIFY
                 currentPageChanged)

  // 视频详情
  Q_PROPERTY(QString videoTitle READ videoTitle NOTIFY videoDetailChanged)
  Q_PROPERTY(QString videoDesc READ videoDesc NOTIFY videoDetailChanged)
  Q_PROPERTY(QString videoPic READ videoPic NOTIFY videoDetailChanged)
  Q_PROPERTY(QString videoOwner READ videoOwner NOTIFY videoDetailChanged)
  Q_PROPERTY(
      QString videoOwnerFace READ videoOwnerFace NOTIFY videoDetailChanged)
  Q_PROPERTY(QString videoViews READ videoViews NOTIFY videoDetailChanged)
  Q_PROPERTY(QString videoLikes READ videoLikes NOTIFY videoDetailChanged)
  Q_PROPERTY(QString videoCoins READ videoCoins NOTIFY videoDetailChanged)
  Q_PROPERTY(
      QString videoFavorites READ videoFavorites NOTIFY videoDetailChanged)
  Q_PROPERTY(QString videoDanmaku READ videoDanmaku NOTIFY videoDetailChanged)
  Q_PROPERTY(QString videoDuration READ videoDuration NOTIFY videoDetailChanged)
  Q_PROPERTY(QString videoBvid READ videoBvid NOTIFY videoDetailChanged)
  Q_PROPERTY(qint64 videoCid READ videoCid NOTIFY videoDetailChanged)
  Q_PROPERTY(qint64 videoAid READ videoAid NOTIFY videoDetailChanged)
  Q_PROPERTY(QString videoPubDate READ videoPubDate NOTIFY videoDetailChanged)

  // 播放地址
  Q_PROPERTY(QString playUrl READ playUrl NOTIFY playUrlChanged)
  Q_PROPERTY(int playQuality READ playQuality NOTIFY playUrlChanged)
  Q_PROPERTY(QVariantList acceptQualities READ acceptQualities NOTIFY acceptQualitiesChanged)

  // 下载状态
  Q_PROPERTY(bool isDownloading READ isDownloading NOTIFY downloadStateChanged)
  Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY downloadStateChanged)
  Q_PROPERTY(QString downloadStatus READ downloadStatus NOTIFY downloadStateChanged)
  Q_PROPERTY(QString tempVideoPath READ tempVideoPath NOTIFY downloadStateChanged)
  Q_PROPERTY(QString tempAudioPath READ tempAudioPath NOTIFY downloadStateChanged)
  Q_PROPERTY(QString dashVideoUrl READ dashVideoUrl NOTIFY playUrlChanged)
  Q_PROPERTY(QString dashAudioUrl READ dashAudioUrl NOTIFY playUrlChanged)

  // 登录状态
  Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY loginStateChanged)
  // 收藏状态
  Q_PROPERTY(bool isFavorited READ isFavorited NOTIFY favoriteStatusChanged)
  Q_PROPERTY(QString userName READ userName NOTIFY loginStateChanged)
  Q_PROPERTY(QString userFace READ userFace NOTIFY loginStateChanged)
  Q_PROPERTY(QString qrcodeUrl READ qrcodeUrl NOTIFY qrcodeChanged)
  Q_PROPERTY(qint64 userId READ userId NOTIFY loginStateChanged)
  Q_PROPERTY(int userLevel READ userLevel NOTIFY loginStateChanged)
  Q_PROPERTY(double userCoins READ userCoins NOTIFY loginStateChanged)
  Q_PROPERTY(int userFans READ userFans NOTIFY loginStateChanged)
  Q_PROPERTY(int userFollowing READ userFollowing NOTIFY loginStateChanged)
  Q_PROPERTY(QString userSign READ userSign NOTIFY loginStateChanged)
  Q_PROPERTY(QString userVipLabel READ userVipLabel NOTIFY loginStateChanged)
  Q_PROPERTY(bool userIsVip READ userIsVip NOTIFY loginStateChanged)

  // 全局错误
  Q_PROPERTY(QString globalError READ globalError NOTIFY globalErrorChanged)
  Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)

public:
  explicit BiliController(QObject *parent = nullptr);
  ~BiliController();

  // Property getters
  QString currentPage() const;
  void setCurrentPage(const QString &page);

  QString videoTitle() const;
  QString videoDesc() const;
  QString videoPic() const;
  QString videoOwner() const;
  QString videoOwnerFace() const;
  QString videoViews() const;
  QString videoLikes() const;
  QString videoCoins() const;
  QString videoFavorites() const;
  QString videoDanmaku() const;
  QString videoDuration() const;
  QString videoBvid() const;
  qint64 videoCid() const;
  qint64 videoAid() const;
  QString videoPubDate() const;

  QString playUrl() const;
  int playQuality() const;
  QVariantList acceptQualities() const;

  bool isDownloading() const { return m_isDownloading; }
  double downloadProgress() const { return m_downloadProgress; }
  QString downloadStatus() const { return m_downloadStatus; }
  QString tempVideoPath() const { return m_tempVideoPath; }
  QString tempAudioPath() const { return m_tempAudioPath; }
  QString dashVideoUrl() const { return m_dashVideoUrl; }
  QString dashAudioUrl() const { return m_dashAudioUrl; }

  bool loggedIn() const;
  bool isFavorited() const { return m_isFavorited; }
  QString userName() const;
  QString userFace() const;
  QString qrcodeUrl() const;
  qint64 userId() const;
  int userLevel() const;
  double userCoins() const;
  int userFans() const;
  int userFollowing() const;
  QString userSign() const;
  QString userVipLabel() const;
  bool userIsVip() const;

  QString globalError() const;
  bool isLoading() const;

  // ====== Q_INVOKABLE API 方法 ======

  Q_INVOKABLE void fetchPopular(int page = 1, int pageSize = 10);
  Q_INVOKABLE void fetchMorePopular();
  Q_INVOKABLE void fetchRanking(int rid = 0);
  Q_INVOKABLE void fetchHotSearch();
  Q_INVOKABLE void search(const QString &keyword, int page = 1);
  Q_INVOKABLE void searchMore();
  Q_INVOKABLE void fetchVideoDetail(const QString &bvid);
  Q_INVOKABLE void fetchPlayUrl(int quality = 64);
  // 仅获取可用清晰度列表（不触发播放）
  Q_INVOKABLE void fetchAcceptQualities(int quality = 64);
  Q_INVOKABLE void fetchComments(int page = 1);
  // 收藏夹
  Q_INVOKABLE void fetchFavoriteFolders();
  Q_INVOKABLE void fetchFavoriteItems(qint64 mediaId, int page = 1, int pageSize = 20);
  Q_INVOKABLE void fetchMoreFavoriteItems();
  // 收藏状态
  Q_INVOKABLE void fetchFavoriteStatus();
  Q_INVOKABLE void toggleFavorite();
  // 外部播放器
  Q_INVOKABLE void launchExternalPlayer(const QString &path);
  Q_INVOKABLE void launchExternalPlayerWithAudio(const QString &videoPath, const QString &audioPath);
  Q_INVOKABLE void launchExternalPlayerWithAudioUrl(const QString &videoUrl, const QString &audioUrl);
  Q_INVOKABLE void fetchMoreComments();
  Q_INVOKABLE void generateQrcode();
  Q_INVOKABLE void pollQrcode();
  Q_INVOKABLE void checkLoginStatus();
  Q_INVOKABLE void logout();
  Q_INVOKABLE void navigateTo(const QString &page);
  Q_INVOKABLE void goBack();
  Q_INVOKABLE void clearError();
  Q_INVOKABLE void clearSearchHistory();

  // 取消所有网络请求
  Q_INVOKABLE void cancelAll();

  // 下载并播放
  Q_INVOKABLE void downloadAndPlay(int quality = 64);
  Q_INVOKABLE void cancelDownload();
  Q_INVOKABLE void cleanupTempVideo();
  Q_INVOKABLE void downloadVideoToDisk(int quality);
  Q_INVOKABLE void playVideoPart(int index);

  // 获取模型（供 QML 使用）
  Q_INVOKABLE QObject *popularModel();
  Q_INVOKABLE QObject *rankingModel();
  Q_INVOKABLE QObject *searchModel();
  Q_INVOKABLE QObject *commentModel();
  Q_INVOKABLE QObject *hotSearchModel();
  Q_INVOKABLE QObject *videoPartModel();
  Q_INVOKABLE QObject *searchHistoryModel();
  Q_INVOKABLE QObject *favoriteFolderModel();
  Q_INVOKABLE QObject *favoriteItemModel();

signals:
  void currentPageChanged();
  void videoDetailChanged();
  void playUrlChanged();
  void acceptQualitiesChanged();
  void loginStateChanged();
  void qrcodeChanged();
  void globalErrorChanged();
  void isLoadingChanged();

  void toastMessage(const QString &message);
  void qrcodeLoginSuccess();
  void qrcodeNeedRefresh();
  void playbackReady(const QString &url);
  void downloadStateChanged();
  void favoriteStatusChanged();

private:
  void fetchUserInfo(qint64 mid);
  void setGlobalError(const QString &error);
  void setIsLoading(bool loading);
  void loadLoginStatus();
  void saveLoginStatus();
  void loadSearchHistory();
  void saveSearchHistory();

  // 安全辅助：检查 this 是否仍然有效的回调包装
  template <typename Func> auto safeCallback(Func &&func);

  BiliNetwork *m_network;

  QString m_currentPage;
  QStringList m_pageStack;
  static constexpr int MAX_PAGE_STACK_SIZE = 50;

  VideoItem m_currentVideo;

  QString m_playUrl;
  int m_playQuality;
  QVector<int> m_acceptQualities;

  // 下载状态
  bool m_isDownloading;
  double m_downloadProgress;
  QString m_downloadStatus;
  QString m_tempVideoPath;
  QString m_tempAudioPath;
  QString m_dashVideoUrl;
  QString m_dashAudioUrl;
  QPointer<QNetworkReply> m_downloadReply;

  bool m_loggedIn;
  bool m_isFavorited;
  QString m_userName;
  QString m_userFace;
  QString m_qrcodeUrl;
  QString m_qrcodeKey;

  // 用户详细信息
  qint64 m_userId;
  int m_userLevel;
  double m_userCoins;
  int m_userFans;
  int m_userFollowing;
  QString m_userSign;
  QString m_userVipLabel;
  bool m_userIsVip;

  int m_popularPage;
  int m_searchPage;
  int m_commentPage;
  QString m_searchKeyword;

  QString m_globalError;
  bool m_isLoading;

  // 加载计数器（多个异步请求同时进行时正确管理 isLoading）
  int m_loadingCount;

  VideoListModel *m_popularModel;
  VideoListModel *m_rankingModel;
  SearchResultModel *m_searchModel;
  CommentListModel *m_commentModel;
  HotSearchModel *m_hotSearchModel;
  VideoPartListModel *m_videoPartModel;
  QStringListModel *m_searchHistoryModel;
  QStringList m_searchHistory;

  FavoriteFolderModel *m_favoriteFolderModel;
  VideoListModel *m_favoriteItemModel;
  int m_favoritePage;
  qint64 m_currentFavoriteId;

  // 标记对象是否正在销毁
  bool m_destroying;
};
