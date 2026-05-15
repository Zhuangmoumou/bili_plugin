#include "BiliController.h"
#include "BiliModels.h"
#include "BiliNetwork.h"
#include "modules/comment/BiliCommentModule.h"
#include "modules/favorite/BiliFavoriteModule.h"
#include "modules/feed/BiliFeedModule.h"
#include "modules/history/BiliHistoryModule.h"
#include "modules/login/BiliLoginModule.h"
#include "modules/playback/BiliPlaybackModule.h"
#include "modules/search/BiliSearchModule.h"
#include "modules/season/BiliSeasonModule.h"
#include "modules/up/BiliUpModule.h"
#include "modules/video/BiliVideoModule.h"
#include "modules/viewer/BiliViewerModule.h"

#include <QDateTime>
#include <QSettings>
#include <QStringListModel>
#include <QTimer>
#include <QDebug>
#include <QNetworkReply>

// 安全回调包装宏 - 在回调执行前检查对象是否仍存在
#define SAFE_CALLBACK(controller, ...)                                         \
  [ guard = QPointer<BiliController>(controller), __VA_ARGS__ ]

BiliController::BiliController(QObject *parent)
    : QObject(parent), m_network(BiliNetwork::instance()),
      m_commentModule(std::make_unique<BiliCommentModule>(this)),
      m_favoriteModule(std::make_unique<BiliFavoriteModule>(this)),
      m_feedModule(std::make_unique<BiliFeedModule>(this)),
      m_playbackModule(std::make_unique<BiliPlaybackModule>(this)),
      m_searchModule(std::make_unique<BiliSearchModule>(this)),
      m_upModule(std::make_unique<BiliUpModule>(this)),
      m_videoModule(std::make_unique<BiliVideoModule>(this)),
      m_viewerModule(std::make_unique<BiliViewerModule>(this)),
      m_loginModule(std::make_unique<BiliLoginModule>(this)),
      m_historyModule(std::make_unique<BiliHistoryModule>(this)),
      m_seasonModule(std::make_unique<BiliSeasonModule>(this)),
      m_currentPage("home"), m_playQuality(64), m_isDownloading(false),
      m_downloadProgress(0), m_loggedIn(false), m_isFavorited(false), m_isCoined(false), m_isLiked(false), m_isWatchLater(false),
      m_userName(""), m_userFace(""), m_qrcodeUrl(""), m_qrcodeKey(""),
      m_userId(0), m_userLevel(0), m_userExp(0), m_userExpMin(0), m_userExpNext(0), m_userCoins(0), m_userFans(0),
      m_userFollowing(0), m_userSign(""), m_userVipLabel(""),
      m_userIsVip(false), m_popularPage(1), m_searchPage(1),
      m_commentPage(1), m_searchKeyword(""), m_globalError(""),
      m_isLoading(false), m_loadingCount(0),
      m_popularModel(new VideoListModel(this)),
      m_rankingModel(new VideoListModel(this)),
      m_searchModel(new SearchResultModel(this)),
      m_commentModel(new CommentListModel(this)),
      m_commentReplyModel(new CommentReplyListModel(this)),
      m_hotSearchModel(new HotSearchModel(this)),
      m_videoPartModel(new VideoPartListModel(this)),
      m_searchHistoryModel(new QStringListModel(this)),
      m_searchHistory(),
      m_favoriteFolderModel(new FavoriteFolderModel(this)),
      m_favoriteItemModel(new VideoListModel(this)),
      m_recentHistoryModel(new VideoListModel(this)),
      m_watchLaterModel(new VideoListModel(this)),
      m_upVideoModel(new VideoListModel(this)),
      m_seasonVideoModel(new VideoListModel(this)),
      m_relatedVideoModel(new VideoListModel(this)),
      m_upSeasonModel(new UpSeasonListModel(this)),
      m_favoritePage(1),
      m_currentFavoriteId(0),
      m_destroying(false) {
  connect(m_network, &BiliNetwork::networkError, this,
          [this](const QString &msg) {
            if (!m_destroying)
              setGlobalError(msg);
          });

  // 默认可用清晰度
  m_acceptQualities = {16, 32, 64};

  loadLoginStatus();
  loadSearchHistory();
  // 加载字幕样式设置
  QSettings settings("BiliPocket", "BiliPlugin");
  m_subtitleFontSize = settings.value("subtitleFontSize", m_subtitleFontSize).toInt();
  m_subtitleMarginV = settings.value("subtitleMarginV", m_subtitleMarginV).toInt();
  m_subtitleSpacing = settings.value("subtitleSpacing", m_subtitleSpacing).toDouble();
  m_subtitleWeight = settings.value("subtitleWeight",
                                    settings.value("subtitleBold", m_subtitleWeight)).toInt();
  m_subtitleColorPreset = settings.value("subtitleColorPreset", m_subtitleColorPreset).toString();
  m_subtitleOutlineEnabled = settings.value("subtitleOutlineEnabled", m_subtitleOutlineEnabled).toBool();
  m_subtitleOutlineWidth = settings.value("subtitleOutlineWidth", m_subtitleOutlineWidth).toInt();
}

