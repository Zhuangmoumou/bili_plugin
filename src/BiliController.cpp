#include "BiliController.h"
#include "BiliImageProvider.h"
#include "BiliModels.h"
#include "BiliNetwork.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QJsonArray>
#include <QPointer>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSettings>
#include <QTimer>
#include <QStringListModel>
#include <iostream>

static QQmlEngine *s_engine = nullptr;

// 安全回调包装宏 - 在回调执行前检查对象是否仍存在
#define SAFE_CALLBACK(controller, ...)                                         \
  [ guard = QPointer<BiliController>(controller), __VA_ARGS__ ]

BiliController::BiliController(QObject *parent)
    : QObject(parent), m_network(BiliNetwork::instance()),
      m_currentPage("home"), m_playQuality(64), m_isDownloading(false),
      m_downloadProgress(0), m_loggedIn(false), m_userId(0), m_userLevel(0),
      m_userCoins(0), m_userFans(0), m_userFollowing(0), m_userIsVip(false),
      m_popularPage(1), m_searchPage(1), m_commentPage(1), m_isLoading(false),
      m_loadingCount(0), m_popularModel(new VideoListModel(this)),
      m_rankingModel(new VideoListModel(this)),
      m_searchModel(new SearchResultModel(this)),
      m_commentModel(new CommentListModel(this)),
      m_hotSearchModel(new HotSearchModel(this)),
      m_videoPartModel(new VideoPartListModel(this)), 
      m_searchHistoryModel(new QStringListModel(this)),
      m_favoriteFolderModel(new FavoriteFolderModel(this)),
      m_favoriteItemModel(new VideoListModel(this)),
      m_favoritePage(1),
      m_currentFavoriteId(0),
      m_destroying(false) {
  connect(m_network, &BiliNetwork::networkError, this,
          [this](const QString &msg) {
            if (!m_destroying)
              setGlobalError(msg);
          });

  // 后台会话验证定时器：每 5 分钟检查一次登录状态（用户无感知）
  QTimer *sessionCheckTimer = new QTimer(this);
  connect(sessionCheckTimer, &QTimer::timeout, this, [this]() {
    if (m_loggedIn) {
      checkLoginStatus();
    }
  });
  sessionCheckTimer->start(5 * 60 * 1000); // 5 分钟

  // 默认可用清晰度
  m_acceptQualities = {16, 32, 64};

  loadLoginStatus();
  loadSearchHistory();
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
QObject *BiliController::hotSearchModel() { return m_hotSearchModel; }

QObject *BiliController::videoPartModel() { return m_videoPartModel; }

QObject *BiliController::searchHistoryModel() { return m_searchHistoryModel; }
QObject *BiliController::favoriteFolderModel() { return m_favoriteFolderModel; }
QObject *BiliController::favoriteItemModel() { return m_favoriteItemModel; }

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
  m_loadingCount = 0;
  if (m_isLoading) {
    m_isLoading = false;
    emit isLoadingChanged();
  }
}

// ====== API: 热门视频 ======

void BiliController::fetchPopular(int page, int pageSize) {
  std::cout << "[BiliCtrl] fetchPopular: page=" << page << std::endl;

  if (m_popularModel->loading())
    return;

  // 参数校验
  page = qBound(1, page, 1000);
  pageSize = qBound(1, pageSize, 30);

  m_popularPage = page;
  if (page == 1) {
    m_popularModel->clear();
  }
  m_popularModel->setLoading(true);
  m_popularModel->setErrorMessage("");
  setIsLoading(true);

  std::cout << "[BiliCtrl] fetchPopular use /recommend" << std::endl;

  QMap<QString, QString> paramsRecommend;
  // fresh_type: 3 表示换一换推荐，4 用于后续刷新
  paramsRecommend["fresh_type"] = (page <= 1) ? "3" : "4";

  QPointer<BiliController> self(this);

  auto onSuccess = [self](const QJsonObject &data) {
    if (!self)
      return;

    QJsonArray list = data.value("list").toArray();
    if (list.isEmpty()) {
      list = data.value("item").toArray();
    }
    if (list.isEmpty()) {
      list = data.value("items").toArray();
    }

    bool noMore = data.value("no_more").toBool(false);

    QVector<VideoItem> items;
    items.reserve(list.size());
    for (const QJsonValue &v : list) {
      if (v.isObject()) {
        items.append(VideoListModel::parseVideoItem(v.toObject()));
      }
    }

    self->m_popularModel->appendItems(items);
    // 推荐流可能没有 no_more 字段，按是否有数据判断
    self->m_popularModel->setHasMore(noMore ? false : !items.isEmpty());
    self->m_popularModel->setLoading(false);
    self->setIsLoading(false);

    if (items.isEmpty() && self->m_popularPage == 1) {
      self->m_popularModel->setErrorMessage("暂无推荐视频");
    }
  };

  auto onErrorFinal = [self](int code, const QString &msg) {
    Q_UNUSED(code)
    if (!self)
      return;

    self->m_popularModel->setLoading(false);
    self->m_popularModel->setErrorMessage(msg);
    self->setIsLoading(false);
    emit self->toastMessage(QString("加载失败：%1").arg(msg));
  };

  m_network->get("/recommend", paramsRecommend, onSuccess, onErrorFinal);
}

void BiliController::fetchMorePopular() {
  if (!m_popularModel->hasMore() || m_popularModel->loading())
    return;
  if (m_popularModel->count() <= 0)
    return;
  m_popularPage++;
  fetchPopular(m_popularPage, 10);
}

// ====== API: 排行榜 ======

void BiliController::fetchRanking(int rid) {
  if (m_rankingModel->loading())
    return;

  rid = qBound(0, rid, 9999);

  m_rankingModel->clear();
  m_rankingModel->setLoading(true);
  setIsLoading(true);

  QMap<QString, QString> params;
  params["rid"] = QString::number(rid);
  params["type"] = "all";

  QPointer<BiliController> self(this);

  m_network->get(
      "/ranking", params,
      [self](const QJsonObject &data) {
        if (!self)
          return;

        QJsonArray list = data.value("list").toArray();
        QVector<VideoItem> items;
        items.reserve(list.size());
        for (const QJsonValue &v : list) {
          if (v.isObject()) {
            items.append(VideoListModel::parseVideoItem(v.toObject()));
          }
        }

        self->m_rankingModel->appendItems(items);
        self->m_rankingModel->setHasMore(false);
        self->m_rankingModel->setLoading(false);
        self->setIsLoading(false);
      },
      [self](int code, const QString &msg) {
        Q_UNUSED(code)
        if (!self)
          return;

        self->m_rankingModel->setLoading(false);
        self->m_rankingModel->setErrorMessage(msg);
        self->setIsLoading(false);
        emit self->toastMessage(QString("排行榜加载失败：%1").arg(msg));
      });
}

