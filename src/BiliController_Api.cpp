#include "BiliController.h"
#include "BiliModels.h"
#include "BiliNetwork.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QProcess>
#include <QRegExp>
#include <QStringListModel>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTimer>
#include <iostream>
#include <algorithm>

// 由插件文件提供的 Go 服务控制函数
extern bool bili_startApiServer();
extern void bili_stopApiServer();

// ====== 内部辅助方法 ======

void BiliController::apiGet(const QString &path,
                            const QMap<QString, QString> &params,
                            std::function<void(const QJsonObject &)> onSuccess,
                            std::function<void(int, const QString &)> onError,
                            bool withLoading) {
  if (withLoading) {
    setIsLoading(true);
  }

  QPointer<BiliController> self(this);
  m_network->get(
      path, params,
      [self, onSuccess, withLoading](const QJsonObject &data) {
        if (!self)
          return;
        if (withLoading) {
          self->setIsLoading(false);
        }
        if (onSuccess) {
          onSuccess(data);
        }
      },
      [self, onError, withLoading](int code, const QString &msg) {
        if (!self)
          return;
        if (withLoading) {
          self->setIsLoading(false);
        }
        if (onError) {
          onError(code, msg);
        }
      });
}

void BiliController::updateAcceptQualities(const QJsonObject &data) {
  QVector<int> newAccepts;
  QJsonArray accept = data.value("accept_quality").toArray();
  for (const QJsonValue &v : accept) {
    int q = v.toInt(0);
    if (q > 0)
      newAccepts.append(q);
  }
  // 兼容 support_formats（仅保留可观看清晰度，作为补充）
  QJsonArray supportFormats = data.value("support_formats").toArray();
  for (const QJsonValue &v : supportFormats) {
    QJsonObject obj = v.toObject();
    int q = obj.value("quality").toInt(0);
    int canWatch = obj.value("can_watch_qn_reason").toInt(0);
    int limit = obj.value("limit_watch_reason").toInt(0);
    if (q > 0 && canWatch == 0 && limit == 0 && !newAccepts.contains(q)) {
      newAccepts.append(q);
    }
  }

  if (!newAccepts.isEmpty() && newAccepts != m_acceptQualities) {
    m_acceptQualities = newAccepts;
    emit acceptQualitiesChanged();
  }
}

BiliController::DashResult
BiliController::pickDashUrls(const QJsonObject &data, int requestedQuality) const {
  DashResult result;
  result.finalQuality = requestedQuality;

  QJsonObject dash = data.value("dash").toObject();
  if (dash.isEmpty()) {
    return result;
  }

  QJsonArray videoArray = dash.value("video").toArray();
  QJsonArray audioArray = dash.value("audio").toArray();

  auto pickVideoByQn = [&](int qn, bool preferAvc) -> bool {
    for (const QJsonValue &v : videoArray) {
      QJsonObject videoObj = v.toObject();
      if (videoObj.value("id").toInt(0) == qn) {
        QString codecs = videoObj.value("codecs").toString();
        if (preferAvc && !codecs.startsWith("avc")) {
          continue;
        }
        QString url = videoObj.value("base_url").toString();
        if (url.isEmpty())
          url = videoObj.value("baseUrl").toString();
        if (!url.isEmpty()) {
          result.videoUrl = url;
          result.finalQuality = qn;
          return true;
        }
      }
    }
    return false;
  };

  // 优先用户选择的清晰度（优先 AVC）
  if (!pickVideoByQn(requestedQuality, true)) {
    pickVideoByQn(requestedQuality, false);
  }

  if (result.videoUrl.isEmpty()) {
    QVector<int> qualityOrder = {16, 32, 64, 80, 112, 116, 120, 125};
    for (int qn : qualityOrder) {
      if (qn == requestedQuality)
        continue;
      if (pickVideoByQn(qn, true))
        break;
      if (pickVideoByQn(qn, false))
        break;
    }
  }

  if (!audioArray.isEmpty()) {
    QVector<int> audioOrder = {30280, 30232, 30216};
    for (int audioQn : audioOrder) {
      for (const QJsonValue &a : audioArray) {
        QJsonObject audioObj = a.toObject();
        if (audioObj.value("id").toInt(0) == audioQn) {
          QString url = audioObj.value("base_url").toString();
          if (url.isEmpty())
            url = audioObj.value("baseUrl").toString();
          if (!url.isEmpty()) {
            result.audioUrl = url;
            break;
          }
        }
      }
      if (!result.audioUrl.isEmpty())
        break;
    }
  }

  return result;
}

QString BiliController::pickMp4Url(const QJsonObject &data) const {
  QJsonArray durl = data.value("durl").toArray();
  if (durl.isEmpty())
    return "";

  QJsonObject first = durl.first().toObject();
  QString url = first.value("url").toString();
  if (url.isEmpty()) {
    QJsonArray backup = first.value("backup_url").toArray();
    if (!backup.isEmpty()) {
      url = backup.first().toString();
    }
  }
  return url;
}