BiliController::~BiliController() {
  m_destroying = true;

  // 停止短信登录轮询与进程
  stopSmsLogin();

  // 取消所有正在进行的请求，防止回调访问已销毁的对象
  if (m_network) {
    m_network->cancelAllRequests();
  }
}

// ====== 加载状态管理（引用计数） ======

void BiliController::setIsLoading(bool loading) {
  if (loading) {
    m_loadingCount++;
  } else {
    m_loadingCount = qMax(0, m_loadingCount - 1);
  }

  bool newLoading = m_loadingCount > 0;
  if (m_isLoading != newLoading) {
    m_isLoading = newLoading;
    emit isLoadingChanged();
  }
}

// ====== Properties ======

QString BiliController::currentPage() const { return m_currentPage; }

void BiliController::setCurrentPage(const QString &page) {
  if (m_currentPage != page) {
    m_currentPage = page;
    emit currentPageChanged();
  }
}

QString BiliController::videoTitle() const { return m_currentVideo.title; }
QString BiliController::videoDesc() const { return m_currentVideo.desc; }
QString BiliController::videoPic() const { return m_currentVideo.pic; }
QString BiliController::videoOwner() const { return m_currentVideo.ownerName; }
QString BiliController::videoOwnerFace() const {
  return m_currentVideo.ownerFace;
}
qint64 BiliController::videoOwnerMid() const {
  return m_currentVideo.ownerMid;
}
QString BiliController::videoViews() const {
  return VideoListModel::formatCount(m_currentVideo.views);
}
QString BiliController::videoLikes() const {
  return VideoListModel::formatCount(m_currentVideo.likes);
}
QString BiliController::videoCoins() const {
  return VideoListModel::formatCount(m_currentVideo.coins);
}
QString BiliController::videoFavorites() const {
  return VideoListModel::formatCount(m_currentVideo.favorites);
}
QString BiliController::videoDanmaku() const {
  return VideoListModel::formatCount(m_currentVideo.danmaku);
}
QString BiliController::videoDuration() const {
  return VideoListModel::formatDuration(m_currentVideo.duration);
}
QString BiliController::videoBvid() const { return m_currentVideo.bvid; }
qint64 BiliController::videoCid() const { return m_currentVideo.cid; }
qint64 BiliController::videoAid() const { return m_currentVideo.aid; }
QString BiliController::videoPubDate() const {
  if (m_currentVideo.pubdate <= 0) return "未知日期";
  QDateTime dt = QDateTime::fromSecsSinceEpoch(m_currentVideo.pubdate);
  return dt.toString("yyyy-MM-dd HH:mm");
}

QString BiliController::playUrl() const { return m_playUrl; }
int BiliController::playQuality() const { return m_playQuality; }

QVariantList BiliController::subtitleList() const {
  QVariantList list;
  for (const QJsonValue &v : m_subtitleItems) {
    if (v.isObject()) {
      list << v.toObject().toVariantMap();
    }
  }
  return list;
}

QVariantList BiliController::acceptQualities() const {
  QVariantList list;
  for (int q : m_acceptQualities) {
    list << q;
  }
  return list;
}