// ====== API: 热搜 ======

void BiliController::fetchHotSearch() {
  if (m_hotSearchModel->loading())
    return;

  m_hotSearchModel->setLoading(true);

  QMap<QString, QString> params;
  params["limit"] = "10";

  QPointer<BiliController> self(this);

  m_network->get(
      "/hot/search", params,
      [self](const QJsonObject &data) {
        if (!self)
          return;

        QJsonObject trending = data.value("trending").toObject();
        QJsonArray list = trending.value("list").toArray();

        QVector<HotSearchItem> items;
        items.reserve(qMin(list.size(), 50)); // 最多 50 个
        int pos = 1;
        for (const QJsonValue &v : list) {
          if (pos > 50)
            break; // 硬限制
          QJsonObject obj = v.toObject();
          HotSearchItem item;
          item.keyword = obj.value("keyword").toString().left(100); // 限制长度
          item.icon = obj.value("icon").toString();
          item.position = pos++;
          items.append(item);
        }

        self->m_hotSearchModel->setItems(items);
        self->m_hotSearchModel->setLoading(false);
      },
      [self](int code, const QString &msg) {
        Q_UNUSED(code)
        if (!self)
          return;

        self->m_hotSearchModel->setLoading(false);
        emit self->toastMessage(QString("热搜加载失败：%1").arg(msg));
      });
}

// ====== API: 搜索 ======

void BiliController::search(const QString &keyword, int page) {
  QString trimmed = keyword.trimmed();
  if (trimmed.isEmpty()) {
    emit toastMessage("请输入搜索关键词");
    return;
  }
  if (m_searchModel->loading())
    return;

  // 限制关键词长度
  if (trimmed.length() > 100) {
    trimmed = trimmed.left(100);
  }

  page = qBound(1, page, 100);

  m_searchKeyword = trimmed;
  m_searchPage = page;

  if (page == 1) {
    m_searchModel->clear();
  }
  m_searchModel->setKeyword(m_searchKeyword);
  m_searchModel->setLoading(true);
  setIsLoading(true);

  // 更新搜索历史
  m_searchHistory.removeAll(trimmed);          // 移除旧的重复项
  m_searchHistory.prepend(trimmed);            // 添加到最前面
  while (m_searchHistory.size() > 10) {         // 保持最多10个
    m_searchHistory.removeLast();
  }
  m_searchHistoryModel->setStringList(m_searchHistory);
  saveSearchHistory();

  QMap<QString, QString> params;
  params["keyword"] = m_searchKeyword;
  params["page"] = QString::number(page);

  QPointer<BiliController> self(this);

  m_network->get(
      "/search", params,
      [self](const QJsonObject &data) {
        if (!self)
          return;

        QJsonArray list = data.value("result").toArray();
        if (list.isEmpty()) {
          list = data.value("list").toArray();
        }

        QVector<VideoItem> items;
        items.reserve(list.size());
        for (const QJsonValue &v : list) {
          if (v.isObject()) {
            items.append(VideoListModel::parseVideoItem(v.toObject()));
          }
        }

        self->m_searchModel->appendItems(items);
        self->m_searchModel->setHasMore(!items.isEmpty());
        self->m_searchModel->setLoading(false);
        self->setIsLoading(false);

        if (items.isEmpty() && self->m_searchPage == 1) {
          self->m_searchModel->setErrorMessage(
              QString("未找到%1相关视频").arg(self->m_searchKeyword));
        }
      },
      [self](int code, const QString &msg) {
        Q_UNUSED(code)
        if (!self)
          return;

        self->m_searchModel->setLoading(false);
        self->m_searchModel->setErrorMessage(msg);
        self->setIsLoading(false);
        emit self->toastMessage(QString("搜索失败：%1").arg(msg));
      });
}

void BiliController::searchMore() {
  if (!m_searchModel->hasMore() || m_searchModel->loading())
    return;
  m_searchPage++;
  search(m_searchKeyword, m_searchPage);
}

// ====== API: 视频详情 ======

void BiliController::fetchVideoDetail(const QString &bvid) {
  if (bvid.isEmpty()) {
    emit toastMessage("视频 ID 为空");
    return;
  }

  // BV号格式校验
  if (!bvid.startsWith("BV") || bvid.length() < 10) {
    emit toastMessage("无效的视频 ID");
    return;
  }

  setIsLoading(true);

  QMap<QString, QString> params;
  params["bvid"] = bvid;

  QPointer<BiliController> self(this);

  m_network->get(
      "/video/info", params,
      [self](const QJsonObject &data) {
        if (!self)
          return;

        self->m_currentVideo = VideoListModel::parseVideoItem(data);
        self->m_currentVideo.aid = data.value("aid").toVariant().toLongLong();

        QJsonArray pages = data.value("pages").toArray();
        if (!pages.isEmpty() && self->m_currentVideo.cid == 0) {
          QJsonObject firstPage = pages.first().toObject();
          self->m_currentVideo.cid =
              firstPage.value("cid").toVariant().toLongLong();
        }

        // 解析分P列表
        if (pages.count() > 1) { // 只有多于1P时才显示列表
            QVector<VideoPartItem> parts;
            parts.reserve(pages.size());
            for (const QJsonValue &v : pages) {
                if (v.isObject()) {
                    parts.append(VideoPartListModel::parseVideoPartItem(v.toObject()));
                }
            }
            self->m_videoPartModel->setItems(parts);
        } else {
            self->m_videoPartModel->clear();
        }

        emit self->videoDetailChanged();
        self->setIsLoading(false);
      },
      [self](int code, const QString &msg) {
        Q_UNUSED(code)
        if (!self)
          return;

        self->setIsLoading(false);
        self->m_videoPartModel->clear();
        emit self->toastMessage(QString("获取视频信息失败：%1").arg(msg));
      });
}

