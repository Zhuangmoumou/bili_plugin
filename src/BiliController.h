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
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QTimer>
#include <functional>

#include "BiliModels.h"

class VideoPartListModel;
class VideoListModel;
class CommentListModel;
class CommentReplyListModel;
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
  Q_PROPERTY(qint64 videoOwnerMid READ videoOwnerMid NOTIFY videoDetailChanged)
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
  Q_PROPERTY(QVariantList subtitleList READ subtitleList NOTIFY subtitleListChanged)
  Q_PROPERTY(qint64 selectedSubtitleId READ selectedSubtitleId NOTIFY selectedSubtitleChanged)
  Q_PROPERTY(QString selectedSubtitleLabel READ selectedSubtitleLabel NOTIFY selectedSubtitleChanged)
  Q_PROPERTY(int subtitleFontSize READ subtitleFontSize NOTIFY subtitleStyleChanged)
  Q_PROPERTY(int subtitleMarginV READ subtitleMarginV NOTIFY subtitleStyleChanged)
  Q_PROPERTY(double subtitleSpacing READ subtitleSpacing NOTIFY subtitleStyleChanged)
  Q_PROPERTY(int subtitleWeight READ subtitleWeight NOTIFY subtitleStyleChanged)

  // 登录状态
  Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY loginStateChanged)

  // UP 主主页
  Q_PROPERTY(qint64 upUserMid READ upUserMid NOTIFY upUserChanged)
  Q_PROPERTY(QString upUserName READ upUserName NOTIFY upUserChanged)
  Q_PROPERTY(QString upUserFace READ upUserFace NOTIFY upUserChanged)
  Q_PROPERTY(int upUserLevel READ upUserLevel NOTIFY upUserChanged)
  Q_PROPERTY(int upUserFans READ upUserFans NOTIFY upUserChanged)
  Q_PROPERTY(int upUserFollowing READ upUserFollowing NOTIFY upUserChanged)
  Q_PROPERTY(QString upUserSign READ upUserSign NOTIFY upUserChanged)
  Q_PROPERTY(bool upIsFollowing READ upIsFollowing NOTIFY upFollowChanged)
  // 收藏/投币/点赞状态
  Q_PROPERTY(bool isFavorited READ isFavorited NOTIFY favoriteStatusChanged)
  Q_PROPERTY(bool isCoined READ isCoined NOTIFY coinStatusChanged)
  Q_PROPERTY(bool isLiked READ isLiked NOTIFY likeStatusChanged)
  Q_PROPERTY(bool isWatchLater READ isWatchLater NOTIFY watchLaterStatusChanged)
  Q_PROPERTY(QString userName READ userName NOTIFY loginStateChanged)
  Q_PROPERTY(QString userFace READ userFace NOTIFY loginStateChanged)
  Q_PROPERTY(QString qrcodeUrl READ qrcodeUrl NOTIFY qrcodeChanged)
  Q_PROPERTY(qint64 userId READ userId NOTIFY loginStateChanged)
  Q_PROPERTY(int userLevel READ userLevel NOTIFY loginStateChanged)
  Q_PROPERTY(int userExp READ userExp NOTIFY loginStateChanged)
  Q_PROPERTY(int userExpMin READ userExpMin NOTIFY loginStateChanged)
  Q_PROPERTY(int userExpNext READ userExpNext NOTIFY loginStateChanged)
  Q_PROPERTY(double userExpProgress READ userExpProgress NOTIFY loginStateChanged)
  Q_PROPERTY(double userCoins READ userCoins NOTIFY loginStateChanged)
  Q_PROPERTY(int userFans READ userFans NOTIFY loginStateChanged)
  Q_PROPERTY(int userFollowing READ userFollowing NOTIFY loginStateChanged)
  Q_PROPERTY(QString userSign READ userSign NOTIFY loginStateChanged)
  Q_PROPERTY(QString userVipLabel READ userVipLabel NOTIFY loginStateChanged)
  Q_PROPERTY(bool userIsVip READ userIsVip NOTIFY loginStateChanged)

  // 全局错误
  Q_PROPERTY(QString globalError READ globalError NOTIFY globalErrorChanged)
  Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)
  Q_PROPERTY(bool replyHasMore READ replyHasMore NOTIFY replyHasMoreChanged)

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
  qint64 videoOwnerMid() const;
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
  QVariantList subtitleList() const;
  qint64 selectedSubtitleId() const { return m_selectedSubtitleId; }
  QString selectedSubtitleLabel() const { return m_selectedSubtitleLabel; }
  int subtitleFontSize() const { return m_subtitleFontSize; }
  int subtitleMarginV() const { return m_subtitleMarginV; }
  double subtitleSpacing() const { return m_subtitleSpacing; }
  int subtitleWeight() const { return m_subtitleWeight; }

  bool loggedIn() const;
  qint64 upUserMid() const { return m_upUserMid; }
  QString upUserName() const { return m_upUserName; }
  QString upUserFace() const { return m_upUserFace; }
  int upUserLevel() const { return m_upUserLevel; }
  int upUserFans() const { return m_upUserFans; }
  int upUserFollowing() const { return m_upUserFollowing; }
  QString upUserSign() const { return m_upUserSign; }
  bool upIsFollowing() const { return m_upIsFollowing; }
  bool isFavorited() const { return m_isFavorited; }
  bool isCoined() const { return m_isCoined; }
  bool isLiked() const { return m_isLiked; }
  bool isWatchLater() const { return m_isWatchLater; }
  QString userName() const;
  QString userFace() const;
  QString qrcodeUrl() const;
  qint64 userId() const;
  int userLevel() const;
  int userExp() const;
  int userExpMin() const;
  int userExpNext() const;
  double userExpProgress() const;
  double userCoins() const;
  int userFans() const;
  int userFollowing() const;
  QString userSign() const;
  QString userVipLabel() const;
  bool userIsVip() const;

  QString globalError() const;
  bool isLoading() const;
  bool replyHasMore() const { return m_commentReplyHasMore; }

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
  Q_INVOKABLE void fetchCommentReplies(qint64 rootRpid);
  Q_INVOKABLE void fetchMoreCommentReplies();
  // 收藏夹
  Q_INVOKABLE void fetchFavoriteFolders();
  Q_INVOKABLE void fetchFavoriteItems(qint64 mediaId, int page = 1, int pageSize = 20);
  Q_INVOKABLE void fetchMoreFavoriteItems();
  // 收藏/投币/点赞状态
  Q_INVOKABLE void fetchFavoriteStatus();
  Q_INVOKABLE void fetchCoinStatus();
  Q_INVOKABLE void fetchLikeStatus();
  Q_INVOKABLE void fetchWatchLaterStatus();
  Q_INVOKABLE void addCoin(int multiply = 1, bool selectLike = false);
  Q_INVOKABLE void toggleLike();
  Q_INVOKABLE void toggleFavorite();
  Q_INVOKABLE void toggleFavoriteTo(qint64 mediaId);
  Q_INVOKABLE void toggleWatchLater();
  // 外部播放器
  Q_INVOKABLE void launchExternalPlayer(const QString &path);
  Q_INVOKABLE void launchExternalPlayerWithAudio(const QString &videoPath, const QString &audioPath);
  Q_INVOKABLE void launchExternalPlayerWithAudioUrl(const QString &videoUrl, const QString &audioUrl);
  Q_INVOKABLE void launchExternalPlayerWithAudioUrlAndSubtitle(const QString &videoUrl, const QString &audioUrl, const QString &subtitlePath);
  Q_INVOKABLE void fetchSubtitleList();
  Q_INVOKABLE void selectSubtitle(qint64 subtitleId, const QString &label);
  Q_INVOKABLE void clearSelectedSubtitle();
  Q_INVOKABLE void launchExternalPlayerCurrentSelection();
  Q_INVOKABLE void setSubtitleFontSize(int value);
  Q_INVOKABLE void setSubtitleMarginV(int value);
  Q_INVOKABLE void setSubtitleSpacing(double value);
  Q_INVOKABLE void setSubtitleWeight(int value);
  Q_INVOKABLE void fetchMoreComments();
  Q_INVOKABLE void generateQrcode();
  Q_INVOKABLE void pollQrcode();
  // 短信登录：启动本地 bili-login（8666端口）并轮询 /pull
  Q_INVOKABLE void startSmsLogin();
  Q_INVOKABLE void stopSmsLogin();
  Q_INVOKABLE void pollSmsLogin();
  Q_INVOKABLE bool smsLoginRunning() const { return m_smsPolling; }
  Q_INVOKABLE QString smsLoginLastError() const { return m_smsLastError; }
  Q_INVOKABLE void checkLoginStatus();
  Q_INVOKABLE void logout();
  Q_INVOKABLE void navigateTo(const QString &page);
  Q_INVOKABLE void goBack();
  Q_INVOKABLE void clearError();
  Q_INVOKABLE void clearSearchHistory();
  Q_INVOKABLE void removeSearchHistory(const QString &keyword);
  // 评论/动态远程图片转本地临时文件，供系统 FileManagerImageViewer 打开
  Q_INVOKABLE void prepareImageForViewer(const QString &url);

  // 取消所有网络请求
  Q_INVOKABLE void cancelAll();

  // 下载并播放
  Q_INVOKABLE void downloadAndPlay(int quality = 64);
  Q_INVOKABLE void cancelDownload();
  Q_INVOKABLE void cleanupTempVideo();
  Q_INVOKABLE void downloadVideoToDisk(int quality);
  Q_INVOKABLE void playVideoPart(int index);
  Q_INVOKABLE void restartGoServer();

  // 获取模型（供 QML 使用）
  Q_INVOKABLE QObject *popularModel();
  Q_INVOKABLE QObject *rankingModel();
  Q_INVOKABLE QObject *searchModel();
  Q_INVOKABLE QObject *commentModel();
  Q_INVOKABLE QObject *commentReplyModel();
  Q_INVOKABLE QObject *hotSearchModel();
  Q_INVOKABLE QObject *videoPartModel();
  Q_INVOKABLE QObject *searchHistoryModel();
  Q_INVOKABLE QObject *favoriteFolderModel();
  Q_INVOKABLE QObject *favoriteItemModel();
  Q_INVOKABLE QObject *recentHistoryModel();
  Q_INVOKABLE QObject *watchLaterModel();
  Q_INVOKABLE QObject *upVideoModel();
  Q_INVOKABLE void fetchRecentHistory();
  Q_INVOKABLE void fetchMoreRecentHistory();
  Q_INVOKABLE void fetchWatchLater(int page = 1, int pageSize = 20);
  Q_INVOKABLE void fetchMoreWatchLater();
  Q_INVOKABLE void addToWatchLater();
  Q_INVOKABLE void refreshUserInfo();

  // UP 主主页
  Q_INVOKABLE void fetchUpInfo(qint64 mid);
  Q_INVOKABLE void fetchUpVideos(qint64 mid, int page = 1, int pageSize = 20);
  Q_INVOKABLE void fetchMoreUpVideos();
  Q_INVOKABLE void toggleUpFollow();