bool BiliController::loggedIn() const { return m_loggedIn; }
QString BiliController::userName() const { return m_userName; }
QString BiliController::userFace() const { return m_userFace; }
QString BiliController::qrcodeUrl() const { return m_qrcodeUrl; }
qint64 BiliController::userId() const { return m_userId; }
int BiliController::userLevel() const { return m_userLevel; }
int BiliController::userExp() const { return m_userExp; }
int BiliController::userExpMin() const { return m_userExpMin; }
int BiliController::userExpNext() const { return m_userExpNext; }
double BiliController::userExpProgress() const {
  if (m_userExpNext > 0 && m_userExp >= 0) {
    double progress = m_userExp * 1.0 / m_userExpNext;
    return qBound(0.0, progress, 1.0);
  }
  return 0.0;
}
double BiliController::userCoins() const { return m_userCoins; }
int BiliController::userFans() const { return m_userFans; }
int BiliController::userFollowing() const { return m_userFollowing; }
QString BiliController::userSign() const { return m_userSign; }
QString BiliController::userVipLabel() const { return m_userVipLabel; }
bool BiliController::userIsVip() const { return m_userIsVip; }

QString BiliController::globalError() const { return m_globalError; }
bool BiliController::isLoading() const { return m_isLoading; }

void BiliController::setGlobalError(const QString &error) {
  if (m_globalError != error) {
    m_globalError = error;
    emit globalErrorChanged();
  }
}

// ====== Models ======

QObject *BiliController::popularModel() { return m_popularModel; }
QObject *BiliController::rankingModel() { return m_rankingModel; }
QObject *BiliController::searchModel() { return m_searchModel; }
QObject *BiliController::commentModel() { return m_commentModel; }
QObject *BiliController::commentReplyModel() { return m_commentReplyModel; }
QObject *BiliController::hotSearchModel() { return m_hotSearchModel; }

QObject *BiliController::videoPartModel() { return m_videoPartModel; }

QObject *BiliController::searchHistoryModel() { return m_searchHistoryModel; }
QObject *BiliController::favoriteFolderModel() { return m_favoriteFolderModel; }
QObject *BiliController::favoriteItemModel() { return m_favoriteItemModel; }
QObject *BiliController::recentHistoryModel() { return m_recentHistoryModel; }
QObject *BiliController::watchLaterModel() { return m_watchLaterModel; }
QObject *BiliController::upVideoModel() { return m_upVideoModel; }
QObject *BiliController::seasonVideoModel() { return m_seasonVideoModel; }
QObject *BiliController::relatedVideoModel() { return m_relatedVideoModel; }
QObject *BiliController::upSeasonModel() { return m_upSeasonModel; }

// ====== Navigation ======

void BiliController::navigateTo(const QString &page) {
  // 限制页面栈深度
  if (m_pageStack.size() >= MAX_PAGE_STACK_SIZE) {
    m_pageStack.removeFirst();
  }
  m_pageStack.append(m_currentPage);
  setCurrentPage(page);
}

void BiliController::goBack() {
  if (m_pageStack.isEmpty()) {
    return;
  }
  QString prev = m_pageStack.takeLast();
  setCurrentPage(prev);
}

void BiliController::clearError() { setGlobalError(""); }