// ====== API: 播放地址 ======

void BiliController::fetchPlayUrl(int quality) {
  if (m_currentVideo.bvid.isEmpty() || m_currentVideo.cid == 0) {
    emit toastMessage("视频信息不完整，无法播放");
    return;
  }

  quality = qBound(16, quality, 127);
  setIsLoading(true);

  QMap<QString, QString> params;
  params["aid"] = QString::number(videoAid());
  params["cid"] = QString::number(m_currentVideo.cid);
  params["qn"] = QString::number(quality);
  params["bvid"] = m_currentVideo.bvid;
  // fnval=1: 优先请求 MP4 格式，fnval=16: DASH 格式（音视频分离）
  // 优先使用 MP4 格式以获得更好的兼容性
  params["fnval"] = "1";

  QPointer<BiliController> self(this);

  m_network->get(
      "/video/playurl", params,
      [self, quality](const QJsonObject &data) {
        if (!self)
          return;

        QString videoUrl;
        QString audioUrl;
        int requestedQuality = quality;
        int finalQuality = requestedQuality;
        int apiQuality = data.value("quality").toInt(0);
        if (apiQuality > 0) {
          finalQuality = apiQuality;
        }

        // 解析可用清晰度
        QVector<int> newAccepts;
        QJsonArray accept = data.value("accept_quality").toArray();
        for (const QJsonValue &v : accept) {
          int q = v.toInt(0);
          if (q > 0) newAccepts.append(q);
        }
        // 兼容 high_format（如 1080P/60帧）
        QJsonObject highFormat = data.value("high_format").toObject();
        int highQn = highFormat.value("quality").toInt(0);
        int highCanWatch = highFormat.value("can_watch_qn_reason").toInt(0);
        int highLimit = highFormat.value("limit_watch_reason").toInt(0);
        if (highQn > 0 && highCanWatch == 0 && highLimit == 0 &&
            !newAccepts.contains(highQn)) {
          newAccepts.append(highQn);
        }
        // 兼容 support_formats（仅保留可观看清晰度）
        QJsonArray supportFormats = data.value("support_formats").toArray();
        for (const QJsonValue &v : supportFormats) {
          QJsonObject obj = v.toObject();
          int q = obj.value("quality").toInt(0);
          int canWatch = obj.value("can_watch_qn_reason").toInt(0);
          int limit = obj.value("limit_watch_reason").toInt(0);
          if (q > 0 && canWatch == 0 && limit == 0 &&
              !newAccepts.contains(q)) {
            newAccepts.append(q);
          }
        }

        if (!newAccepts.isEmpty() && newAccepts != self->m_acceptQualities) {
          self->m_acceptQualities = newAccepts;
          emit self->acceptQualitiesChanged();
        }

        // 优先 durl 格式（MP4）
        QJsonArray durl = data.value("durl").toArray();
        if (!durl.isEmpty()) {
          QJsonObject first = durl.first().toObject();
          videoUrl = first.value("url").toString();
          if (videoUrl.isEmpty()) {
            QJsonArray backup = first.value("backup_url").toArray();
            if (!backup.isEmpty()) {
              videoUrl = backup.first().toString();
            }
          }
        }

        // 回退到 dash 格式
        if (videoUrl.isEmpty()) {
          QJsonObject dash = data.value("dash").toObject();
          if (!dash.isEmpty()) {
            QJsonArray videoArray = dash.value("video").toArray();
            QJsonArray audioArray = dash.value("audio").toArray();

            // 按画质优先级选择（优先用户选择）
            auto pickVideoByQn = [&](int qn) -> bool {
              for (const QJsonValue &v : videoArray) {
                QJsonObject videoObj = v.toObject();
                if (videoObj.value("id").toInt(0) == qn) {
                  QString url = videoObj.value("base_url").toString();
                  if (url.isEmpty())
                    url = videoObj.value("baseUrl").toString();
                  if (!url.isEmpty()) {
                    videoUrl = url;
                    finalQuality = qn;
                    return true;
                  }
                }
              }
              return false;
            };

            // 先尝试用户选择的清晰度
            pickVideoByQn(requestedQuality);

            // 回退到其他画质
            if (videoUrl.isEmpty()) {
              QVector<int> qualityOrder = {16, 32, 64, 80, 112, 116, 120, 125};
              for (int qn : qualityOrder) {
                if (qn == requestedQuality)
                  continue;
                if (pickVideoByQn(qn))
                  break;
              }
            }

            // 获取音频流
            if (!audioArray.isEmpty()) {
              QVector<int> audioOrder = {30280, 30232, 30216};
              for (int audioQn : audioOrder) {
                for (const QJsonValue &a : audioArray) {
                  QJsonObject audioObj = a.toObject();
                  if (audioObj.value("id").toInt(0) == audioQn) {
                    audioUrl = audioObj.value("base_url").toString();
                    if (audioUrl.isEmpty())
                      audioUrl = audioObj.value("baseUrl").toString();
                    if (!audioUrl.isEmpty())
                      break;
                  }
                }
                if (!audioUrl.isEmpty())
                  break;
              }
            }
          }
        }

        if (videoUrl.isEmpty()) {
          emit self->toastMessage("未获取到播放地址");
          self->setIsLoading(false);
          return;
        }

        // URL 安全验证
        QUrl parsedUrl(videoUrl);
        if (!parsedUrl.isValid() || (!parsedUrl.scheme().startsWith("http"))) {
          emit self->toastMessage("播放地址无效");
          self->setIsLoading(false);
          return;
        }

        self->m_playUrl = videoUrl;
        self->m_playQuality = finalQuality;

        if (!audioUrl.isEmpty()) {
          QUrl parsedAudio(audioUrl);
          if (parsedAudio.isValid()) {
            self->m_playUrl = videoUrl + "|" + audioUrl;
            qDebug() << "[BiliController] DASH: video + audio";
          }
        }

        emit self->playUrlChanged();
        self->setIsLoading(false);

        if (!self->m_playUrl.isEmpty()) {
          emit self->playbackReady(self->m_playUrl);
        }
      },
      [self](int code, const QString &msg) {
        Q_UNUSED(code)
        if (!self)
          return;

        self->setIsLoading(false);
        emit self->toastMessage(QString("获取播放地址失败：%1").arg(msg));
      });
}