void BiliController::startDownloadTask(const QString &videoUrl,
                                       const QString &audioUrl,
                                       const QString &videoPath,
                                       const QString &audioPath,
                                       int finalQuality, bool playAfter,
                                       const QString &successToastPrefix,
                                       const QString &errorToastPrefix) {
  if (videoUrl.isEmpty() || videoPath.isEmpty()) {
    emit toastMessage("未获取到下载地址");
    setIsLoading(false);
    return;
  }

  m_isDownloading = true;
  m_downloadProgress = 0;
  m_downloadStatus = "正在下载视频...";
  emit downloadStateChanged();

  QPointer<BiliController> self(this);

  m_network->downloadVideo(
      videoUrl, videoPath,
      [self, audioUrl, audioPath, videoPath, finalQuality, playAfter, successToastPrefix](const QString &path) {
        if (!self)
          return;

        if (!audioUrl.isEmpty() && !audioPath.isEmpty()) {
          self->m_downloadStatus = "正在下载音频...";
          emit self->downloadStateChanged();

          self->m_network->downloadVideo(
              audioUrl, audioPath,
              [self, videoPath, finalQuality, playAfter, successToastPrefix](const QString &) {
                if (!self)
                  return;

                self->m_isDownloading = false;
                self->m_downloadProgress = 1.0;
                self->m_downloadStatus = "下载完成";
                if (playAfter) {
                  self->m_playUrl = videoPath;
                  self->m_playQuality = finalQuality;
                  emit self->playUrlChanged();
                }
                emit self->downloadStateChanged();
                self->setIsLoading(false);
                if (!successToastPrefix.isEmpty()) {
                  emit self->toastMessage(successToastPrefix + videoPath);
                }
              },
              [self](int, const QString &msg) {
                if (!self)
                  return;
                self->m_isDownloading = false;
                self->m_downloadProgress = 0;
                self->m_downloadStatus.clear();
                emit self->downloadStateChanged();
                self->setIsLoading(false);
                emit self->toastMessage(QString("音频下载失败：%1").arg(msg));
              });
          return;
        }

        self->m_isDownloading = false;
        self->m_downloadProgress = 1.0;
        self->m_downloadStatus = "下载完成";
        if (playAfter) {
          self->m_playUrl = path;
          self->m_playQuality = finalQuality;
          emit self->playUrlChanged();
        }
        emit self->downloadStateChanged();
        self->setIsLoading(false);
        if (!successToastPrefix.isEmpty()) {
          emit self->toastMessage(successToastPrefix + path);
        }
      },
      [self, errorToastPrefix](int, const QString &msg) {
        if (!self)
          return;

        self->m_isDownloading = false;
        self->m_downloadProgress = 0;
        self->m_downloadStatus.clear();
        emit self->downloadStateChanged();
        self->setIsLoading(false);
        emit self->toastMessage(errorToastPrefix + msg);
      },
      [self](qint64 received, qint64 total) {
        if (!self)
          return;

        double progress = total > 0 ? static_cast<double>(received) / total : 0;
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

  QString apiPath = "/recommend";
  std::cout << "[BiliCtrl] fetchPopular use " << apiPath.toStdString() << std::endl;

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
    if (!self)
      return;

    if (code == -101 || code == -401 || code == 401) {
      self->clearLocalLoginState();
    }

    self->m_popularModel->setLoading(false);
    self->m_popularModel->setErrorMessage(msg);
    self->setIsLoading(false);
    emit self->toastMessage(QString("加载失败：%1").arg(msg));
  };

  m_network->get(apiPath, paramsRecommend, onSuccess, onErrorFinal);
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

void BiliController::reportCurrentVideoAsRecentViewIfNeeded() {
  if (!m_loggedIn)
    return;
  if (m_currentVideo.aid <= 0 || m_currentVideo.cid <= 0 || m_currentVideo.bvid.isEmpty())
    return;

  QString reportKey = m_currentVideo.bvid + "#" + QString::number(m_currentVideo.cid);
  if (m_lastRecentViewReportKey == reportKey)
    return;
  m_lastRecentViewReportKey = reportKey;

  QMap<QString, QString> params;
  params["aid"] = QString::number(m_currentVideo.aid);
  params["cid"] = QString::number(m_currentVideo.cid);
  params["bvid"] = m_currentVideo.bvid;
  params["played_time"] = "1";

  QPointer<BiliController> self(this);
  m_network->get(
      "/player/heartbeat", params,
      [self](const QJsonObject &) {
        if (!self)
          return;
      },
      [self, reportKey](int, const QString &) {
        if (!self)
          return;
        // 失败时允许后续刷新/重进详情再次尝试。
        if (self->m_lastRecentViewReportKey == reportKey) {
          self->m_lastRecentViewReportKey.clear();
        }
      });
}

void BiliController::fetchVideoDetail(const QString &bvid) {
  if (bvid.isEmpty()) {
    emit toastMessage("视频 ID 为空");
    return;
  }
  if (m_videoDetailLoadingBvid == bvid) {
    return;
  }

  bool sameVideoRefresh = (m_currentVideo.bvid == bvid && !bvid.isEmpty());
  // 同视频刷新时保存当前选中的 cid，避免 refreshDetail 解析后被重置到第一页
  const qint64 prevCid = sameVideoRefresh ? m_currentVideo.cid : 0;

  // 仅在切换到新视频时重置收藏/投币/点赞/字幕状态，避免同视频刷新闪动
  if (!sameVideoRefresh) {
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

    // 切换视频时重置字幕选择与列表
    if (m_selectedSubtitleId != 0 || !m_selectedSubtitleLabel.isEmpty()) {
      m_selectedSubtitleId = 0;
      m_selectedSubtitleLabel.clear();
      emit selectedSubtitleChanged();
    }
    if (!m_subtitleItems.isEmpty()) {
      m_subtitleItems = QJsonArray();
      emit subtitleListChanged();
    }
  }

  // BV号格式校验
  if (!bvid.startsWith("BV") || bvid.length() < 10) {
    emit toastMessage("无效的视频 ID");
    return;
  }

  setIsLoading(true);
  m_videoDetailLoadingBvid = bvid;

  QMap<QString, QString> params;
  params["bvid"] = bvid;

  QPointer<BiliController> self(this);

  m_network->get(
      "/video/info", params,
      [self, sameVideoRefresh, prevCid](const QJsonObject &data) {
        if (!self)
          return;

        self->m_videoDetailLoadingBvid.clear();
        self->m_currentVideo = VideoListModel::parseVideoItem(data);
        self->m_currentVideo.aid = data.value("aid").toVariant().toLongLong();

        QJsonArray pages = data.value("pages").toArray();

        // 解析分P列表（先更新模型，后决定 cid）
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

        // 决定当前 cid：
        // - 同视频刷新：优先保留 prevCid（若它存在于 pages 中）
        // - 否则：若解析不到 cid，则退回到第一页 cid
        if (!pages.isEmpty()) {
          if (sameVideoRefresh && prevCid > 0) {
            bool found = false;
            for (const QJsonValue &v : pages) {
              QJsonObject p = v.toObject();
              qint64 cid = p.value("cid").toVariant().toLongLong();
              if (cid > 0 && cid == prevCid) {
                found = true;
                break;
              }
            }
            if (found) {
              self->m_currentVideo.cid = prevCid;
            }
          }

          if (self->m_currentVideo.cid == 0) {
            QJsonObject firstPage = pages.first().toObject();
            self->m_currentVideo.cid = firstPage.value("cid").toVariant().toLongLong();
          }
        }

        emit self->videoDetailChanged();
        self->reportCurrentVideoAsRecentViewIfNeeded();
        self->setIsLoading(false);
      },
      [self](int code, const QString &msg) {
        Q_UNUSED(code)
        if (!self)
          return;

        self->m_videoDetailLoadingBvid.clear();
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
  const QString requestKey = QString("%1:%2:%3:%4")
                                 .arg(m_currentVideo.bvid)
                                 .arg(m_currentVideo.cid)
                                 .arg(quality)
                                 .arg(4048);
  if (m_playUrlLoadingKey == requestKey) {
    return;
  }
  setIsLoading(true);
  m_playUrlLoadingKey = requestKey;

  QMap<QString, QString> params;
  params["aid"] = QString::number(videoAid());
  params["cid"] = QString::number(m_currentVideo.cid);
  params["qn"] = QString::number(quality);
  params["bvid"] = m_currentVideo.bvid;
  // fnval=1: 优先请求 MP4 格式，fnval=16: DASH 格式（音视频分离）
  // 优先使用 MP4 格式以获得更好的兼容性
  params["fnval"] = "4048";

  QPointer<BiliController> self(this);

  m_network->get(
      "/video/playurl", params,
      [self, quality](const QJsonObject &data) {
        if (!self)
          return;

        self->m_playUrlLoadingKey.clear();
        QString videoUrl;
        QString audioUrl;
        int requestedQuality = quality;
        int finalQuality = requestedQuality;
        int apiQuality = data.value("quality").toInt(0);
        if (apiQuality > 0) {
          finalQuality = apiQuality;
        }

        self->updateAcceptQualities(data);

        videoUrl = self->pickMp4Url(data);
        if (videoUrl.isEmpty()) {
          auto dash = self->pickDashUrls(data, requestedQuality);
          videoUrl = dash.videoUrl;
          audioUrl = dash.audioUrl;
          finalQuality = dash.finalQuality;
        }

        // 保存 DASH 直链，供外部播放器流式播放
        self->m_dashVideoUrl = videoUrl;
        self->m_dashAudioUrl = audioUrl;

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

        self->m_playUrlLoadingKey.clear();
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
  const QString requestKey = QString("%1:%2:%3:%4")
                                 .arg(m_currentVideo.bvid)
                                 .arg(m_currentVideo.cid)
                                 .arg(quality)
                                 .arg(4048);
  if (m_acceptQualitiesLoadingKey == requestKey) {
    return;
  }
  setIsLoading(true);
  m_acceptQualitiesLoadingKey = requestKey;

  QMap<QString, QString> params;
  params["aid"] = QString::number(videoAid());
  params["cid"] = QString::number(m_currentVideo.cid);
  params["qn"] = QString::number(quality);
  params["bvid"] = m_currentVideo.bvid;
  params["fnval"] = "4048";

  QPointer<BiliController> self(this);

  m_network->get(
      "/video/playurl", params,
      [self](const QJsonObject &data) {
        if (!self)
          return;

        self->m_acceptQualitiesLoadingKey.clear();
        self->updateAcceptQualities(data);
        self->setIsLoading(false);
      },
      [self](int, const QString &msg) {
        if (!self)
          return;
        self->m_acceptQualitiesLoadingKey.clear();
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
  QString baseName = QString("bili_%1_%2")
                         .arg(m_currentVideo.bvid)
                         .arg(QDateTime::currentMSecsSinceEpoch());
  m_tempVideoPath = QDir(tempDir).filePath(baseName + ".m4s");
  m_tempAudioPath = QDir(tempDir).filePath(baseName + "_audio.m4s");

  QMap<QString, QString> params;
  params["aid"] = QString::number(videoAid());
  params["cid"] = QString::number(m_currentVideo.cid);
  params["qn"] = QString::number(quality);
  params["bvid"] = m_currentVideo.bvid;
  params["fnval"] = "1"; // MP4 合并流

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

        self->updateAcceptQualities(data);

        auto dash = self->pickDashUrls(data, requestedQuality);
        videoUrl = dash.videoUrl;
        audioUrl = dash.audioUrl;
        finalQuality = dash.finalQuality;

        // 回退到 MP4（durl）
        if (videoUrl.isEmpty()) {
          videoUrl = self->pickMp4Url(data);
        }

        if (videoUrl.isEmpty()) {
          emit self->toastMessage("未获取到播放地址");
          self->setIsLoading(false);
          return;
        }

        self->startDownloadTask(videoUrl, audioUrl, self->m_tempVideoPath,
                                self->m_tempAudioPath, finalQuality, true);
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
  if (!m_tempAudioPath.isEmpty()) {
    QFile file(m_tempAudioPath);
    if (file.exists()) {
      file.remove();
      std::cout << "[BiliController] Cleaned up temp audio: "
                << m_tempAudioPath.toStdString() << std::endl;
    }
    m_tempAudioPath.clear();
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
  params["sort"] = "2";
  params["pn"] = QString::number(page);
  params["ps"] = "10";

  apiGet(
      "/video/comments", params,
      [this](const QJsonObject &data) {

        QJsonObject pageObj = data.value("page").toObject();
        int total = pageObj.value("count").toInt();
        m_commentModel->setTotalCount(total);

        QJsonArray replies = data.value("replies").toArray();

        QVector<CommentItem> items;

        // 置顶评论（可能存在 upper/admin/vote 等）
        if (m_commentPage == 1) {
          QJsonObject topObj = data.value("top").toObject();
          QStringList topKeys = {"upper", "admin", "vote"};
          for (const QString &key : topKeys) {
            QJsonObject topItem = topObj.value(key).toObject();
            if (!topItem.isEmpty()) {
              CommentItem topComment = CommentListModel::parseCommentItem(topItem);
              topComment.isTop = true;
              items.append(topComment);
            }
          }
        }

        items.reserve(items.size() + replies.size());
        for (const QJsonValue &v : replies) {
          if (v.isObject()) {
            items.append(CommentListModel::parseCommentItem(v.toObject()));
          }
        }

        m_commentModel->appendItems(items);
        m_commentModel->setLoading(false);

        if (items.isEmpty() && m_commentPage == 1) {
          m_commentModel->setErrorMessage("暂无评论");
        }
      },
      [this](int code, const QString &msg) {
        m_commentModel->setLoading(false);
        if (code == -404 || msg == "啥都木有") {
          m_commentModel->setErrorMessage("暂无评论");
        } else {
          m_commentModel->setErrorMessage(msg);
          emit toastMessage(QString("评论加载失败：%1").arg(msg));
        }
      });
}

void BiliController::fetchCommentReplies(qint64 rootRpid) {
  if (m_currentVideo.aid <= 0 || rootRpid <= 0) {
    emit toastMessage("评论信息不完整");
    return;
  }
  if (m_commentReplyModel->loading())
    return;

  m_currentCommentRootRpid = rootRpid;
  m_commentReplyPage = 1;
  m_commentReplyHasMore = false;
  emit replyHasMoreChanged();

  m_commentReplyModel->clear();
  m_commentReplyModel->setLoading(true);

  QMap<QString, QString> params;
  params["oid"] = QString::number(m_currentVideo.aid);
  params["type"] = "1";
  params["root"] = QString::number(rootRpid);
  params["ps"] = "20";
  params["pn"] = QString::number(m_commentReplyPage);

  apiGet(
      "/video/comments/replies", params,
      [this](const QJsonObject &data) {

        QJsonArray replies = data.value("replies").toArray();
        QVector<CommentReplyItem> items;
        items.reserve(replies.size());
        for (const QJsonValue &v : replies) {
          if (v.isObject()) {
            items.append(CommentReplyListModel::parseCommentReplyItem(v.toObject()));
          }
        }

        QJsonObject pageObj = data.value("page").toObject();
        int count = pageObj.value("count").toInt(0);
        int num = pageObj.value("num").toInt(m_commentReplyPage);
        int size = pageObj.value("size").toInt(20);
        bool hasMore = (count > 0) ? (num * size < count) : (items.size() >= size);

        m_commentReplyHasMore = hasMore;
        emit replyHasMoreChanged();

        m_commentReplyModel->setItems(items);
        m_commentReplyModel->setLoading(false);
        if (items.isEmpty()) {
          m_commentReplyModel->setErrorMessage("暂无回复");
        }
      },
      [this](int, const QString &msg) {
        m_commentReplyHasMore = false;
        emit replyHasMoreChanged();
        m_commentReplyModel->setLoading(false);
        m_commentReplyModel->setErrorMessage(msg);
        emit toastMessage(QString("回复加载失败：%1").arg(msg));
      });
}

void BiliController::fetchMoreCommentReplies() {
  if (m_currentCommentRootRpid <= 0) return;
  if (!m_commentReplyHasMore) return;
  if (m_commentReplyModel->loading()) return;

  m_commentReplyPage++;
  m_commentReplyModel->setLoading(true);

  QMap<QString, QString> params;
  params["oid"] = QString::number(m_currentVideo.aid);
  params["type"] = "1";
  params["root"] = QString::number(m_currentCommentRootRpid);
  params["ps"] = "20";
  params["pn"] = QString::number(m_commentReplyPage);

  apiGet(
      "/video/comments/replies", params,
      [this](const QJsonObject &data) {

        QJsonArray replies = data.value("replies").toArray();
        QVector<CommentReplyItem> items;
        items.reserve(replies.size());
        for (const QJsonValue &v : replies) {
          if (v.isObject()) {
            items.append(CommentReplyListModel::parseCommentReplyItem(v.toObject()));
          }
        }

        QJsonObject pageObj = data.value("page").toObject();
        int count = pageObj.value("count").toInt(0);
        int num = pageObj.value("num").toInt(m_commentReplyPage);
        int size = pageObj.value("size").toInt(20);
        bool hasMore = (count > 0) ? (num * size < count) : (items.size() >= size);

        m_commentReplyHasMore = hasMore;
        emit replyHasMoreChanged();

        m_commentReplyModel->appendItems(items);
        m_commentReplyModel->setLoading(false);
      },
      [this](int, const QString &msg) {
        m_commentReplyModel->setLoading(false);
        emit toastMessage(QString("回复加载失败：%1").arg(msg));
      },
      true);
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

  QMap<QString, QString> params;
  params["mid"] = QString::number(m_userId);

  apiGet(
      "/fav/folder/list", params,
      [this](const QJsonObject &data) {

        QJsonArray list = data.value("list").toArray();
        QVector<FavoriteFolderItem> items;
        items.reserve(list.size());
        for (const QJsonValue &v : list) {
          if (v.isObject()) {
            items.append(FavoriteFolderModel::parseFavoriteFolderItem(v.toObject()));
          }
        }

        m_favoriteFolderModel->setItems(items);
        m_favoriteFolderModel->setLoading(false);

        // 更新封面为每个收藏夹的首个视频封面
        for (const FavoriteFolderItem &folder : items) {
          QMap<QString, QString> p;
          qint64 mediaId = folder.id > 0 ? folder.id : folder.fid;
          if (mediaId <= 0) continue;
          p["media_id"] = QString::number(mediaId);
          p["pn"] = "1";
          p["ps"] = "1";
          p["order"] = "mtime";
          p["type"] = "0";

          QPointer<BiliController> self2(this);
          m_network->get(
              "/fav/resource/list", p,
              [self2, mediaId](const QJsonObject &data2) {
                if (!self2) return;
                QJsonArray medias = data2.value("medias").toArray();
                if (!medias.isEmpty()) {
                  QJsonObject first = medias.first().toObject();
                  QString cover = first.value("cover").toString();
                  if (!cover.isEmpty()) {
                    self2->m_favoriteFolderModel->updateCover(mediaId, cover);
                  }
                }
              },
              nullptr);
        }

        if (items.isEmpty()) {
          emit toastMessage("暂无收藏夹");
        }
      },
      [this](int, const QString &msg) {
        m_favoriteFolderModel->setLoading(false);
        emit toastMessage(QString("收藏夹加载失败：%1").arg(msg));
      },
      true);
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

  QMap<QString, QString> params;
  params["media_id"] = QString::number(mediaId);
  params["pn"] = QString::number(page);
  params["ps"] = QString::number(pageSize);
  params["order"] = "mtime";
  params["type"] = "0";

  apiGet(
      "/fav/resource/list", params,
      [this](const QJsonObject &data) {

        QJsonArray list = data.value("medias").toArray();
        QVector<VideoItem> items;
        items.reserve(list.size());
        for (const QJsonValue &v : list) {
          if (!v.isObject())
            continue;

          QJsonObject obj = v.toObject();
          VideoItem item = VideoListModel::parseVideoItem(obj);
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

        m_favoriteItemModel->appendItems(items);
        bool hasMore = data.value("has_more").toBool(false);
        m_favoriteItemModel->setHasMore(hasMore ? true : !items.isEmpty());
        m_favoriteItemModel->setLoading(false);

        if (items.isEmpty() && m_favoritePage == 1) {
          m_favoriteItemModel->setErrorMessage("收藏夹为空");
        }
      },
      [this](int, const QString &msg) {
        m_favoriteItemModel->setLoading(false);
        m_favoriteItemModel->setErrorMessage(msg);
        emit toastMessage(QString("收藏夹加载失败：%1").arg(msg));
      },
      true);
}

void BiliController::fetchMoreFavoriteItems() {
  if (!m_favoriteItemModel->hasMore() || m_favoriteItemModel->loading())
    return;
  if (m_favoriteItemModel->count() <= 0)
    return;
  m_favoritePage++;
  fetchFavoriteItems(m_currentFavoriteId, m_favoritePage);
}

// ====== API: 收藏状态 ======

void BiliController::fetchFavoriteStatus() {
  if (!m_loggedIn) {
    return;
  }
  if (m_currentVideo.aid <= 0) {
    return;
  }
  const qint64 aid = m_currentVideo.aid;
  if (m_favoriteStatusLoadingAid == aid) {
    return;
  }
  m_favoriteStatusLoadingAid = aid;

  QMap<QString, QString> params;
  params["aid"] = QString::number(aid);

  apiGet(
      "/fav/status", params,
      [this](const QJsonObject &data) {
        m_favoriteStatusLoadingAid = 0;

        bool fav = false;
        if (data.value("favoured").isBool()) {
          fav = data.value("favoured").toBool(false);
        } else {
          fav = data.value("favoured").toInt(0) == 1;
        }
        if (m_isFavorited != fav) {
          m_isFavorited = fav;
          emit favoriteStatusChanged();
        }
      },
      [this](int, const QString &msg) {
        m_favoriteStatusLoadingAid = 0;
        emit toastMessage(QString("获取收藏状态失败：%1").arg(msg));
      });
}

void BiliController::fetchCoinStatus() {
  if (!m_loggedIn) {
    return;
  }
  if (m_currentVideo.aid <= 0) {
    return;
  }
  const qint64 aid = m_currentVideo.aid;
  if (m_coinStatusLoadingAid == aid) {
    return;
  }
  m_coinStatusLoadingAid = aid;

  QMap<QString, QString> params;
  params["aid"] = QString::number(aid);

  apiGet(
      "/coin/status", params,
      [this](const QJsonObject &data) {
        m_coinStatusLoadingAid = 0;

        bool coined = false;
        int multiply = data.value("multiply").toInt(0);
        if (multiply > 0) {
          coined = true;
        } else if (data.value("multiply").isString()) {
          coined = data.value("multiply").toString().toInt() > 0;
        }
        if (m_isCoined != coined) {
          m_isCoined = coined;
          emit coinStatusChanged();
        }
      },
      [this](int, const QString &msg) {
        m_coinStatusLoadingAid = 0;
        emit toastMessage(QString("获取投币状态失败：%1").arg(msg));
      });
}

void BiliController::addCoin(int multiply, bool selectLike) {
  if (!m_loggedIn) {
    emit toastMessage("请先登录后再投币");
    return;
  }
  if (m_currentVideo.aid <= 0) {
    emit toastMessage("视频信息不完整");
    return;
  }
  if (m_isCoined) {
    emit toastMessage("已经投过币了");
    return;
  }

  multiply = qBound(1, multiply, 2);

  QMap<QString, QString> params;
  params["aid"] = QString::number(m_currentVideo.aid);
  params["bvid"] = m_currentVideo.bvid;
  params["multiply"] = QString::number(multiply);
  params["select_like"] = selectLike ? "1" : "0";

  QPointer<BiliController> self(this);
  m_network->get(
      "/coin/add", params,
      [self, multiply, selectLike](const QJsonObject &) {
        if (!self)
          return;
        self->m_isCoined = true;
        if (selectLike) {
          self->m_isLiked = true;
          emit self->likeStatusChanged();
        }
        emit self->coinStatusChanged();
        emit self->toastMessage(QString("投币成功（%1个）").arg(multiply));
        self->fetchVideoDetail(self->m_currentVideo.bvid);
        self->fetchCoinStatus();
        self->fetchLikeStatus();
      },
      [self](int, const QString &msg) {
        if (!self)
          return;
        emit self->toastMessage(QString("投币失败：%1").arg(msg));
      });
}

void BiliController::fetchLikeStatus() {
  if (!m_loggedIn) {
    return;
  }
  if (m_currentVideo.aid <= 0) {
    return;
  }
  const qint64 aid = m_currentVideo.aid;
  if (m_likeStatusLoadingAid == aid) {
    return;
  }
  m_likeStatusLoadingAid = aid;

  QMap<QString, QString> params;
  params["aid"] = QString::number(aid);

  apiGet(
      "/like/status", params,
      [this](const QJsonObject &data) {
        m_likeStatusLoadingAid = 0;

        int liked = 0;
        if (data.value("liked").isBool()) {
          liked = data.value("liked").toBool() ? 1 : 0;
        } else if (data.value("liked").isString()) {
          liked = data.value("liked").toString().toInt();
        } else {
          liked = data.value("liked").toInt(0);
        }
        bool isLiked = liked == 1;
        if (m_isLiked != isLiked) {
          m_isLiked = isLiked;
          emit likeStatusChanged();
        }
      },
      [this](int, const QString &msg) {
        m_likeStatusLoadingAid = 0;
        emit toastMessage(QString("获取点赞状态失败：%1").arg(msg));
      });
}

void BiliController::fetchWatchLaterStatus() {
  if (!m_loggedIn) {
    return;
  }
  if (m_currentVideo.aid <= 0) {
    return;
  }
  const qint64 aid = m_currentVideo.aid;
  if (m_watchLaterStatusLoadingAid == aid) {
    return;
  }
  m_watchLaterStatusLoadingAid = aid;

  QMap<QString, QString> params;
  params["pn"] = "1";
  params["ps"] = "100";

  apiGet(
      "/toview/list", params,
      [this](const QJsonObject &data) {
        m_watchLaterStatusLoadingAid = 0;
        QJsonArray list = data.value("list").toArray();
        if (list.isEmpty()) {
          list = data.value("data").toArray();
        }

        bool found = false;
        for (const QJsonValue &v : list) {
          if (!v.isObject())
            continue;
          QJsonObject obj = v.toObject();
          qint64 aid = obj.value("aid").toVariant().toLongLong();
          QString bvid = obj.value("bvid").toString();
          if ((aid > 0 && aid == m_currentVideo.aid) || (!bvid.isEmpty() && bvid == m_currentVideo.bvid)) {
            found = true;
            break;
          }
        }

        if (m_isWatchLater != found) {
          m_isWatchLater = found;
          emit watchLaterStatusChanged();
        }
      },
      [this](int, const QString &msg) {
        m_watchLaterStatusLoadingAid = 0;
        emit toastMessage(QString("获取稍后再看状态失败：%1").arg(msg));
      });
}

void BiliController::toggleLike() {
  if (!m_loggedIn) {
    emit toastMessage("请先登录后再点赞");
    return;
  }
  if (m_currentVideo.aid <= 0) {
    emit toastMessage("视频信息不完整");
    return;
  }

  int likeAction = m_isLiked ? 2 : 1; // 1=点赞,2=取消

  QMap<QString, QString> params;
  params["aid"] = QString::number(m_currentVideo.aid);
  params["bvid"] = m_currentVideo.bvid;
  params["like"] = QString::number(likeAction);

  QPointer<BiliController> self(this);
  m_network->get(
      "/like/toggle", params,
      [self, likeAction](const QJsonObject &) {
        if (!self)
          return;
        bool newLiked = (likeAction == 1);
        self->m_isLiked = newLiked;
        if (newLiked) {
          emit self->toastMessage("点赞爆棚，感谢推荐！");
        } else {
          emit self->toastMessage("已取消点赞");
        }
        emit self->likeStatusChanged();
        self->fetchVideoDetail(self->m_currentVideo.bvid);
        self->fetchLikeStatus();
      },
      [self](int, const QString &msg) {
        if (!self)
          return;
        emit self->toastMessage(QString("点赞操作失败：%1").arg(msg));
      });
}

void BiliController::toggleFavorite() {
  if (!m_loggedIn) {
    emit toastMessage("请先登录后再收藏");
    return;
  }
  if (m_currentVideo.aid <= 0) {
    emit toastMessage("视频信息不完整");
    return;
  }

  if (m_isFavorited) {
    QMap<QString, QString> params;
    params["aid"] = QString::number(m_currentVideo.aid);
    params["action"] = "0";

    QPointer<BiliController> self(this);
    m_network->get(
        "/fav/toggle", params,
        [self](const QJsonObject &) {
          if (!self)
            return;
          self->m_isFavorited = false;
          emit self->favoriteStatusChanged();
          emit self->toastMessage("已取消收藏");
        },
        [self](int, const QString &msg) {
          if (!self)
            return;
          emit self->toastMessage(QString("取消收藏失败：%1").arg(msg));
        });
    return;
  }

  fetchFavoriteFolders();
  emit toastMessage("请选择收藏夹");
}

void BiliController::toggleFavoriteTo(qint64 mediaId) {
  if (!m_loggedIn) {
    emit toastMessage("请先登录后再收藏");
    return;
  }
  if (m_currentVideo.aid <= 0) {
    emit toastMessage("视频信息不完整");
    return;
  }
  if (mediaId <= 0) {
    emit toastMessage("收藏夹无效");
    return;
  }

  QMap<QString, QString> params;
  params["aid"] = QString::number(m_currentVideo.aid);
  params["action"] = "1";
  params["media_id"] = QString::number(mediaId);

  QPointer<BiliController> self(this);

  m_network->get(
      "/fav/toggle", params,
      [self](const QJsonObject &) {
        if (!self)
          return;

        self->m_isFavorited = true;
        emit self->favoriteStatusChanged();
        emit self->toastMessage("已收藏到收藏夹");
      },
      [self](int, const QString &msg) {
        if (!self)
          return;
        emit self->toastMessage(QString("收藏操作失败：%1").arg(msg));
      });
}

void BiliController::toggleWatchLater() {
  if (!m_loggedIn) {
    emit toastMessage("请先登录后再添加稍后再看");
    return;
  }
  if (m_currentVideo.aid <= 0) {
    emit toastMessage("视频信息不完整");
    return;
  }

  const QString apiPath = m_isWatchLater ? "/toview/del" : "/toview/add";
  const QString okMsg = m_isWatchLater ? "已从稍后再看移除" : "已添加到稍后再看";

  QMap<QString, QString> params;
  params["aid"] = QString::number(m_currentVideo.aid);
  // 删除接口只支持 aid，添加接口支持 aid 和 bvid
  if (!m_isWatchLater) {
    params["bvid"] = m_currentVideo.bvid;
  }

  QPointer<BiliController> self(this);
  m_network->get(
      apiPath, params,
      [self, okMsg](const QJsonObject &) {
        if (!self)
          return;
        self->m_isWatchLater = !self->m_isWatchLater;
        emit self->watchLaterStatusChanged();
        emit self->toastMessage(okMsg);
        self->fetchWatchLater(1, 20);
      },
      [self](int, const QString &msg) {
        if (!self)
          return;
        emit self->toastMessage(QString("稍后再看操作失败：%1").arg(msg));
      });
}

bool BiliController::isExternalPlayerRunning() const {
  if (m_externalPlayerProcess && m_externalPlayerProcess->state() != QProcess::NotRunning) {
    return true;
  }

  QProcess pgrep;
  pgrep.start("pgrep", QStringList() << "-f" << "/userdisk/mpv/bin/mpv");
  if (!pgrep.waitForFinished(1500)) {
    return false;
  }

  return pgrep.exitCode() == 0 && !QString::fromLocal8Bit(pgrep.readAllStandardOutput()).trimmed().isEmpty();
}

QString BiliController::externalPlayerTitle() const {
  QString title = videoTitle().trimmed();
  if (title.isEmpty()) {
    title = m_currentVideo.bvid.trimmed();
  }
  return title;
}

bool BiliController::startExternalPlayer(const QStringList &args) {
  const QString player = "/userdisk/VideoPlayer";
  if (!QFile::exists(player)) {
    emit toastMessage("外部播放器不存在");
    return false;
  }

  if (isExternalPlayerRunning()) {
    emit toastMessage("播放器已在运行，请先关闭当前窗口");
    return false;
  }

  auto *process = new QProcess(this);
  process->setProgram(player);
  process->setArguments(args);

  connect(process, &QProcess::errorOccurred, this,
          [this, process](QProcess::ProcessError error) {
            if (process != m_externalPlayerProcess) {
              process->deleteLater();
              return;
            }

            QString detail = process->errorString();
            if (detail.isEmpty()) {
              detail = QString::number(static_cast<int>(error));
            }
            emit toastMessage(QString("启动外部播放器失败：%1").arg(detail));
            m_externalPlayerProcess = nullptr;
            process->deleteLater();
          });

  connect(process,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [this, process](int, QProcess::ExitStatus) {
            if (process == m_externalPlayerProcess) {
              m_externalPlayerProcess = nullptr;
            }
            process->deleteLater();
          });

  process->start();
  if (!process->waitForStarted(3000)) {
    QString detail = process->errorString();
    const QString stderrText = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
    if (!stderrText.isEmpty()) {
      detail = detail.isEmpty() ? stderrText : detail + " | " + stderrText;
    }
    emit toastMessage(QString("启动外部播放器失败：%1").arg(detail.isEmpty() ? QStringLiteral("未知错误") : detail));
    process->deleteLater();
    return false;
  }

  m_externalPlayerProcess = process;
  return true;
}

void BiliController::launchExternalPlayer(const QString &path) {
  if (path.isEmpty()) {
    emit toastMessage("播放路径为空");
    return;
  }

  QString filePath = path;
  if (filePath.startsWith("file://")) {
    filePath = filePath.mid(7);
  }

  QStringList args;
  args << ("--force-media-title=" + externalPlayerTitle()) << filePath;
  startExternalPlayer(args);
}

void BiliController::launchExternalPlayerWithAudio(const QString &videoPath, const QString &audioPath) {
  if (videoPath.isEmpty() || audioPath.isEmpty()) {
    emit toastMessage("播放路径不完整");
    return;
  }

  QString v = videoPath;
  QString a = audioPath;
  if (v.startsWith("file://")) v = v.mid(7);
  if (a.startsWith("file://")) a = a.mid(7);

  QStringList args;
  args << ("--force-media-title=" + externalPlayerTitle())
       << v << ("--audio-file=" + a);
  startExternalPlayer(args);
}

void BiliController::launchExternalPlayerWithAudioUrl(const QString &videoUrl, const QString &audioUrl) {
  if (videoUrl.isEmpty() || audioUrl.isEmpty()) {
    emit toastMessage("播放地址不完整");
    return;
  }

  QStringList args;
  args << ("--force-media-title=" + externalPlayerTitle())
       << videoUrl << ("--audio-file=" + audioUrl)
       << "--referrer=https://www.bilibili.com"
       << "--user-agent=Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
       << ("--script-opts=bili-aid=" + QString::number(videoAid())
           + ",bili-cid=" + QString::number(m_currentVideo.cid)
           + ",bili-bvid=" + m_currentVideo.bvid);

  startExternalPlayer(args);
}

void BiliController::launchExternalPlayerWithAudioUrlAndSubtitle(const QString &videoUrl, const QString &audioUrl, const QString &subtitlePath) {
  if (videoUrl.isEmpty() || audioUrl.isEmpty()) {
    emit toastMessage("播放地址不完整");
    return;
  }

  QString sub = subtitlePath;
  if (sub.startsWith("file://")) {
    sub = sub.mid(7);
  }

  QStringList args;
  args << ("--force-media-title=" + externalPlayerTitle())
       << videoUrl << ("--audio-file=" + audioUrl);
  if (!sub.isEmpty()) {
    args << ("--sub-file=" + sub);
  }
  args << "--referrer=https://www.bilibili.com"
       << "--user-agent=Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
       << ("--script-opts=bili-aid=" + QString::number(videoAid())
           + ",bili-cid=" + QString::number(m_currentVideo.cid)
           + ",bili-bvid=" + m_currentVideo.bvid);

  qDebug() << "[BiliController] launchExternalPlayerWithAudioUrlAndSubtitle"
           << "sub=" << sub;

  startExternalPlayer(args);
}

void BiliController::fetchSubtitleList() {
  if (m_currentVideo.aid <= 0 || m_currentVideo.cid <= 0) {
    emit toastMessage("视频信息不完整，无法获取字幕");
    return;
  }

  QMap<QString, QString> params;
  params["aid"] = QString::number(m_currentVideo.aid);
  params["cid"] = QString::number(m_currentVideo.cid);
  params["bvid"] = m_currentVideo.bvid;

  QPointer<BiliController> self(this);
  m_network->get(
      "/video/subtitle/list", params,
      [self](const QJsonObject &data) {
        if (!self)
          return;
        self->m_subtitleItems = data.value("subtitles").toArray();
        emit self->subtitleListChanged();
      },
      [self](int, const QString &msg) {
        if (!self)
          return;
        self->m_subtitleItems = QJsonArray();
        emit self->subtitleListChanged();
        emit self->toastMessage(QString("获取字幕列表失败：%1").arg(msg));
      });
}

void BiliController::selectSubtitle(qint64 subtitleId, const QString &label) {
  if (m_selectedSubtitleId == subtitleId && m_selectedSubtitleLabel == label) {
    return;
  }
  m_selectedSubtitleId = subtitleId;
  m_selectedSubtitleLabel = label;
  emit selectedSubtitleChanged();
}

void BiliController::clearSelectedSubtitle() {
  if (m_selectedSubtitleId == 0 && m_selectedSubtitleLabel.isEmpty()) {
    return;
  }
  m_selectedSubtitleId = 0;
  m_selectedSubtitleLabel.clear();
  emit selectedSubtitleChanged();
}

void BiliController::setSubtitleFontSize(int value) {
  value = qBound(6, value, 40);
  if (m_subtitleFontSize == value) return;
  m_subtitleFontSize = value;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("subtitleFontSize", m_subtitleFontSize);
  settings.sync();
  emit subtitleStyleChanged();
}

void BiliController::setSubtitleMarginV(int value) {
  value = qBound(0, value, 30);
  if (m_subtitleMarginV == value) return;
  m_subtitleMarginV = value;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("subtitleMarginV", m_subtitleMarginV);
  settings.sync();
  emit subtitleStyleChanged();
}

void BiliController::setSubtitleSpacing(double value) {
  if (value < 0) value = 0;
  if (value > 6.0) value = 6.0;
  if (qFuzzyCompare(m_subtitleSpacing, value)) return;
  m_subtitleSpacing = value;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("subtitleSpacing", m_subtitleSpacing);
  settings.sync();
  emit subtitleStyleChanged();
}

void BiliController::setSubtitleWeight(int value) {
  value = qBound(100, value, 900);
  if (m_subtitleWeight == value) return;
  m_subtitleWeight = value;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("subtitleWeight", m_subtitleWeight);
  settings.sync();
  emit subtitleStyleChanged();
}

void BiliController::launchExternalPlayerCurrentSelection() {
  if (m_dashVideoUrl.isEmpty() || m_dashAudioUrl.isEmpty()) {
    emit toastMessage("播放地址尚未准备好");
    return;
  }

  if (m_selectedSubtitleId <= 0) {
    launchExternalPlayerWithAudioUrl(m_dashVideoUrl, m_dashAudioUrl);
    return;
  }

  QUrl subtitleUrl(m_network->apiBase() + "/video/subtitle/ass/file");
  QUrlQuery subtitleQuery;
  subtitleQuery.addQueryItem("aid", QString::number(m_currentVideo.aid));
  subtitleQuery.addQueryItem("cid", QString::number(m_currentVideo.cid));
  subtitleQuery.addQueryItem("bvid", m_currentVideo.bvid);
  subtitleQuery.addQueryItem("sid", QString::number(m_selectedSubtitleId));
  subtitleQuery.addQueryItem("font_size", QString::number(m_subtitleFontSize));
  subtitleQuery.addQueryItem("margin_v", QString::number(m_subtitleMarginV));
  subtitleQuery.addQueryItem("spacing", QString::number(m_subtitleSpacing, 'f', 2));
  subtitleQuery.addQueryItem("weight", QString::number(m_subtitleWeight));
  subtitleUrl.setQuery(subtitleQuery);

  launchExternalPlayerWithAudioUrlAndSubtitle(
      m_dashVideoUrl, m_dashAudioUrl, subtitleUrl.toString());
}

// ====== API: 登录 ======

void BiliController::generateQrcode() {
  // 用户触发登录时：除了拉取二维码，也在后台启动短信登录服务并开始轮询
  // 这样用户可以在浏览器完成短信登录（bili-sms）后，本插件自动接管 cookies
  startSmsLogin();

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

  // 若短信登录已经成功导入并触发 checkLoginStatus，这里避免继续轮询造成冲突
  if (m_loggedIn) {
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

// ====== 短信登录（bili-sms + /pull） ======

static const int SMS_LOGIN_PORT = 8666;
static const char *SMS_PULL_PATH = "/pull";

void BiliController::startSmsLogin() {
  if (m_loggedIn) return;
  if (m_smsPolling) return;

  if (!m_smsNam) {
    m_smsNam = new QNetworkAccessManager(this);
  }

  // 启动当前目录下的 bili-sms（二进制）
  QStringList candidates;
  candidates << QDir::current().filePath("bili-sms");
  candidates << QCoreApplication::applicationDirPath() + "/bili-sms";
  candidates << QStringLiteral("/userdisk/PenMods/plugins/bili_plugin/bili-sms");

  QString execPath;
  for (const QString &c : candidates) {
    if (QFile::exists(c)) { execPath = c; break; }
  }

  if (!execPath.isEmpty()) {
    QFile f(execPath);
    if (!(f.permissions() & QFile::ExeUser)) {
      f.setPermissions(f.permissions() | QFile::ExeUser | QFile::ExeGroup | QFile::ExeOther);
    }

    bool ok = QProcess::startDetached(execPath, QStringList(), QFileInfo(execPath).absolutePath());
    if (!ok) {
      m_smsLastError = QString("启动 bili-sms 失败：%1").arg(execPath);
      emit toastMessage(m_smsLastError);
    }
  } else {
    m_smsLastError = "未找到 bili-sms 可执行文件（将继续尝试轮询 8666/pull）";
    qDebug() << "[BiliController]" << m_smsLastError;
  }

  if (!m_smsPollTimer) {
    m_smsPollTimer = new QTimer(this);
    m_smsPollTimer->setInterval(2000);
    m_smsPollTimer->setSingleShot(false);
    connect(m_smsPollTimer, &QTimer::timeout, this, &BiliController::pollSmsLogin);
  }

  m_smsPolling = true;
  m_smsImporting = false;
  m_smsPollTimer->start();
}

void BiliController::stopSmsLogin() {
  m_smsPolling = false;
  m_smsImporting = false;
  if (m_smsPollTimer) m_smsPollTimer->stop();
}

void BiliController::pollSmsLogin() {
  if (!m_smsPolling || m_loggedIn) return;
  if (m_smsImporting) return;
  if (!m_smsNam) m_smsNam = new QNetworkAccessManager(this);

  QUrl url(QString("http://127.0.0.1:%1%2").arg(SMS_LOGIN_PORT).arg(SMS_PULL_PATH));
  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

  QNetworkReply *reply = m_smsNam->get(req);
  if (!reply) return;

  QPointer<BiliController> self(this);
  connect(reply, &QNetworkReply::finished, this, [self, reply]() {
    if (!self) { if (reply) reply->deleteLater(); return; }

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    if (httpStatus == 404) {
      return; // no account
    }
    if (httpStatus < 200 || httpStatus >= 300) {
      self->m_smsLastError = QString("短信登录服务异常：HTTP %1").arg(httpStatus);
      qDebug() << "[BiliController]" << self->m_smsLastError;
      return;
    }

    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
      self->m_smsLastError = "短信登录 /pull 返回非JSON";
      return;
    }

    QJsonObject obj = doc.object();
    QString refreshToken = obj.value("refresh_token").toString();
    QJsonObject cookies = obj.value("cookies").toObject();
    QString sessdata = cookies.value("SESSDATA").toString();
    QString biliJct = cookies.value("bili_jct").toString();
    QString buvid3 = cookies.value("buvid3").toString();
    QString dedeUserID = cookies.value("DedeUserID").toString();
    QString dedeCkMd5 = cookies.value("DedeUserID__ckMd5").toString();

    if (sessdata.isEmpty()) {
      self->m_smsLastError = "短信登录 cookies 缺少 SESSDATA";
      return;
    }

    self->m_smsImporting = true;

    QUrl importUrl(QString("%1/login/import").arg(self->m_network->apiBase()));
    QNetworkRequest importReq(importUrl);
    importReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject payload;
    payload.insert("SESSDATA", sessdata);
    if (!biliJct.isEmpty()) payload.insert("bili_jct", biliJct);
    if (!buvid3.isEmpty()) payload.insert("buvid3", buvid3);
    if (!dedeUserID.isEmpty()) payload.insert("DedeUserID", dedeUserID);
    if (!dedeCkMd5.isEmpty()) payload.insert("DedeUserID__ckMd5", dedeCkMd5);
    if (!refreshToken.isEmpty()) payload.insert("refresh_token", refreshToken);

    QNetworkReply *importReply = self->m_smsNam->post(importReq, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    if (!importReply) { self->m_smsImporting = false; return; }

    connect(importReply, &QNetworkReply::finished, self, [self, importReply]() {
      if (!self) { if (importReply) importReply->deleteLater(); return; }

      const int st = importReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      importReply->deleteLater();

      self->m_smsImporting = false;

      if (st < 200 || st >= 300) {
        self->m_smsLastError = QString("导入短信登录 cookies 失败：HTTP %1").arg(st);
        return;
      }

      // 导入成功：停止轮询
      self->stopSmsLogin();

      // 结束 bili-sms 进程（按你的要求：pgrep -f bili-sms 然后 kill）
      {
        QProcess pgrep;
        pgrep.start("pgrep", QStringList() << "-f" << "bili-sms");
        if (pgrep.waitForFinished(1500) && pgrep.exitCode() == 0) {
          QString out = QString::fromLocal8Bit(pgrep.readAllStandardOutput()).trimmed();
          QStringList pids = out.split('\n', Qt::SkipEmptyParts);
          for (const QString &pid : pids) {
            // 使用 kill 命令避免引入平台差异
            QProcess::startDetached("kill", QStringList() << "-TERM" << pid);
          }
        }
      }

      self->checkLoginStatus();
    });
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

        qint64 mid = data.value("mid").toVariant().toLongLong();
        if (mid <= 0) {
          self->clearLocalLoginState();
          emit self->toastMessage("未登录，请先扫码登录");
          return;
        }

        self->m_loggedIn = true;
        // nav API 返回的字段名是 name 而不是 uname
        self->m_userName = data.value("name").toString();
        if (self->m_userName.isEmpty()) {
          self->m_userName = data.value("uname").toString();
        }
        self->m_userFace = data.value("face").toString();
        self->m_userId = mid;

        // 等级信息
        QJsonObject levelInfo = data.value("level_info").toObject();
        self->m_userLevel = levelInfo.value("current_level").toInt(0);
        self->m_userExp = levelInfo.value("current_exp").toInt(0);
        self->m_userExpMin = levelInfo.value("current_min").toInt(0);
        self->m_userExpNext = levelInfo.value("next_exp").toInt(0);

        // 硬币数
        self->m_userCoins = data.value("money").toDouble(0);

        // VIP 信息
        self->m_userIsVip = data.value("vipStatus").toInt(0) == 1;
        QJsonObject vipLabel = data.value("vip_label").toObject();
        self->m_userVipLabel = vipLabel.value("text").toString();

        emit self->loginStateChanged();

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
          self->clearLocalLoginState();
          emit self->toastMessage("登录已过期，请重新登录");
        } else {
          // 不再在网络错误时伪造登录成功，改为清空本地状态
          qDebug() << "[BiliController] checkLoginStatus failed (network), "
                      "clearing local session:"
                   << msg;
          self->clearLocalLoginState();
          emit self->toastMessage("无法验证登录状态，请重新登录");
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
      [self, mid](const QJsonObject &data) {
        if (!self)
          return;
        if (!self->m_loggedIn || self->m_userId != mid)
          return;

        // 从 data 字段中获取用户信息（兼容嵌套格式）
        QJsonObject dataObj = data.value("data").toObject();
        if (dataObj.isEmpty()) {
          dataObj = data; // 兼容旧格式
        }

        qDebug() << "[BiliController] fetchUserInfo response:"
                 << QJsonDocument(dataObj).toJson();

        auto toIntSafe = [](const QJsonValue &v) -> int {
          if (v.isDouble()) return v.toInt();
          if (v.isString()) return v.toString().toInt();
          return 0;
        };

        int fans = toIntSafe(dataObj.value("fans"));
        if (fans <= 0) fans = toIntSafe(dataObj.value("follower"));
        if (fans <= 0) {
          QJsonObject statObj = dataObj.value("stat").toObject();
          fans = toIntSafe(statObj.value("follower"));
        }

        int following = toIntSafe(dataObj.value("following"));
        if (following <= 0) {
          QJsonObject statObj = dataObj.value("stat").toObject();
          following = toIntSafe(statObj.value("following"));
        }

        self->m_userFans = fans;
        self->m_userFollowing = following;
        self->m_userSign = dataObj.value("sign").toString();

        // 更新登录状态信号
        emit self->loginStateChanged();
        qDebug() << "[BiliController] User info updated:"
                 << "fans=" << self->m_userFans
                 << "following=" << self->m_userFollowing;
      },
      [self](int code, const QString &msg) {
        if (!self)
          return;
        if (code == -101 || code == -401 || code == 401) {
          self->clearLocalLoginState();
          self->fetchPopular(1, 10);
          emit self->toastMessage("登录已过期，请重新登录");
          return;
        }
        // 用户信息获取失败不影响登录状态
        // 不发射 qrcodeLoginSuccess，因为 checkLoginStatus 成功后已经发射过了
        qDebug() << "[BiliController] fetchUserInfo failed:" << msg;
      });
}

void BiliController::logout() {
  std::cout << "[BiliCtrl] Logout" << std::endl;

  // 停止短信登录后台轮询与进程，避免退出后又被 /pull 拉起登录
  stopSmsLogin();

  if (m_network) {
    m_network->cancelAllRequests();
  }

  auto doLocalLogout = [this]() {
    clearLocalLoginState();
    emit toastMessage("已退出登录");
  };

  // 通知服务器清理登录态
  if (m_network) {
    m_network->get(
        "/logout", {},
        [doLocalLogout](const QJsonObject &) { doLocalLogout(); },
        [doLocalLogout](int, const QString &) { doLocalLogout(); });
  } else {
    doLocalLogout();
  }
}

void BiliController::fetchRecentHistory() {
    if (!m_loggedIn) {
        emit toastMessage("请先登录后查看最近观看");
        return;
    }

    if (m_recentHistoryModel->loading())
        return;

    m_recentHistoryMax = 0;
    m_recentHistoryViewAt = 0;
    m_recentHistoryModel->clear();
    m_recentHistoryModel->setLoading(true);
    setIsLoading(true);

    QMap<QString, QString> params;
    QPointer<BiliController> self(this);
    m_network->get(
        "/history/recent", params,
        [self](const QJsonObject &data) {
            if (!self) return;

            QJsonObject cursor = data.value("cursor").toObject();
            self->m_recentHistoryMax = cursor.value("max").toVariant().toInt();
            self->m_recentHistoryViewAt = cursor.value("view_at").toVariant().toInt();

            QJsonArray list = data.value("list").toArray();
            if (list.isEmpty()) {
                list = data.value("items").toArray();
            }

            QVector<VideoItem> items;
            items.reserve(list.size());
            for (const QJsonValue &v : list) {
                if (!v.isObject()) continue;
                QJsonObject obj = v.toObject();
                QJsonObject history = obj.value("history").toObject();

                VideoItem item = VideoListModel::parseVideoItem(obj);
                item.bvid = history.value("bvid").toString();
                item.aid = history.value("oid").toVariant().toLongLong();
                item.cid = history.value("cid").toVariant().toLongLong();
                item.title = obj.value("title").toString();
                item.pic = obj.value("cover").toString();
                item.duration = obj.value("duration").toInt();
                item.ownerName = obj.value("author_name").toString();
                if (item.ownerName.isEmpty()) {
                    item.ownerName = obj.value("name").toString();
                }
                item.views = 0;
                item.danmaku = 0;

                if (!item.bvid.isEmpty()) {
                    items.append(item);
                }
            }

            self->m_recentHistoryModel->appendItems(items);
            self->m_recentHistoryModel->setHasMore(!items.isEmpty());
            self->m_recentHistoryModel->setLoading(false);
            self->setIsLoading(false);
        },
        [self](int, const QString &msg) {
            if (!self) return;
            self->m_recentHistoryModel->setLoading(false);
            self->setIsLoading(false);
            emit self->toastMessage(QString("最近观看加载失败：%1").arg(msg));
        });
}

void BiliController::fetchMoreRecentHistory() {
    if (m_recentHistoryModel->loading() || !m_recentHistoryModel->hasMore())
        return;

    QMap<QString, QString> params;
    if (m_recentHistoryMax > 0) {
        params["max"] = QString::number(m_recentHistoryMax);
    }
    if (m_recentHistoryViewAt > 0) {
        params["view_at"] = QString::number(m_recentHistoryViewAt);
    }

    m_recentHistoryModel->setLoading(true);
    setIsLoading(true);

    QPointer<BiliController> self(this);
    m_network->get(
        "/history/recent", params,
        [self](const QJsonObject &data) {
            if (!self) return;

            QJsonObject cursor = data.value("cursor").toObject();
            self->m_recentHistoryMax = cursor.value("max").toVariant().toInt();
            self->m_recentHistoryViewAt = cursor.value("view_at").toVariant().toInt();

            QJsonArray list = data.value("list").toArray();
            if (list.isEmpty()) {
                list = data.value("items").toArray();
            }

            QVector<VideoItem> items;
            items.reserve(list.size());
            for (const QJsonValue &v : list) {
                if (!v.isObject()) continue;
                QJsonObject obj = v.toObject();
                QJsonObject history = obj.value("history").toObject();

                VideoItem item = VideoListModel::parseVideoItem(obj);
                item.bvid = history.value("bvid").toString();
                item.aid = history.value("oid").toVariant().toLongLong();
                item.cid = history.value("cid").toVariant().toLongLong();
                item.title = obj.value("title").toString();
                item.pic = obj.value("cover").toString();
                item.duration = obj.value("duration").toInt();
                item.ownerName = obj.value("author_name").toString();
                if (item.ownerName.isEmpty()) {
                    item.ownerName = obj.value("name").toString();
                }
                item.views = 0;
                item.danmaku = 0;

                if (!item.bvid.isEmpty()) {
                    items.append(item);
                }
            }

            self->m_recentHistoryModel->appendItems(items);
            self->m_recentHistoryModel->setHasMore(!items.isEmpty());
            self->m_recentHistoryModel->setLoading(false);
            self->setIsLoading(false);
        },
        [self](int, const QString &msg) {
            if (!self) return;
            self->m_recentHistoryModel->setLoading(false);
            self->setIsLoading(false);
            emit self->toastMessage(QString("最近观看加载失败：%1").arg(msg));
        });
}

// ====== 稍后再看 ======

void BiliController::fetchWatchLater(int page, int pageSize) {
  if (!m_loggedIn) {
    emit toastMessage("请先登录后查看稍后再看");
    return;
  }
  if (m_watchLaterModel->loading())
    return;

  page = qBound(1, page, 1000);
  pageSize = qBound(1, pageSize, 30);

  m_watchLaterPage = page;
  if (page == 1) {
    m_watchLaterModel->clear();
  }
  m_watchLaterModel->setLoading(true);
  m_watchLaterModel->setErrorMessage("");
  setIsLoading(true);

  QMap<QString, QString> params;
  params["pn"] = QString::number(page);
  params["ps"] = QString::number(pageSize);

  QPointer<BiliController> self(this);
  m_network->get(
      "/toview/list", params,
      [self, pageSize](const QJsonObject &data) {
        if (!self) return;

        QJsonArray list = data.value("list").toArray();
        if (list.isEmpty()) {
          list = data.value("data").toArray();
        }

        QVector<VideoItem> items;
        items.reserve(list.size());
        for (const QJsonValue &v : list) {
          if (!v.isObject())
            continue;
          QJsonObject obj = v.toObject();

          VideoItem item = VideoListModel::parseVideoItem(obj);
          item.aid = obj.value("aid").toVariant().toLongLong();
          item.pic = obj.value("pic").toString();
          if (item.pic.isEmpty()) {
            item.pic = obj.value("cover").toString();
          }
          item.duration = obj.value("duration").toInt();

          QJsonObject ownerObj = obj.value("owner").toObject();
          item.ownerName = ownerObj.value("name").toString();
          if (item.ownerName.isEmpty()) {
            item.ownerName = obj.value("author_name").toString();
          }
          if (item.ownerName.isEmpty()) {
            item.ownerName = obj.value("name").toString();
          }

          if (!item.bvid.isEmpty() || item.aid > 0) {
            items.append(item);
          }
        }

        bool hasMore = data.value("has_more").toBool(false);
        if (!data.contains("has_more")) {
          hasMore = items.size() >= pageSize;
        }

        self->m_watchLaterHasMore = hasMore;
        self->m_watchLaterModel->appendItems(items);
        self->m_watchLaterModel->setHasMore(hasMore);
        self->m_watchLaterModel->setLoading(false);
        self->setIsLoading(false);

        if (items.isEmpty() && self->m_watchLaterPage == 1) {
          self->m_watchLaterModel->setErrorMessage("暂无稍后再看");
        }
      },
      [self](int, const QString &msg) {
        if (!self) return;
        self->m_watchLaterModel->setLoading(false);
        self->setIsLoading(false);
        self->m_watchLaterModel->setErrorMessage(msg);
        emit self->toastMessage(QString("稍后再看加载失败：%1").arg(msg));
      });
}

void BiliController::fetchMoreWatchLater() {
  if (!m_watchLaterHasMore || m_watchLaterModel->loading())
    return;
  if (m_watchLaterModel->count() <= 0)
    return;
  m_watchLaterPage++;
  fetchWatchLater(m_watchLaterPage, 20);
}

void BiliController::addToWatchLater() {
  // 直接调用 toggleWatchLater，它会根据当前状态决定是添加还是删除
  toggleWatchLater();
}

// ====== UP 主主页 ======

void BiliController::fetchUpInfo(qint64 mid) {
  if (mid <= 0) {
    return;
  }

  if (m_upUserMid != mid) {
    m_upUserMid = mid;
    m_upVideoPage = 1;
    m_upVideoHasMore = true;
    m_upVideoCursorNext = 0;
    if (m_upVideoModel) {
      m_upVideoModel->clear();
      m_upVideoModel->setHasMore(true);
    }
    if (m_upIsFollowing) {
      m_upIsFollowing = false;
      emit upFollowChanged();
    }
  }

  QMap<QString, QString> params;
  params["mid"] = QString::number(mid);

  apiGet(
      "/user/info", params,
      [this, mid](const QJsonObject &data) {
        if (m_upUserMid != mid)
          return;

        const QJsonObject obj = data;

        m_upUserName = obj.value("name").toString();
        if (m_upUserName.isEmpty()) {
          m_upUserName = obj.value("uname").toString();
        }
        m_upUserFace = obj.value("face").toString();
        m_upUserSign = obj.value("sign").toString();

        int level = obj.value("level").toInt(0);
        if (level <= 0) {
          QJsonObject levelInfo = obj.value("level_info").toObject();
          level = levelInfo.value("current_level").toInt(0);
        }
        m_upUserLevel = level;

        auto toIntSafe = [](const QJsonValue &v) -> int {
          if (v.isDouble())
            return v.toInt();
          if (v.isString())
            return v.toString().toInt();
          return 0;
        };
        m_upUserFans = toIntSafe(obj.value("follower"));
        m_upUserFollowing = toIntSafe(obj.value("following"));

        bool newFollowState = obj.value("is_following").toBool(false)
                              || obj.value("is_followed").toBool(false);
        bool followStateChanged = (m_upIsFollowing != newFollowState);
        m_upIsFollowing = newFollowState;

        emit upUserChanged();
        if (followStateChanged) {
          emit upFollowChanged();
        }
      },
      [this](int, const QString &msg) {
        emit toastMessage(QString("获取UP主信息失败：%1").arg(msg));
      },
      true);
}

void BiliController::fetchUpVideos(qint64 mid, int page, int pageSize) {
  if (mid <= 0) {
    return;
  }
  if (m_upVideoModel->loading())
    return;

  page = qBound(1, page, 9999);
  pageSize = qBound(1, pageSize, 30);

  if (m_upUserMid != mid) {
    m_upUserMid = mid;
    m_upVideoPage = 1;
    m_upVideoHasMore = true;
    m_upVideoCursorNext = 0;
    m_upVideoModel->clear();
  }

  m_upVideoPage = page;
  if (page == 1) {
    m_upVideoCursorNext = 0;
    m_upVideoModel->clear();
  }
  m_upVideoModel->setLoading(true);
  m_upVideoModel->setErrorMessage("");

  QMap<QString, QString> params;
  params["mid"] = QString::number(mid);
  params["ps"] = QString::number(pageSize);

  // 优先使用游标翻页：当 page>1 且已有游标时，带 max 参数请求下一段
  // 这样可避免 UP 新增稿件导致 pn 翻页出现丢失/重复。
  if (page > 1 && m_upVideoCursorNext > 0) {
    params["pn"] = "1";
    params["max"] = QString::number(m_upVideoCursorNext);
  } else {
    params["pn"] = QString::number(page);
  }

  apiGet(
      "/user/videos", params,
      [this, page, pageSize](const QJsonObject &data) {
        QJsonArray list;
        if (data.value("list").isObject()) {
          QJsonObject listObj = data.value("list").toObject();
          list = listObj.value("vlist").toArray();
        }
        if (list.isEmpty()) {
          list = data.value("vlist").toArray();
        }
        if (list.isEmpty()) {
          list = data.value("item").toArray();
        }

        QVector<VideoItem> items;
        items.reserve(list.size());
        for (const QJsonValue &v : list) {
          if (v.isObject()) {
            QJsonObject obj = v.toObject();
            QJsonObject archiveObj = obj.value("archive").toObject();
            VideoItem item = archiveObj.isEmpty()
                                 ? VideoListModel::parseVideoItem(obj)
                                 : VideoListModel::parseVideoItem(archiveObj);

            // APP space/archive/cursor：常见字段
            // - aid 不一定在 archive 对象里，可能在 param 字段（字符串）
            // - 时间戳常用 ctime
            if (item.aid <= 0) {
              item.aid = obj.value("aid").toVariant().toLongLong();
            }
            if (item.aid <= 0) {
              item.aid = obj.value("param").toString().toLongLong();
            }

            if (item.pubdate <= 0) {
              item.pubdate = obj.value("pubdate").toVariant().toLongLong();
            }
            if (item.pubdate <= 0) {
              item.pubdate = obj.value("ctime").toVariant().toLongLong();
            }
            if (item.pubdate <= 0) {
              item.pubdate = obj.value("created").toVariant().toLongLong();
            }
            if (item.pubdate <= 0 && !archiveObj.isEmpty()) {
              item.pubdate = archiveObj.value("created").toVariant().toLongLong();
            }

            items.append(item);
          }
        }

        std::sort(items.begin(), items.end(),
                  [](const VideoItem &a, const VideoItem &b) {
                    return a.pubdate > b.pubdate;
                  });

        m_upVideoModel->appendItems(items);

        // 解析游标（APP cursor）
        // space/archive/cursor 在很多情况下不返回 data.cursor，而是要求客户端用“上一页最后一个 aid”继续翻页。
        qint64 nextCursor = 0;

        QJsonObject cursorObj = data.value("cursor").toObject();
        if (!cursorObj.isEmpty()) {
          nextCursor = cursorObj.value("next").toVariant().toLongLong();
          if (nextCursor <= 0) {
            nextCursor = cursorObj.value("max").toVariant().toLongLong();
          }
        }

        // 兜底：用最后一条的 aid（优先）/pubdate 作为游标
        if (nextCursor <= 0 && !items.isEmpty()) {
          if (items.last().aid > 0) {
            nextCursor = items.last().aid;
          } else {
            nextCursor = items.last().pubdate;
          }
        }

        if (nextCursor > 0) {
          m_upVideoCursorNext = nextCursor;
        }

        bool hasMore = !items.isEmpty();
        if (data.contains("has_next")) {
          hasMore = data.value("has_next").toBool(false);
        } else {
          // 兼容 web 分页
          QJsonObject pageObj = data.value("page").toObject();
          int count = pageObj.value("count").toInt(0);
          int num = pageObj.value("pn").toInt(page);
          int size = pageObj.value("ps").toInt(pageSize);
          if (count > 0 && size > 0) {
            hasMore = (num * size < count);
          } else {
            // 若是游标模式但缺失 has_next，则按 nextCursor 是否变化判断
            hasMore = (m_upVideoCursorNext > 0) && (items.size() >= pageSize);
          }
        }
        m_upVideoHasMore = hasMore;
        m_upVideoModel->setHasMore(hasMore);
        m_upVideoModel->setLoading(false);

        if (items.isEmpty() && m_upVideoPage == 1) {
          m_upVideoModel->setErrorMessage("暂无投稿");
        }
      },
      [this](int, const QString &msg) {
        m_upVideoModel->setLoading(false);
        m_upVideoModel->setErrorMessage(msg);
        emit toastMessage(QString("加载投稿失败：%1").arg(msg));
      },
      true);
}

void BiliController::fetchMoreUpVideos() {
  if (!m_upVideoHasMore || m_upVideoModel->loading())
    return;
  m_upVideoPage++;
  fetchUpVideos(m_upUserMid, m_upVideoPage);
}

void BiliController::toggleUpFollow() {
  if (!m_loggedIn) {
    emit toastMessage("请先登录后再关注");
    return;
  }
  if (m_upUserMid <= 0) {
    emit toastMessage("UP 主信息无效");
    return;
  }
  if (m_userId > 0 && m_userId == m_upUserMid) {
    emit toastMessage("不能关注自己");
    return;
  }
  if (m_upFollowLoading) {
    return;
  }

  const bool targetFollow = !m_upIsFollowing;
  m_upFollowLoading = true;

  QMap<QString, QString> params;
  params["mid"] = QString::number(m_upUserMid);
  params["action"] = targetFollow ? "1" : "0";

  QPointer<BiliController> self(this);
  apiGet(
      "/user/follow/toggle", params,
      [self, targetFollow](const QJsonObject &data) {
        if (!self)
          return;

        self->m_upFollowLoading = false;

        bool finalFollowState = targetFollow;
        if (data.contains("following")) {
          finalFollowState = data.value("following").toBool(targetFollow);
        }

        bool changed = (self->m_upIsFollowing != finalFollowState);
        self->m_upIsFollowing = finalFollowState;

        if (changed) {
          if (finalFollowState) {
            self->m_upUserFans += 1;
          } else if (self->m_upUserFans > 0) {
            self->m_upUserFans -= 1;
          }
          emit self->upUserChanged();
          emit self->upFollowChanged();
        } else {
          emit self->upFollowChanged();
        }

        emit self->toastMessage(finalFollowState ? "关注成功" : "已取消关注");
      },
      [self](int, const QString &msg) {
        if (!self)
          return;
        self->m_upFollowLoading = false;
        emit self->toastMessage(QString("关注操作失败：%1").arg(msg));
      },
      false);
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

void BiliController::restartGoServer() {
    emit toastMessage("正在重启 Go 服务端...");
    bili_stopApiServer();
    if (bili_startApiServer()) {
        emit toastMessage("Go 服务端已重启");
    } else {
        emit toastMessage("Go 服务端重启失败");
    }
}

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

  // 多P视频：选集标题前10字符 + ... + 当前P标题
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
      [self, targetPath, quality](const QJsonObject &data) {
        if (!self) return;

        self->updateAcceptQualities(data);

        QString videoUrl = self->pickMp4Url(data);

        if (videoUrl.isEmpty()) {
          emit self->toastMessage("未获取到下载地址");
          self->setIsLoading(false);
          return;
        }

        self->m_downloadStatus = "准备下载...";
        emit self->downloadStateChanged();
        emit self->toastMessage("开始下载...");

        self->startDownloadTask(videoUrl, QString(), targetPath, QString(),
                                quality, false,
                                QStringLiteral("下载完成: "),
                                QStringLiteral("下载失败: "));
      },
      [self](int, const QString &msg) {
        if (!self) return;
        self->setIsLoading(false);
        emit self->toastMessage("获取下载地址失败: " + msg);
      });
}