void BiliController::cancelAll() {
  if (m_network) {
    m_network->cancelAllRequests();
  }

  // 立即重置前端可见加载状态，避免取消后卡在 loading UI
  m_loadingCount = 0;
  m_videoDetailLoadingBvid.clear();
  m_playUrlLoadingKey.clear();
  m_acceptQualitiesLoadingKey.clear();
  m_favoriteStatusLoadingAid = 0;
  m_coinStatusLoadingAid = 0;
  m_likeStatusLoadingAid = 0;
  m_watchLaterStatusLoadingAid = 0;
  if (m_isLoading) {
    m_isLoading = false;
    emit isLoadingChanged();
  }

  // 重置各模型加载状态，避免它们因 abort 后未恢复而阻塞后续操作
  if (m_popularModel) m_popularModel->setLoading(false);
  if (m_rankingModel) m_rankingModel->setLoading(false);
  if (m_searchModel) m_searchModel->setLoading(false);
  if (m_commentModel) m_commentModel->setLoading(false);
  if (m_commentReplyModel) m_commentReplyModel->setLoading(false);
  if (m_hotSearchModel) m_hotSearchModel->setLoading(false);
  if (m_favoriteFolderModel) m_favoriteFolderModel->setLoading(false);
  if (m_favoriteItemModel) m_favoriteItemModel->setLoading(false);
  if (m_recentHistoryModel) m_recentHistoryModel->setLoading(false);
  if (m_watchLaterModel) m_watchLaterModel->setLoading(false);
  if (m_upVideoModel) m_upVideoModel->setLoading(false);
  if (m_seasonVideoModel) m_seasonVideoModel->setLoading(false);
  if (m_upSeasonModel) m_upSeasonModel->setLoading(false);

  // 下载中也允许中止显示状态
  if (m_isDownloading) {
    m_isDownloading = false;
    m_downloadProgress = 0;
    m_downloadStatus.clear();
    emit downloadStateChanged();
  }

  emit toastMessage("已取消当前请求");
}

void BiliController::clearSearchHistory() {
  m_searchModule->clearSearchHistory();
}

void BiliController::removeSearchHistory(const QString &keyword) {
  m_searchModule->removeSearchHistory(keyword);
}

void BiliController::refreshUserInfo() {
  if (!m_loggedIn || m_userId <= 0) {
    return;
  }
  m_loginModule->refreshLoginInfo();
  fetchUserInfo(m_userId);
}

// ====== 登录状态持久化 ======

void BiliController::loadLoginStatus() {
  // 登录状态由 Go 服务器统一维护，这里只做一次后台校验
  QTimer::singleShot(0, this, [this]() { checkLoginStatus(); });
}

void BiliController::saveLoginStatus() {
  // Cookie 由 Go 服务器统一处理，客户端不再持久化
}

void BiliController::loadSearchHistory() {
    QSettings settings("BiliPocket", "BiliPlugin");
    m_searchHistory = settings.value("searchHistory").toStringList();
    m_searchHistoryModel->setStringList(m_searchHistory);
}

void BiliController::saveSearchHistory() {
    QSettings settings("BiliPocket", "BiliPlugin");
    settings.setValue("searchHistory", m_searchHistory);
    settings.sync();
}

void BiliController::clearLocalLoginState() {
  m_loggedIn = false;
  m_userName = "";
  m_userFace = "";
  m_userId = 0;
  m_userLevel = 0;
  m_userExp = 0;
  m_userExpMin = 0;
  m_userExpNext = 0;
  m_userCoins = 0;
  m_userFans = 0;
  m_userFollowing = 0;
  m_userSign = "";
  m_userVipLabel = "";
  m_userIsVip = false;
  m_qrcodeUrl = "";
  m_qrcodeKey = "";
  emit qrcodeChanged();
  if (m_isFavorited) {
    m_isFavorited = false;
    emit favoriteStatusChanged();
  }
  if (m_isCoined) {
    m_isCoined = false;
    emit coinStatusChanged();
  }
  if (m_isLiked) {
    m_isLiked = false;
    emit likeStatusChanged();
  }
  if (m_isWatchLater) {
    m_isWatchLater = false;
    emit watchLaterStatusChanged();
  }
  emit loginStateChanged();
}

// ====== QML API delegates ======

void BiliController::fetchPopular(int page, int pageSize) { m_feedModule->fetchPopular(page, pageSize); }
void BiliController::fetchMorePopular() { m_feedModule->fetchMorePopular(); }
void BiliController::fetchRanking(int rid) { m_feedModule->fetchRanking(rid); }
void BiliController::fetchHotSearch() { m_feedModule->fetchHotSearch(); }

void BiliController::search(const QString &keyword, int page) { m_searchModule->search(keyword, page); }
void BiliController::searchMore() { m_searchModule->searchMore(); }

void BiliController::fetchVideoDetail(const QString &bvid) { m_videoModule->fetchVideoDetail(bvid); }
void BiliController::reportCurrentVideoAsRecentViewIfNeeded() { m_videoModule->reportCurrentVideoAsRecentViewIfNeeded(); }