// ====== 仅获取可用清晰度 ======

void BiliController::fetchAcceptQualities(int quality) {
  if (m_currentVideo.bvid.isEmpty() || m_currentVideo.cid == 0) {
    emit toastMessage("视频信息不完整，无法获取清晰度");
    return;
  }

  quality = qBound(16, quality, 127);
  setIsLoading(true);

  QMap<QString, QString> params;
  params["aid"] = QString::number(videoAid());
  params["cid"] = QString::number(m_currentVideo.cid);
  params["qn"] = QString::number(quality);
  params["bvid"] = m_currentVideo.bvid;
  params["fnval"] = "1";

  QPointer<BiliController> self(this);

  m_network->get(
      "/video/playurl", params,
      [self](const QJsonObject &data) {
        if (!self)
          return;

        QVector<int> newAccepts;
        QJsonArray accept = data.value("accept_quality").toArray();
        for (const QJsonValue &v : accept) {
          int q = v.toInt(0);
          if (q > 0) newAccepts.append(q);
        }
        // 兼容 high_format（如 1080P/60帧）
        QJsonObject highFormat = data.value("high_format").toObject();
        int highQn = highFormat.value("quality").toInt(0);
        int highCanWatch = highFormat.value("can_watch_qn_reason").toInt(0);
        int highLimit = highFormat.value("limit_watch_reason").toInt(0);
        if (highQn > 0 && highCanWatch == 0 && highLimit == 0 &&
            !newAccepts.contains(highQn)) {
          newAccepts.append(highQn);
        }
        // 兼容 support_formats（仅保留可观看清晰度）
        QJsonArray supportFormats = data.value("support_formats").toArray();
        for (const QJsonValue &v : supportFormats) {
          QJsonObject obj = v.toObject();
          int q = obj.value("quality").toInt(0);
          int canWatch = obj.value("can_watch_qn_reason").toInt(0);
          int limit = obj.value("limit_watch_reason").toInt(0);
          if (q > 0 && canWatch == 0 && limit == 0 &&
              !newAccepts.contains(q)) {
            newAccepts.append(q);
          }
        }

        if (!newAccepts.isEmpty() && newAccepts != self->m_acceptQualities) {
          self->m_acceptQualities = newAccepts;
          emit self->acceptQualitiesChanged();
        }

        self->setIsLoading(false);
      },
      [self](int, const QString &msg) {
        if (!self)
          return;
        self->setIsLoading(false);
        emit self->toastMessage(QString("获取清晰度失败：%1").arg(msg));
      });
}

// ====== 下载并播放视频 ======