signals:
  void currentPageChanged();
  void videoDetailChanged();
  void playUrlChanged();
  void acceptQualitiesChanged();
  void subtitleListChanged();
  void selectedSubtitleChanged();
  void subtitleStyleChanged();
  void loginStateChanged();
  void qrcodeChanged();
  void globalErrorChanged();
  void isLoadingChanged();
  void replyHasMoreChanged();
  void upUserChanged();
  void upFollowChanged();

  void toastMessage(const QString &message);
  void qrcodeLoginSuccess();
  void qrcodeNeedRefresh();
  void playbackReady(const QString &url);
  void downloadStateChanged();
  void favoriteStatusChanged();
  void coinStatusChanged();
  void likeStatusChanged();
  void watchLaterStatusChanged();
  void commentImageReadyForViewer(const QString &localPath);

private:
  void fetchUserInfo(qint64 mid);
  void clearLocalLoginState();
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
  QJsonArray m_subtitleItems;
  qint64 m_selectedSubtitleId = 0;
  QString m_selectedSubtitleLabel;
  int m_subtitleFontSize = 10;
  int m_subtitleMarginV = 10;
  double m_subtitleSpacing = 2.0;
  int m_subtitleWeight = 700;
  QPointer<QNetworkReply> m_downloadReply;

  bool m_loggedIn;
  bool m_isFavorited;
  bool m_isCoined;
  bool m_isLiked;
  bool m_isWatchLater;
  QString m_userName;
  QString m_userFace;
  QString m_qrcodeUrl;
  QString m_qrcodeKey;
  // ====== 短信登录（bili-login 服务） ======
  // bili-login 二进制进程（可为空：startDetached 场景）
  QPointer<QProcess> m_smsLoginProcess;
  QPointer<QTimer> m_smsPollTimer;
  QNetworkAccessManager *m_smsNam = nullptr;
  bool m_smsPolling = false;
  bool m_smsImporting = false;
  QString m_smsLastError;

  // 用户详细信息
  qint64 m_userId;
  int m_userLevel;
  int m_userExp;
  int m_userExpMin;
  int m_userExpNext;
  double m_userCoins;
  int m_userFans;
  int m_userFollowing;
  QString m_userSign;
  QString m_userVipLabel;
  bool m_userIsVip;

  int m_popularPage;
  int m_searchPage;
  int m_commentPage;
  int m_commentReplyPage = 1;
  bool m_commentReplyHasMore = false;
  qint64 m_currentCommentRootRpid = 0;
  QString m_searchKeyword;

  QString m_globalError;
  bool m_isLoading;

  // 加载计数器（多个异步请求同时进行时正确管理 isLoading）
  int m_loadingCount;

  VideoListModel *m_popularModel;
  VideoListModel *m_rankingModel;
  SearchResultModel *m_searchModel;
  CommentListModel *m_commentModel;
  CommentReplyListModel *m_commentReplyModel;
  HotSearchModel *m_hotSearchModel;
  VideoPartListModel *m_videoPartModel;
  QStringListModel *m_searchHistoryModel;
  QStringList m_searchHistory;

  FavoriteFolderModel *m_favoriteFolderModel;
  VideoListModel *m_favoriteItemModel;
  VideoListModel *m_recentHistoryModel;
  VideoListModel *m_watchLaterModel;
  VideoListModel *m_upVideoModel;
  int m_favoritePage;
  qint64 m_currentFavoriteId;
  int m_recentHistoryMax = 0;
  int m_recentHistoryViewAt = 0;
  int m_watchLaterPage = 1;
  bool m_watchLaterHasMore = true;

  // UP 主主页数据
  qint64 m_upUserMid = 0;
  QString m_upUserName;
  QString m_upUserFace;
  int m_upUserLevel = 0;
  int m_upUserFans = 0;
  int m_upUserFollowing = 0;
  QString m_upUserSign;
  bool m_upIsFollowing = false;
  bool m_upFollowLoading = false;
  int m_upVideoPage = 1;
  bool m_upVideoHasMore = true;
  // APP 游标翻页：记录下一页游标（max/next）。用于修复“加载更多只拿到第一页”和新稿件插入导致的丢失。
  qint64 m_upVideoCursorNext = 0;
  QString m_lastRecentViewReportKey;

  struct DashResult {
    QString videoUrl;
    QString audioUrl;
    int finalQuality = 0;
  };

  void updateAcceptQualities(const QJsonObject &data);
  DashResult pickDashUrls(const QJsonObject &data, int requestedQuality) const;
  QString pickMp4Url(const QJsonObject &data) const;

  void apiGet(const QString &path, const QMap<QString, QString> &params,
              std::function<void(const QJsonObject &)> onSuccess,
              std::function<void(int, const QString &)> onError = nullptr,
              bool withLoading = false);
  void reportCurrentVideoAsRecentViewIfNeeded();

  void startDownloadTask(const QString &videoUrl, const QString &audioUrl,
                         const QString &videoPath, const QString &audioPath,
                         int finalQuality, bool playAfter,
                         const QString &successToastPrefix = QString(),
                         const QString &errorToastPrefix = QStringLiteral("下载失败："));

  // 标记对象是否正在销毁
  bool m_destroying;
};