void BiliController::fetchPlayUrl(int quality) { m_playbackModule->fetchPlayUrl(quality); }
void BiliController::fetchAcceptQualities(int quality) { m_playbackModule->fetchAcceptQualities(quality); }
void BiliController::downloadAndPlay(int quality) { m_playbackModule->downloadAndPlay(quality); }
void BiliController::cancelDownload() { m_playbackModule->cancelDownload(); }
void BiliController::cleanupTempVideo() { m_playbackModule->cleanupTempVideo(); }

void BiliController::fetchComments(int page) { m_commentModule->fetchComments(page); }
void BiliController::fetchCommentReplies(qint64 rootRpid) { m_commentModule->fetchCommentReplies(rootRpid); }
void BiliController::fetchMoreCommentReplies() { m_commentModule->fetchMoreCommentReplies(); }
void BiliController::fetchMoreComments() { m_commentModule->fetchMoreComments(); }

void BiliController::fetchFavoriteFolders() { m_favoriteModule->fetchFavoriteFolders(); }
void BiliController::fetchFavoriteItems(qint64 mediaId, int page, int pageSize) { m_favoriteModule->fetchFavoriteItems(mediaId, page, pageSize); }
void BiliController::fetchMoreFavoriteItems() { m_favoriteModule->fetchMoreFavoriteItems(); }
void BiliController::fetchFavoriteStatus() { m_favoriteModule->fetchFavoriteStatus(); }
void BiliController::fetchCoinStatus() { m_favoriteModule->fetchCoinStatus(); }
void BiliController::addCoin(int multiply, bool selectLike) { m_favoriteModule->addCoin(multiply, selectLike); }
void BiliController::fetchLikeStatus() { m_favoriteModule->fetchLikeStatus(); }
void BiliController::fetchWatchLaterStatus() { m_favoriteModule->fetchWatchLaterStatus(); }
void BiliController::toggleLike() { m_favoriteModule->toggleLike(); }
void BiliController::toggleFavorite() { m_favoriteModule->toggleFavorite(); }
void BiliController::toggleFavoriteTo(qint64 mediaId) { m_favoriteModule->toggleFavoriteTo(mediaId); }
void BiliController::toggleWatchLater() { m_favoriteModule->toggleWatchLater(); }
bool BiliController::externalPlayerRunning() const { return m_playbackModule->externalPlayerRunning(); }
void BiliController::launchExternalPlayer(const QString &path) { m_playbackModule->launchExternalPlayer(path); }
void BiliController::launchExternalPlayerWithAudio(const QString &videoPath, const QString &audioPath) { m_playbackModule->launchExternalPlayerWithAudio(videoPath, audioPath); }
void BiliController::launchExternalPlayerWithAudioUrl(const QString &videoUrl, const QString &audioUrl) { m_playbackModule->launchExternalPlayerWithAudioUrl(videoUrl, audioUrl); }
void BiliController::launchExternalPlayerWithAudioUrlAndSubtitle(const QString &videoUrl, const QString &audioUrl, const QString &subtitlePath) { m_playbackModule->launchExternalPlayerWithAudioUrlAndSubtitle(videoUrl, audioUrl, subtitlePath); }
void BiliController::fetchSubtitleList() { m_playbackModule->fetchSubtitleList(); }
void BiliController::selectSubtitle(qint64 subtitleId, const QString &label) { m_playbackModule->selectSubtitle(subtitleId, label); }
void BiliController::clearSelectedSubtitle() { m_playbackModule->clearSelectedSubtitle(); }
void BiliController::setSubtitleFontSize(int value) { m_playbackModule->setSubtitleFontSize(value); }
void BiliController::setSubtitleMarginV(int value) { m_playbackModule->setSubtitleMarginV(value); }
void BiliController::setSubtitleSpacing(double value) { m_playbackModule->setSubtitleSpacing(value); }
void BiliController::setSubtitleWeight(int value) { m_playbackModule->setSubtitleWeight(value); }
void BiliController::setSubtitleColorPreset(const QString &value) { m_playbackModule->setSubtitleColorPreset(value); }
void BiliController::setSubtitleOutlineEnabled(bool enabled) { m_playbackModule->setSubtitleOutlineEnabled(enabled); }
void BiliController::setSubtitleOutlineWidth(int value) { m_playbackModule->setSubtitleOutlineWidth(value); }
void BiliController::launchExternalPlayerCurrentSelection() { m_playbackModule->launchExternalPlayerCurrentSelection(); }