void BiliController::downloadAndPlay(int quality) {
  if (m_currentVideo.bvid.isEmpty() || m_currentVideo.cid == 0) {
    emit toastMessage("视频信息不完整，无法下载");
    return;
  }

  if (m_isDownloading) {
    emit toastMessage("正在下载中，请稍候...");
    return;
  }

  quality = qBound(16, quality, 127);
  setIsLoading(true);

  // 先清理旧的临时文件
  cleanupTempVideo();

  // 生成临时文件路径
  QString tempDir =
      QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  QString fileName = QString("bili_%1_%2.mp4")
                         .arg(m_currentVideo.bvid)
                         .arg(QDateTime::currentMSecsSinceEpoch());
  m_tempVideoPath = QDir(tempDir).filePath(fileName);

  QMap<QString, QString> params;
  params["aid"] = QString::number(videoAid());
  params["cid"] = QString::number(m_currentVideo.cid);
  params["qn"] = QString::number(quality);
  params["bvid"] = m_currentVideo.bvid;
  params["fnval"] = "1"; // MP4 格式

  QPointer<BiliController> self(this);

  m_network->get(
      "/video/playurl", params,
      [self, quality](const QJsonObject &data) {
        if (!self)
          return;

        QString videoUrl;
        int requestedQuality = quality;
        int finalQuality = requestedQuality;
        int apiQuality = data.value("quality").toInt(0);
        if (apiQuality > 0) {
          finalQuality = apiQuality;
        }

        // 解析可用清晰度
        QVector<int> newAccepts;
        QJsonArray accept = data.value("accept_quality").toArray();
        for (const QJsonValue &v : accept) {
          int q = v.toInt(0);
          if (q > 0) newAccepts.append(q);
        }
        // 兼容 high_format（如 1080P/60帧）
        QJsonObject highFormat = data.value("high_format").toObject();
        int highQn = highFormat.value("quality").toInt(0);
        int highCanWatch = highFormat.value("can_watch_qn_reason").toInt(0);
        int highLimit = highFormat.value("limit_watch_reason").toInt(0);
        if (highQn > 0 && highCanWatch == 0 && highLimit == 0 &&
            !newAccepts.contains(highQn)) {
          newAccepts.append(highQn);
        }
        // 兼容 support_formats（仅保留可观看清晰度）
        QJsonArray supportFormats = data.value("support_formats").toArray();
        for (const QJsonValue &v : supportFormats) {
          QJsonObject obj = v.toObject();
          int q = obj.value("quality").toInt(0);
          int canWatch = obj.value("can_watch_qn_reason").toInt(0);
          int limit = obj.value("limit_watch_reason").toInt(0);
          if (q > 0 && canWatch == 0 && limit == 0 &&
              !newAccepts.contains(q)) {
            newAccepts.append(q);
          }
        }

        if (!newAccepts.isEmpty() && newAccepts != self->m_acceptQualities) {
          self->m_acceptQualities = newAccepts;
          emit self->acceptQualitiesChanged();
        }

        // 优先 durl 格式（MP4）
        QJsonArray durl = data.value("durl").toArray();
        if (!durl.isEmpty()) {
          QJsonObject first = durl.first().toObject();
          videoUrl = first.value("url").toString();
          if (videoUrl.isEmpty()) {
            QJsonArray backup = first.value("backup_url").toArray();
            if (!backup.isEmpty()) {
              videoUrl = backup.first().toString();
            }
          }
        }

        // 回退到 dash 格式（仅视频轨）
        if (videoUrl.isEmpty()) {
          QJsonObject dash = data.value("dash").toObject();
          if (!dash.isEmpty()) {
            QJsonArray videoArray = dash.value("video").toArray();

            auto pickVideoByQn = [&](int qn) -> bool {
              for (const QJsonValue &v : videoArray) {
                QJsonObject videoObj = v.toObject();
                if (videoObj.value("id").toInt(0) == qn) {
                  videoUrl = videoObj.value("base_url").toString();
                  if (videoUrl.isEmpty())
                    videoUrl = videoObj.value("baseUrl").toString();
                  if (!videoUrl.isEmpty()) {
                    finalQuality = qn;
                    return true;
                  }
                }
              }
              return false;
            };

            // 先尝试用户选择的清晰度
            pickVideoByQn(requestedQuality);

            if (videoUrl.isEmpty()) {
              QVector<int> qualityOrder = {16, 32, 64, 80, 112, 116, 120, 125};
              for (int qn : qualityOrder) {
                if (qn == requestedQuality)
                  continue;
                if (pickVideoByQn(qn))
                  break;
              }
            }
          }
        }

        if (videoUrl.isEmpty()) {
          emit self->toastMessage("未获取到播放地址");
          self->setIsLoading(false);
          return;
        }

        // 开始下载
        self->m_isDownloading = true;
        self->m_downloadProgress = 0;
        self->m_downloadStatus = "正在下载...";
        emit self->downloadStateChanged();

        self->m_network->downloadVideo(
            videoUrl, self->m_tempVideoPath,
            // 成功回调
            [self, finalQuality](const QString &path) {
              if (!self)
                return;

              self->m_isDownloading = false;
              self->m_downloadProgress = 1.0;
              self->m_downloadStatus = "下载完成";
              self->m_playUrl = path;
              self->m_playQuality = finalQuality;
              emit self->downloadStateChanged();
              emit self->playUrlChanged();
              self->setIsLoading(false);

              if (!self->m_playUrl.isEmpty()) {
                emit self->playbackReady(self->m_playUrl);
              }
            },
            // 错误回调
            [self](int code, const QString &msg) {
              Q_UNUSED(code)
              if (!self)
                return;

              self->m_isDownloading = false;
              self->m_downloadProgress = 0;
              self->m_downloadStatus.clear();
              emit self->downloadStateChanged();
              self->setIsLoading(false);
              emit self->toastMessage(QString("下载失败：%1").arg(msg));
            },
            // 进度回调
            [self](qint64 received, qint64 total) {
              if (!self)
                return;

              double progress =
                  total > 0 ? static_cast<double>(received) / total : 0;
              self->m_downloadProgress = progress;
              QString sizeStr;
              if (total > 0) {
                double mb = received / (1024.0 * 1024.0);
                double totalMb = total / (1024.0 * 1024.0);
                sizeStr = QString("%1MB / %2MB")
                              .arg(mb, 0, 'f', 1)
                              .arg(totalMb, 0, 'f', 1);
              } else {
                double mb = received / (1024.0 * 1024.0);
                sizeStr = QString("%1MB").arg(mb, 0, 'f', 1);
              }
              self->m_downloadStatus =
                  QString("正在下载... %1 (%2%)")
                      .arg(sizeStr)
                      .arg(static_cast<int>(progress * 100));
              emit self->downloadStateChanged();
            });
      },
      [self](int code, const QString &msg) {
        Q_UNUSED(code)
        if (!self)
          return;

        self->setIsLoading(false);
        emit self->toastMessage(QString("获取播放地址失败：%1").arg(msg));
      });
}

void BiliController::cancelDownload() {
  if (!m_isDownloading)
    return;

  // 调用新的、专门的取消下载方法，避免死锁
  m_network->cancelVideoDownload();

  // 这里可以立即更新UI状态，因为网络层的abort()会异步触发错误回调
  // 错误回调会再次更新状态，但这里的即时更新能提供更好的用户反馈
  m_isDownloading = false;
  m_downloadStatus = "正在取消...";
  emit downloadStateChanged();
  emit toastMessage("正在取消下载...");
}

void BiliController::cleanupTempVideo() {
  if (!m_tempVideoPath.isEmpty()) {
    QFile file(m_tempVideoPath);
    if (file.exists()) {
      file.remove();
      std::cout << "[BiliController] Cleaned up temp video: "
                << m_tempVideoPath.toStdString() << std::endl;
    }
    m_tempVideoPath.clear();
  }
}

// ====== API: 评论 ======

void BiliController::fetchComments(int page) {
  if (m_currentVideo.bvid.isEmpty()) {
    emit toastMessage("请先打开一个视频");
    return;
  }
  if (m_commentModel->loading())
    return;
  if (m_currentVideo.aid <= 0) {
    emit toastMessage("视频信息不完整");
    return;
  }

  page = qBound(1, page, 1000);
  m_commentPage = page;
  if (page == 1) {
    m_commentModel->clear();
  }
  m_commentModel->setLoading(true);

  QMap<QString, QString> params;
  params["oid"] = QString::number(m_currentVideo.aid);
  params["type"] = "1";
  params["pn"] = QString::number(page);
  params["ps"] = "10";

  QPointer<BiliController> self(this);

  m_network->get(
      "/video/comments", params,
      [self](const QJsonObject &data) {
        if (!self)
          return;

        QJsonObject pageObj = data.value("page").toObject();
        int total = pageObj.value("count").toInt();
        self->m_commentModel->setTotalCount(total);

        QJsonArray replies = data.value("replies").toArray();

        QVector<CommentItem> items;
        items.reserve(replies.size());
        for (const QJsonValue &v : replies) {
          if (v.isObject()) {
            items.append(CommentListModel::parseCommentItem(v.toObject()));
          }
        }

        self->m_commentModel->appendItems(items);
        self->m_commentModel->setLoading(false);

        if (items.isEmpty() && self->m_commentPage == 1) {
          self->m_commentModel->setErrorMessage("暂无评论");
        }
      },
      [self](int code, const QString &msg) {
        if (!self)
          return;

        self->m_commentModel->setLoading(false);
        if (code == -404 || msg == "啥都木有") {
          self->m_commentModel->setErrorMessage("暂无评论");
        } else {
          self->m_commentModel->setErrorMessage(msg);
          emit self->toastMessage(QString("评论加载失败：%1").arg(msg));
        }
      });
}

