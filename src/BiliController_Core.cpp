#include "BiliController.h"
#include "BiliModels.h"
#include "BiliNetwork.h"

#include <QDateTime>
#include <QSettings>
#include <QStringListModel>
#include <QTimer>
#include <QDebug>

// 安全回调包装宏 - 在回调执行前检查对象是否仍存在
#define SAFE_CALLBACK(controller, ...)                                         \
  [ guard = QPointer<BiliController>(controller), __VA_ARGS__ ]

BiliController::BiliController(QObject *parent)
    : QObject(parent), m_network(BiliNetwork::instance()),
      m_currentPage("home"), m_playQuality(64), m_isDownloading(false),
      m_downloadProgress(0), m_loggedIn(false), m_isFavorited(false), m_isCoined(false), m_isLiked(false),
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
  m_subtitleOutline = settings.value("subtitleOutline", m_subtitleOutline).toDouble();
  m_subtitleMarginV = settings.value("subtitleMarginV", m_subtitleMarginV).toInt();
  m_subtitleSpacing = settings.value("subtitleSpacing", m_subtitleSpacing).toDouble();
  m_subtitleBold = settings.value("subtitleBold", m_subtitleBold).toInt();
}

BiliController::~BiliController() {
  m_destroying = true;
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
    m_searchHistory.clear();
    m_searchHistoryModel->setStringList(m_searchHistory);
    saveSearchHistory();
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
  emit loginStateChanged();
}