void BiliController::generateQrcode() { m_loginModule->generateQrcode(); }
void BiliController::pollQrcode() { m_loginModule->pollQrcode(); }
void BiliController::startSmsLogin() { m_loginModule->startSmsLogin(); }
void BiliController::stopSmsLogin() { m_loginModule->stopSmsLogin(); }
void BiliController::pollSmsLogin() { m_loginModule->pollSmsLogin(); }
void BiliController::checkLoginStatus() { m_loginModule->checkLoginStatus(); }
void BiliController::fetchUserInfo(qint64 mid) { m_loginModule->fetchUserInfo(mid); }
void BiliController::logout() { m_loginModule->logout(); }

void BiliController::fetchRecentHistory() { m_historyModule->fetchRecentHistory(); }
void BiliController::fetchMoreRecentHistory() { m_historyModule->fetchMoreRecentHistory(); }
void BiliController::fetchWatchLater(int page, int pageSize) { m_historyModule->fetchWatchLater(page, pageSize); }
void BiliController::fetchMoreWatchLater() { m_historyModule->fetchMoreWatchLater(); }
void BiliController::addToWatchLater() { m_historyModule->addToWatchLater(); }

void BiliController::fetchRelatedVideos() { m_seasonModule->fetchRelatedVideos(); }
void BiliController::setSeasonVideoTotal(int total) {
  total = qMax(0, total);
  if (m_seasonVideoTotal == total) return;
  m_seasonVideoTotal = total;
  emit seasonVideoTotalChanged();
}
void BiliController::fetchUpSeasonVideos(int page, int pageSize) { m_seasonModule->fetchUpSeasonVideos(page, pageSize); }
void BiliController::fetchMoreUpSeasonVideos() { m_seasonModule->fetchMoreUpSeasonVideos(); }
void BiliController::fetchSeasonVideos(qint64 mid, qint64 seasonId, int page, int pageSize) { m_seasonModule->fetchSeasonVideos(mid, seasonId, page, pageSize); }
void BiliController::fetchMoreSeasonVideos() { m_seasonModule->fetchMoreSeasonVideos(); }

void BiliController::setUpVideoTotal(int total) {
  total = qMax(0, total);
  if (m_upVideoTotal == total) return;
  m_upVideoTotal = total;
  emit upVideoTotalChanged();
}
void BiliController::fetchUpInfo(qint64 mid) { m_upModule->fetchUpInfo(mid); }
void BiliController::fetchUpVideos(qint64 mid, int page, int pageSize) { m_upModule->fetchUpVideos(mid, page, pageSize); }
void BiliController::fetchMoreUpVideos() { m_upModule->fetchMoreUpVideos(); }
void BiliController::fetchUpSeasons(qint64 mid) { m_upModule->fetchUpSeasons(mid); }
void BiliController::selectUpSeason(qint64 seasonId, const QString &name, bool isSeries, int total) { m_upModule->selectUpSeason(seasonId, name, isSeries, total); }
void BiliController::toggleUpFollow() { m_upModule->toggleUpFollow(); }
void BiliController::playVideoPart(int index) { m_upModule->playVideoPart(index); }
void BiliController::restartGoServer() { m_upModule->restartGoServer(); }
void BiliController::downloadVideoToDisk(int quality) { m_upModule->downloadVideoToDisk(quality); }

void BiliController::prepareImageForViewer(const QString &url) { m_viewerModule->prepareImageForViewer(url); }

#include "BiliController.h"
#include "BiliImageProvider.h"
#include "BiliModels.h"
#include "BiliNetwork.h"

#include <QDir>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QThread>
#include <QtQml>
#include <signal.h>
#include <iostream>