void BiliController::fetchMoreComments() {
  if (m_commentModel->loading())
    return;
  m_commentPage++;
  fetchComments(m_commentPage);
}

// ====== API: 收藏夹 ======

void BiliController::fetchFavoriteFolders() {
  if (!m_loggedIn || m_userId <= 0) {
    emit toastMessage("请先登录后查看收藏夹");
    return;
  }
  if (m_favoriteFolderModel->loading())
    return;

  m_favoriteFolderModel->setLoading(true);
  setIsLoading(true);

  QMap<QString, QString> params;
  params["mid"] = QString::number(m_userId);

  QPointer<BiliController> self(this);

  m_network->get(
      "/fav/folder/list", params,
      [self](const QJsonObject &data) {
        if (!self)
          return;

        QJsonArray list = data.value("list").toArray();
        QVector<FavoriteFolderItem> items;
        items.reserve(list.size());
        for (const QJsonValue &v : list) {
          if (v.isObject()) {
            items.append(FavoriteFolderModel::parseFavoriteFolderItem(v.toObject()));
          }
        }

        self->m_favoriteFolderModel->setItems(items);
        self->m_favoriteFolderModel->setLoading(false);
        self->setIsLoading(false);

        if (items.isEmpty()) {
          emit self->toastMessage("暂无收藏夹");
        }
      },
      [self](int, const QString &msg) {
        if (!self)
          return;

        self->m_favoriteFolderModel->setLoading(false);
        self->setIsLoading(false);
        emit self->toastMessage(QString("收藏夹加载失败：%1").arg(msg));
      });
}

void BiliController::fetchFavoriteItems(qint64 mediaId, int page, int pageSize) {
  if (mediaId <= 0) {
    emit toastMessage("收藏夹 ID 无效");
    return;
  }
  if (m_favoriteItemModel->loading())
    return;

  page = qBound(1, page, 2000);
  pageSize = qBound(1, pageSize, 20);

  m_currentFavoriteId = mediaId;
  m_favoritePage = page;

  if (page == 1) {
    m_favoriteItemModel->clear();
  }
  m_favoriteItemModel->setLoading(true);
  m_favoriteItemModel->setErrorMessage("");
  setIsLoading(true);

  QMap<QString, QString> params;
  params["media_id"] = QString::number(mediaId);
  params["pn"] = QString::number(page);
  params["ps"] = QString::number(pageSize);
  params["order"] = "mtime";
  params["type"] = "0";

  QPointer<BiliController> self(this);

  m_network->get(
      "/fav/resource/list", params,
      [self](const QJsonObject &data) {
        if (!self)
          return;

        QJsonArray list = data.value("medias").toArray();
        QVector<VideoItem> items;
        items.reserve(list.size());
        for (const QJsonValue &v : list) {
          if (!v.isObject())
            continue;

          QJsonObject obj = v.toObject();
          VideoItem item;
          item.bvid = obj.value("bvid").toString();
          item.title = obj.value("title").toString();
          item.pic = obj.value("cover").toString();
          item.duration = obj.value("duration").toInt();

          QJsonObject upper = obj.value("upper").toObject();
          item.ownerName = upper.value("name").toString();
          item.ownerMid = upper.value("mid").toVariant().toLongLong();
          item.ownerFace = upper.value("face").toString();

          QJsonObject cntInfo = obj.value("cnt_info").toObject();
          item.views = cntInfo.value("play").toVariant().toLongLong();
          if (item.views <= 0) {
            item.views = cntInfo.value("view").toVariant().toLongLong();
          }
          item.danmaku = cntInfo.value("danmaku").toVariant().toLongLong();
          item.likes = cntInfo.value("like").toVariant().toLongLong();
          item.favorites = cntInfo.value("favorite").toVariant().toLongLong();

          items.append(item);
        }

        self->m_favoriteItemModel->appendItems(items);
        bool hasMore = data.value("has_more").toBool(false);
        self->m_favoriteItemModel->setHasMore(hasMore ? true : !items.isEmpty());
        self->m_favoriteItemModel->setLoading(false);
        self->setIsLoading(false);

        if (items.isEmpty() && self->m_favoritePage == 1) {
          self->m_favoriteItemModel->setErrorMessage("收藏夹为空");
        }
      },
      [self](int, const QString &msg) {
        if (!self)
          return;

        self->m_favoriteItemModel->setLoading(false);
        self->m_favoriteItemModel->setErrorMessage(msg);
        self->setIsLoading(false);
        emit self->toastMessage(QString("收藏夹加载失败：%1").arg(msg));
      });
}

void BiliController::fetchMoreFavoriteItems() {
  if (!m_favoriteItemModel->hasMore() || m_favoriteItemModel->loading())
    return;
  if (m_favoriteItemModel->count() <= 0)
    return;
  m_favoritePage++;
  fetchFavoriteItems(m_currentFavoriteId, m_favoritePage);
}

// ====== API: 登录 ======

void BiliController::generateQrcode() {
  setIsLoading(true);

  QPointer<BiliController> self(this);

  m_network->get(
      "/qrcode/generate", {},
      [self](const QJsonObject &data) {
        if (!self)
          return;

        self->m_qrcodeUrl = data.value("qrcode").toString();
        self->m_qrcodeKey = data.value("qrcode_key").toString();
        emit self->qrcodeChanged();
        self->setIsLoading(false);
      },
      [self](int code, const QString &msg) {
        Q_UNUSED(code)
        if (!self)
          return;

        self->setIsLoading(false);
        emit self->toastMessage(QString("获取二维码失败：%1").arg(msg));
      });
}

void BiliController::pollQrcode() {
  if (m_qrcodeKey.isEmpty()) {
    emit toastMessage("请先获取二维码");
    return;
  }

  QMap<QString, QString> params;
  params["qrcode_key"] = m_qrcodeKey;

  QPointer<BiliController> self(this);

  m_network->get(
      "/qrcode/poll", params,
      [self](const QJsonObject &data) {
        if (!self)
          return;

        // 从 data 字段中获取 code 和 SESSDATA
        QJsonObject dataObj = data.value("data").toObject();
        if (dataObj.isEmpty()) {
          dataObj = data; // 兼容旧格式
        }

        int code = dataObj.value("code").toInt(-1);
        qDebug() << "[BiliController] pollQrcode response, code=" << code;

        switch (code) {
        case 0: {
          qDebug() << "[BiliController] QR login confirmed";
          // 登录状态交由服务端 Cookie 判断
          self->checkLoginStatus();
          break;
        }
        case 86038:
          qDebug() << "[BiliController] QR code expired";
          emit self->qrcodeNeedRefresh();
          emit self->toastMessage("二维码已过期，请刷新");
          break;
        case 86090:
          // 等待确认
          break;
        case 86101:
          // 未扫描
          break;
        default:
          qDebug() << "[BiliController] Unknown login code:" << code;
          emit self->toastMessage("登录状态未知");
          break;
        }
      },
      [self](int code, const QString &msg) {
        Q_UNUSED(code)
        if (!self)
          return;
        qDebug() << "[BiliController] pollQrcode error:" << msg;
        emit self->toastMessage(QString("轮询失败：%1").arg(msg));
      });
}

void BiliController::checkLoginStatus() {
  QPointer<BiliController> self(this);

  m_network->get(
      "/login/info", {},
      [self](const QJsonObject &data) {
        if (!self)
          return;

        qDebug() << "[BiliController] checkLoginStatus response:"
                 << QJsonDocument(data).toJson();

        self->m_loggedIn = true;
        // nav API 返回的字段名是 name 而不是 uname
        self->m_userName = data.value("name").toString();
        if (self->m_userName.isEmpty()) {
          self->m_userName = data.value("uname").toString();
        }
        self->m_userFace = data.value("face").toString();
        self->m_userId = data.value("mid").toVariant().toLongLong();

        // 等级信息
        QJsonObject levelInfo = data.value("level_info").toObject();
        self->m_userLevel = levelInfo.value("current_level").toInt(0);

        // 硬币数
        self->m_userCoins = data.value("money").toDouble(0);

        // VIP 信息
        self->m_userIsVip = data.value("vipStatus").toInt(0) == 1;
        QJsonObject vipLabel = data.value("vip_label").toObject();
        self->m_userVipLabel = vipLabel.value("text").toString();

        // 立即发射登录成功信号，不等待 fetchUserInfo
        // 这样即使 fetchUserInfo 因风控失败，页面也能正常返回
        emit self->qrcodeLoginSuccess();
        emit self->toastMessage("登录成功！");

        // 登录成功后刷新首页推荐流
        self->fetchPopular(1, 10);

        // 获取用户空间详细信息（粉丝数、关注数等）
        // 这个失败不影响登录流程
        self->fetchUserInfo(self->m_userId);
      },
      [self](int code, const QString &msg) {
        if (!self)
          return;

        qDebug() << "[BiliController] checkLoginStatus error, code:" << code
                 << "msg:" << msg;

        // 只有 401/-101 等认证错误才表示未登录
        if (code == 401 || code == -101 || code == -401) {
          qDebug() << "[BiliController] Session expired (code=" << code << ")";
          self->logout();
          emit self->toastMessage("登录已过期，请重新登录");
        } else {
          // 网络错误等其他失败保持现有登录状态，让用户无感知
          qDebug() << "[BiliController] checkLoginStatus failed (network), "
                      "keeping session:"
                   << msg;
          emit self->qrcodeLoginSuccess();
          emit self->toastMessage("登录成功！");
        }
      });
}

void BiliController::fetchUserInfo(qint64 mid) {
  if (mid <= 0)
    return;

  QPointer<BiliController> self(this);
  QMap<QString, QString> params;
  params["mid"] = QString::number(mid);

  m_network->get(
      "/user/info", params,
      [self](const QJsonObject &data) {
        if (!self)
          return;

        // 从 data 字段中获取用户信息（兼容嵌套格式）
        QJsonObject dataObj = data.value("data").toObject();
        if (dataObj.isEmpty()) {
          dataObj = data; // 兼容旧格式
        }

        qDebug() << "[BiliController] fetchUserInfo response:"
                 << QJsonDocument(dataObj).toJson();

        self->m_userFans = dataObj.value("fans").toInt(0);
        self->m_userFollowing = dataObj.value("following").toInt(0);
        self->m_userSign = dataObj.value("sign").toString();

        // 更新登录状态信号
        emit self->loginStateChanged();
        qDebug() << "[BiliController] User info updated:"
                 << "fans=" << self->m_userFans
                 << "following=" << self->m_userFollowing;
      },
      [self](int code, const QString &msg) {
        Q_UNUSED(code)
        if (!self)
          return;
        // 用户信息获取失败不影响登录状态
        // 不发射 qrcodeLoginSuccess，因为 checkLoginStatus 成功后已经发射过了
        qDebug() << "[BiliController] fetchUserInfo failed:" << msg;
      });
}