static QQmlEngine *s_engine = nullptr;

extern "C" {

// 全局变量
static BiliImageProvider *s_imageProvider = nullptr;
static QMutex s_providerMutex;
static QProcess *s_apiServerProcess = nullptr;
static const QString API_SERVER_PATH =
    "/userdisk/PenMods/plugins/bili_plugin/";
static const QString API_SERVER_EXEC = "server"; // Go 编译的可执行文件名

// 查找并结束旧的 API 服务进程
static void killExistingApiServer() {
  std::cout << "BiliPlugin: Checking for existing API server processes..."
            << std::endl;

  QString execPath = API_SERVER_PATH + "/" + API_SERVER_EXEC;

  // 使用 pgrep 查找进程
  QProcess pgrepProcess;
  pgrepProcess.start("pgrep", QStringList() << "-f" << execPath);
  pgrepProcess.waitForFinished(2000);

  if (pgrepProcess.exitCode() == 0) {
    QString output =
        QString::fromLocal8Bit(pgrepProcess.readAllStandardOutput()).trimmed();
    QStringList pids = output.split('\n', Qt::SkipEmptyParts);

    for (const QString &pid : pids) {
      bool ok;
      int pidNum = pid.toInt(&ok);
      if (ok && pidNum > 0) {
        std::cout << "BiliPlugin: Killing existing process PID: " << pidNum
                  << std::endl;
        kill(pidNum, SIGTERM);
      }
    }

    // 等待进程退出
    QThread::msleep(500);

    // 强制杀死仍在运行的进程
    for (const QString &pid : pids) {
      bool ok;
      int pidNum = pid.toInt(&ok);
      if (ok && pidNum > 0) {
        if (kill(pidNum, 0) == 0) {
          std::cout << "BiliPlugin: Force killing PID: " << pidNum << std::endl;
          kill(pidNum, SIGKILL);
        }
      }
    }
  }

  std::cout << "BiliPlugin: Existing API server cleanup done" << std::endl;
}

// 启动 API 服务器
static bool startApiServerImpl() {
  std::cout << "BiliPlugin: Starting API server..." << std::endl;

  // 先结束旧进程
  killExistingApiServer();

  // 等待端口释放
  QThread::msleep(300);

  // 检查可执行文件是否存在
  QString execPath = API_SERVER_PATH + "/" + API_SERVER_EXEC;
  if (!QFile::exists(execPath)) {
    std::cerr << "BiliPlugin: API server executable not found: "
              << execPath.toStdString() << std::endl;
    return false;
  }

  // 检查执行权限
  QFile serverFile(execPath);
  if (!(serverFile.permissions() & QFile::ExeUser)) {
    std::cout << "BiliPlugin: Setting executable permission..." << std::endl;
    serverFile.setPermissions(serverFile.permissions() | QFile::ExeUser |
                              QFile::ExeGroup | QFile::ExeOther);
  }

  // 创建新进程
  s_apiServerProcess = new QProcess();
  s_apiServerProcess->setWorkingDirectory(API_SERVER_PATH);

  // 连接信号用于调试
  QObject::connect(
      s_apiServerProcess, &QProcess::readyReadStandardOutput, []() {
        if (s_apiServerProcess) {
          std::cout << "API Server: "
                    << s_apiServerProcess->readAllStandardOutput().constData();
        }
      });

  QObject::connect(s_apiServerProcess, &QProcess::readyReadStandardError, []() {
    if (s_apiServerProcess) {
      std::cerr << "API Server Error: "
                << s_apiServerProcess->readAllStandardError().constData();
    }
  });

  QObject::connect(
      s_apiServerProcess,
      QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
      [](int exitCode, QProcess::ExitStatus exitStatus) {
        std::cout << "BiliPlugin: API server exited with code " << exitCode
                  << ", status: "
                  << (exitStatus == QProcess::NormalExit ? "normal" : "crashed")
                  << std::endl;
      });

  // 直接启动 Go 可执行文件
  s_apiServerProcess->start(execPath, QStringList());

  // 等待启动
  if (!s_apiServerProcess->waitForStarted(5000)) {
    std::cerr << "BiliPlugin: Failed to start API server: "
              << s_apiServerProcess->errorString().toStdString() << std::endl;
    delete s_apiServerProcess;
    s_apiServerProcess = nullptr;
    return false;
  }

  // 等待服务器初始化
  QThread::msleep(1000);

  // 检查进程是否还在运行
  if (s_apiServerProcess->state() != QProcess::Running) {
    std::cerr << "BiliPlugin: API server failed to stay running" << std::endl;
    delete s_apiServerProcess;
    s_apiServerProcess = nullptr;
    return false;
  }

  std::cout << "BiliPlugin: API server started successfully, PID: "
            << s_apiServerProcess->processId() << std::endl;
  return true;
}

// 停止 API 服务器
static void stopApiServerImpl() {
  std::cout << "BiliPlugin: Stopping API server..." << std::endl;

  if (s_apiServerProcess) {
    // 优雅关闭
    s_apiServerProcess->terminate();

    // 等待退出
    if (!s_apiServerProcess->waitForFinished(3000)) {
      std::cout << "BiliPlugin: API server not responding, force killing..."
                << std::endl;
      s_apiServerProcess->kill();
      s_apiServerProcess->waitForFinished(2000);
    }

    delete s_apiServerProcess;
    s_apiServerProcess = nullptr;
  }

  // 确保清理所有残留进程
  killExistingApiServer();

  std::cout << "BiliPlugin: API server stopped" << std::endl;
}

} // extern "C"