void BiliController::logout() {
  std::cout << "[BiliCtrl] Logout" << std::endl;
  m_loggedIn = false;
  m_userName = "";
  m_userFace = "";
  m_userId = 0;
  m_userLevel = 0;
  m_userCoins = 0;
  m_userFans = 0;
  m_userFollowing = 0;
  m_userSign = "";
  m_userVipLabel = "";
  m_userIsVip = false;
  emit loginStateChanged();
  emit toastMessage("已退出登录");
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

void BiliController::playVideoPart(int index) {
    if (!m_videoPartModel || index < 0 || index >= m_videoPartModel->count()) {
        return;
    }

    QModelIndex modelIndex = m_videoPartModel->index(index, 0);
    qint64 newCid = m_videoPartModel->data(modelIndex, VideoPartListModel::CidRole).toLongLong();

    if (newCid > 0 && m_currentVideo.cid != newCid) {
        m_currentVideo.cid = newCid;
        emit videoDetailChanged(); // 更新cid
        emit toastMessage(QString("切换到 P%1").arg(index + 1));
        // 这里可以根据需求决定是否立即播放
        // downloadAndPlay(m_playQuality); 
        // 或者只获取URL
        fetchPlayUrl(m_playQuality);
    }
}

// ====== 插件入口 ======

#include <QDir>
#include <QProcess>
#include <QThread>
#include <QtQml>
#include <signal.h>

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
    QStringList pids = output.split('\n', QString::SkipEmptyParts);

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
static bool startApiServer() {
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
  s_apiServerProcess->start(execPath);

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
static void stopApiServer() {
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

void init_plugin() {
  std::cout << "BiliPlugin: Initializing..." << std::endl;

  qmlRegisterType<BiliController>("BiliPlugin", 1, 0, "BiliController");
  qmlRegisterType<VideoListModel>("BiliPlugin", 1, 0, "VideoListModel");
  qmlRegisterType<CommentListModel>("BiliPlugin", 1, 0, "CommentListModel");
  qmlRegisterType<HotSearchModel>("BiliPlugin", 1, 0, "HotSearchModel");
  qmlRegisterType<SearchResultModel>("BiliPlugin", 1, 0, "SearchResultModel");
  qmlRegisterType<VideoPartListModel>("BiliPlugin", 1, 0, "VideoPartListModel");
  qmlRegisterType<FavoriteFolderModel>("BiliPlugin", 1, 0, "FavoriteFolderModel");

  // 启动 API 服务器
  if (!startApiServer()) {
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
  stopApiServer();

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

// ====== 下载到指定目录 ======

void BiliController::downloadVideoToDisk(int quality) {
  if (m_currentVideo.bvid.isEmpty() || m_currentVideo.cid == 0) {
    emit toastMessage("视频信息不完整，无法下载");
    return;
  }

  if (m_isDownloading) {
    emit toastMessage("已有下载任务进行中");
    return;
  }

  quality = qBound(16, quality, 127);
  
  // 1. 定义存储目录并确保它存在
  QString downloadDir = "/userdisk/Music/bili";
  QDir dir(downloadDir);
  if (!dir.exists()) {
      if (!dir.mkpath(".")) {
          emit toastMessage("创建下载目录失败");
          return;
      }
  }

  // 2. 构建并净化文件名
  QString title = videoTitle();

  // 多P视频：合集前10字符 + ... + 当前P标题
  if (m_videoPartModel && m_videoPartModel->count() > 1) {
      QString partTitle;
      for (int i = 0; i < m_videoPartModel->count(); ++i) {
          QModelIndex idx = m_videoPartModel->index(i, 0);
          qint64 cid = m_videoPartModel->data(idx, VideoPartListModel::CidRole).toLongLong();
          if (cid == m_currentVideo.cid) {
              partTitle = m_videoPartModel->data(idx, VideoPartListModel::PartRole).toString();
              break;
          }
      }
      if (!partTitle.isEmpty()) {
          QString collectionTitle = videoTitle().left(10);
          title = collectionTitle + "..." + partTitle;
      }
  }

  // 移除Windows和Linux文件名中的非法字符
  title.remove(QRegExp("[/\\\\:*?\"<>|]"));
  title = title.trimmed().left(100); // 限制文件名长度
  if (title.isEmpty()) {
      title = m_currentVideo.bvid; // 如果标题为空，使用bvid作为备用
  }
  QString targetPath = dir.filePath(title + ".mp4");

  // 3. 检查文件是否已存在
  if (QFile::exists(targetPath)) {
      emit toastMessage("文件已存在");
      return;
  }

  setIsLoading(true);

  QMap<QString, QString> params;
  params["aid"] = QString::number(videoAid());
  params["cid"] = QString::number(m_currentVideo.cid);
  params["qn"] = QString::number(quality);
  params["bvid"] = m_currentVideo.bvid;
  params["fnval"] = "1"; // 请求MP4格式

  QPointer<BiliController> self(this);

  m_network->get(
      "/video/playurl", params,
      [self, targetPath](const QJsonObject &data) {
        if (!self) return;

        // 解析可用清晰度
        QVector<int> newAccepts;
        QJsonArray accept = data.value("accept_quality").toArray();
        for (const QJsonValue &v : accept) {
          int q = v.toInt(0);
          if (q > 0) newAccepts.append(q);
        }
        if (!newAccepts.isEmpty() && newAccepts != self->m_acceptQualities) {
          self->m_acceptQualities = newAccepts;
          emit self->acceptQualitiesChanged();
        }

        QString videoUrl;
        QJsonArray durl = data.value("durl").toArray();
        if (!durl.isEmpty()) {
          QJsonObject first = durl.first().toObject();
          videoUrl = first.value("url").toString();
        }

        if (videoUrl.isEmpty()) {
          emit self->toastMessage("未获取到下载地址");
          self->setIsLoading(false);
          return;
        }

        // 开始下载
        self->m_isDownloading = true;
        self->m_downloadProgress = 0;
        self->m_downloadStatus = "准备下载...";
        emit self->downloadStateChanged();
        emit self->toastMessage("开始下载...");

        self->m_network->downloadVideo(
            videoUrl, targetPath,
            // 成功回调
            [self](const QString &path) {
              if (!self) return;
              self->m_isDownloading = false;
              self->m_downloadProgress = 1.0;
              self->m_downloadStatus = "下载完成";
              emit self->downloadStateChanged();
              self->setIsLoading(false);
              emit self->toastMessage("下载完成: " + path);
            },
            // 错误回调
            [self](int, const QString &msg) {
              if (!self) return;
              self->m_isDownloading = false;
              self->m_downloadProgress = 0;
              self->m_downloadStatus = "下载失败";
              emit self->downloadStateChanged();
              self->setIsLoading(false);
              emit self->toastMessage("下载失败: " + msg);
            },
            // 进度回调
            [self](qint64 received, qint64 total) {
              if (!self) return;
              double progress = total > 0 ? static_cast<double>(received) / total : 0;
              self->m_downloadProgress = progress;
              
              double mb = received / (1024.0 * 1024.0);
              double totalMb = total / (1024.0 * 1024.0);
              QString sizeStr = QString("%1MB / %2MB").arg(mb, 0, 'f', 1).arg(totalMb, 0, 'f', 1);
              
              self->m_downloadStatus = QString("下载中 %1 (%2%)").arg(sizeStr).arg(static_cast<int>(progress * 100));
              emit self->downloadStateChanged();
            });
      },
      [self](int, const QString &msg) {
        if (!self) return;
        self->setIsLoading(false);
        emit self->toastMessage("获取下载地址失败: " + msg);
      });
}