// 供控制器调用的 API Server 控制函数
bool bili_startApiServer() {
  return startApiServerImpl();
}

void bili_stopApiServer() {
  stopApiServerImpl();
}

extern "C" {

void init_plugin() {
  std::cout << "BiliPlugin: Initializing..." << std::endl;

  qmlRegisterType<BiliController>("BiliPlugin", 1, 0, "BiliController");
  qmlRegisterType<VideoListModel>("BiliPlugin", 1, 0, "VideoListModel");
  qmlRegisterType<CommentListModel>("BiliPlugin", 1, 0, "CommentListModel");
  qmlRegisterType<HotSearchModel>("BiliPlugin", 1, 0, "HotSearchModel");
  qmlRegisterType<SearchResultModel>("BiliPlugin", 1, 0, "SearchResultModel");
  qmlRegisterType<VideoPartListModel>("BiliPlugin", 1, 0, "VideoPartListModel");
  qmlRegisterType<FavoriteFolderModel>("BiliPlugin", 1, 0, "FavoriteFolderModel");
  qmlRegisterType<UpSeasonListModel>("BiliPlugin", 1, 0, "UpSeasonListModel");

  // 启动 API 服务器
  if (!bili_startApiServer()) {
    std::cerr << "BiliPlugin: Warning - API server failed to start"
              << std::endl;
  }

  std::cout << "BiliPlugin: Registered successfully!" << std::endl;
}

void attach_engine(QQmlEngine *engine) {
  QMutexLocker locker(&s_providerMutex);

  s_engine = engine;

  if (s_engine) {
    QString pluginPath = "/userdisk/PenMods/plugins/bili_plugin/qml";
    engine->addImportPath(pluginPath);

    QString playerPath = "/userdisk/PenMods/plugins/bili_plugin/";
    engine->addImportPath(playerPath);

    BiliNetwork *network = BiliNetwork::instance();
    s_imageProvider = new BiliImageProvider(network);

    s_engine->addImageProvider("bili", s_imageProvider);

    std::cout << "BiliPlugin: Engine attached, ImageProvider registered"
              << std::endl;
  }
}

void destroy_plugin() {
  std::cout << "BiliPlugin: Destroying..." << std::endl;

  // 先停止 API 服务器
  bili_stopApiServer();

  {
    QMutexLocker locker(&s_providerMutex);
    s_imageProvider = nullptr;
  }

  // 取消所有网络请求
  BiliNetwork *network = BiliNetwork::instance();
  if (network) {
    network->cancelAllRequests();
  }

  QThread::msleep(200);

  s_engine = nullptr;

  std::cout << "BiliPlugin: Destroyed successfully" << std::endl;
}

} // extern "C"
