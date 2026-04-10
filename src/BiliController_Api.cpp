#include "BiliController.h"
#include "BiliModels.h"
#include "BiliNetwork.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QProcess>
#include <QRegExp>
#include <QStringListModel>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <iostream>

// 由插件文件提供的 Go 服务控制函数
extern bool bili_startApiServer();
extern void bili_stopApiServer();

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

void BiliController::fetchVideoDetail(const QString &bvid) {
  if (bvid.isEmpty()) {
    emit toastMessage("视频 ID 为空");
    return;
  }

  bool sameVideoRefresh = (m_currentVideo.bvid == bvid && !bvid.isEmpty());

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
  params["fnval"] = "4048";

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

        // 解析可用清晰度（以 accept_quality 为准）
        QVector<int> newAccepts;
        QJsonArray accept = data.value("accept_quality").toArray();
        for (const QJsonValue &v : accept) {
          int q = v.toInt(0);
          if (q > 0) newAccepts.append(q);
        }
        // 兼容 support_formats（仅保留可观看清晰度，作为补充）
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
                    videoUrl = url;
                    finalQuality = qn;
                    return true;
                  }
                }
              }
              return false;
            };

            // 先尝试用户选择的清晰度（优先 AVC/H.264）
            if (!pickVideoByQn(requestedQuality, true)) {
              pickVideoByQn(requestedQuality, false);
            }

            // 回退到其他画质
            if (videoUrl.isEmpty()) {
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
  params["fnval"] = "4048";

  QPointer<BiliController> self(this);

  m_network->get(
      "/video/playurl", params,
      [self](const QJsonObject &data) {
        if (!self)
          return;

        // 解析可用清晰度（以 accept_quality 为准）
        QVector<int> newAccepts;
        QJsonArray accept = data.value("accept_quality").toArray();
        for (const QJsonValue &v : accept) {
          int q = v.toInt(0);
          if (q > 0) newAccepts.append(q);
        }
        // 兼容 support_formats（仅保留可观看清晰度，作为补充）
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

        // 解析可用清晰度（以 accept_quality 为准）
        QVector<int> newAccepts;
        QJsonArray accept = data.value("accept_quality").toArray();
        for (const QJsonValue &v : accept) {
          int q = v.toInt(0);
          if (q > 0) newAccepts.append(q);
        }
        // 兼容 support_formats（仅保留可观看清晰度，作为补充）
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

        // DASH：优先选择视频流与音频流
        QJsonObject dash = data.value("dash").toObject();
        if (!dash.isEmpty()) {
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

          // 先尝试用户选择的清晰度（优先 AVC/H.264）
          if (!pickVideoByQn(requestedQuality, true)) {
            pickVideoByQn(requestedQuality, false);
          }

          if (videoUrl.isEmpty()) {
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

          // 获取音频流（优先高码率）
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

        // 回退到 MP4（durl）
        if (videoUrl.isEmpty()) {
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
        }

        if (videoUrl.isEmpty()) {
          emit self->toastMessage("未获取到播放地址");
          self->setIsLoading(false);
          return;
        }

        // 开始下载视频
        self->m_isDownloading = true;
        self->m_downloadProgress = 0;
        self->m_downloadStatus = "正在下载视频...";
        emit self->downloadStateChanged();

        self->m_network->downloadVideo(
            videoUrl, self->m_tempVideoPath,
            // 视频成功回调
            [self, audioUrl, finalQuality](const QString &path) {
              if (!self)
                return;

              // 如果有音频流，继续下载音频
              if (!audioUrl.isEmpty()) {
                self->m_downloadStatus = "正在下载音频...";
                emit self->downloadStateChanged();

                self->m_network->downloadVideo(
                    audioUrl, self->m_tempAudioPath,
                    [self, finalQuality](const QString &) {
                      if (!self)
                        return;

                      self->m_isDownloading = false;
                      self->m_downloadProgress = 1.0;
                      self->m_downloadStatus = "下载完成";
                      self->m_playUrl = self->m_tempVideoPath;
                      self->m_playQuality = finalQuality;
                      emit self->downloadStateChanged();
                      emit self->playUrlChanged();
                      self->setIsLoading(false);
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
              } else {
                self->m_isDownloading = false;
                self->m_downloadProgress = 1.0;
                self->m_downloadStatus = "下载完成";
                self->m_playUrl = path;
                self->m_playQuality = finalQuality;
                emit self->downloadStateChanged();
                emit self->playUrlChanged();
                self->setIsLoading(false);
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
  setIsLoading(true);

  QMap<QString, QString> params;
  params["oid"] = QString::number(m_currentVideo.aid);
  params["type"] = "1";
  params["root"] = QString::number(rootRpid);
  params["ps"] = "20";
  params["pn"] = QString::number(m_commentReplyPage);

  QPointer<BiliController> self(this);
  m_network->get(
      "/video/comments/replies", params,
      [self](const QJsonObject &data) {
        if (!self)
          return;

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
        int num = pageObj.value("num").toInt(self->m_commentReplyPage);
        int size = pageObj.value("size").toInt(20);
        bool hasMore = (count > 0) ? (num * size < count) : (items.size() >= size);

        self->m_commentReplyHasMore = hasMore;
        emit self->replyHasMoreChanged();

        self->m_commentReplyModel->setItems(items);
        self->m_commentReplyModel->setLoading(false);
        self->setIsLoading(false);
        if (items.isEmpty()) {
          self->m_commentReplyModel->setErrorMessage("暂无回复");
        }
      },
      [self](int, const QString &msg) {
        if (!self)
          return;
        self->m_commentReplyHasMore = false;
        emit self->replyHasMoreChanged();
        self->m_commentReplyModel->setLoading(false);
        self->m_commentReplyModel->setErrorMessage(msg);
        self->setIsLoading(false);
        emit self->toastMessage(QString("回复加载失败：%1").arg(msg));
      });
}

void BiliController::fetchMoreCommentReplies() {
  if (m_currentCommentRootRpid <= 0) return;
  if (!m_commentReplyHasMore) return;
  if (m_commentReplyModel->loading()) return;

  m_commentReplyPage++;
  m_commentReplyModel->setLoading(true);
  setIsLoading(true);

  QMap<QString, QString> params;
  params["oid"] = QString::number(m_currentVideo.aid);
  params["type"] = "1";
  params["root"] = QString::number(m_currentCommentRootRpid);
  params["ps"] = "20";
  params["pn"] = QString::number(m_commentReplyPage);

  QPointer<BiliController> self(this);
  m_network->get(
      "/video/comments/replies", params,
      [self](const QJsonObject &data) {
        if (!self) return;

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
        int num = pageObj.value("num").toInt(self->m_commentReplyPage);
        int size = pageObj.value("size").toInt(20);
        bool hasMore = (count > 0) ? (num * size < count) : (items.size() >= size);

        self->m_commentReplyHasMore = hasMore;
        emit self->replyHasMoreChanged();

        self->m_commentReplyModel->appendItems(items);
        self->m_commentReplyModel->setLoading(false);
        self->setIsLoading(false);
      },
      [self](int, const QString &msg) {
        if (!self) return;
        self->m_commentReplyModel->setLoading(false);
        self->setIsLoading(false);
        emit self->toastMessage(QString("回复加载失败：%1").arg(msg));
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

          QPointer<BiliController> self2(self);
          self->m_network->get(
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

// ====== API: 收藏状态 ======

void BiliController::fetchFavoriteStatus() {
  if (!m_loggedIn) {
    return;
  }
  if (m_currentVideo.aid <= 0) {
    return;
  }

  QMap<QString, QString> params;
  params["aid"] = QString::number(m_currentVideo.aid);

  QPointer<BiliController> self(this);

  m_network->get(
      "/fav/status", params,
      [self](const QJsonObject &data) {
        if (!self)
          return;

        bool fav = false;
        if (data.value("favoured").isBool()) {
          fav = data.value("favoured").toBool(false);
        } else {
          fav = data.value("favoured").toInt(0) == 1;
        }
        if (self->m_isFavorited != fav) {
          self->m_isFavorited = fav;
          emit self->favoriteStatusChanged();
        }
      },
      [self](int, const QString &msg) {
        if (!self)
          return;
        emit self->toastMessage(QString("获取收藏状态失败：%1").arg(msg));
      });
}

void BiliController::fetchCoinStatus() {
  if (!m_loggedIn) {
    return;
  }
  if (m_currentVideo.aid <= 0) {
    return;
  }

  QMap<QString, QString> params;
  params["aid"] = QString::number(m_currentVideo.aid);

  QPointer<BiliController> self(this);

  m_network->get(
      "/coin/status", params,
      [self](const QJsonObject &data) {
        if (!self)
          return;

        bool coined = false;
        int multiply = data.value("multiply").toInt(0);
        if (multiply > 0) {
          coined = true;
        } else if (data.value("multiply").isString()) {
          coined = data.value("multiply").toString().toInt() > 0;
        }
        if (self->m_isCoined != coined) {
          self->m_isCoined = coined;
          emit self->coinStatusChanged();
        }
      },
      [self](int, const QString &msg) {
        if (!self)
          return;
        emit self->toastMessage(QString("获取投币状态失败：%1").arg(msg));
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

  QMap<QString, QString> params;
  params["aid"] = QString::number(m_currentVideo.aid);

  QPointer<BiliController> self(this);

  m_network->get(
      "/like/status", params,
      [self](const QJsonObject &data) {
        if (!self)
          return;

        int liked = 0;
        if (data.value("liked").isBool()) {
          liked = data.value("liked").toBool() ? 1 : 0;
        } else if (data.value("liked").isString()) {
          liked = data.value("liked").toString().toInt();
        } else {
          liked = data.value("liked").toInt(0);
        }
        bool isLiked = liked == 1;
        if (self->m_isLiked != isLiked) {
          self->m_isLiked = isLiked;
          emit self->likeStatusChanged();
        }
      },
      [self](int, const QString &msg) {
        if (!self)
          return;
        emit self->toastMessage(QString("获取点赞状态失败：%1").arg(msg));
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
          emit self->toastMessage("点赞成功");
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

void BiliController::launchExternalPlayer(const QString &path) {
  if (path.isEmpty()) {
    emit toastMessage("播放路径为空");
    return;
  }

  QString filePath = path;
  if (filePath.startsWith("file://")) {
    filePath = filePath.mid(7);
  }

  QString player = "/userdisk/VideoPlayer";
  if (!QFile::exists(player)) {
    emit toastMessage("外部播放器不存在");
    return;
  }

  bool ok = QProcess::startDetached(player, QStringList() << filePath);
  if (!ok) {
    emit toastMessage("启动外部播放器失败");
  }
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

  QString player = "/userdisk/VideoPlayer";
  if (!QFile::exists(player)) {
    emit toastMessage("外部播放器不存在");
    return;
  }

  QStringList args;
  args << v << ("--audio-file=" + a);
  bool ok = QProcess::startDetached(player, args);
  if (!ok) {
    emit toastMessage("启动外部播放器失败");
  }
}

void BiliController::launchExternalPlayerWithAudioUrl(const QString &videoUrl, const QString &audioUrl) {
  if (videoUrl.isEmpty() || audioUrl.isEmpty()) {
    emit toastMessage("播放地址不完整");
    return;
  }

  QString player = "/userdisk/VideoPlayer";
  if (!QFile::exists(player)) {
    emit toastMessage("外部播放器不存在");
    return;
  }

  QStringList args;
  args << videoUrl << ("--audio-file=" + audioUrl)
       << "--referrer=https://www.bilibili.com"
       << "--user-agent=Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
       << ("--script-opts=bili-aid=" + QString::number(videoAid())
           + ",bili-cid=" + QString::number(m_currentVideo.cid)
           + ",bili-bvid=" + m_currentVideo.bvid);

  bool ok = QProcess::startDetached(player, args);
  if (!ok) {
    emit toastMessage("启动外部播放器失败");
  }
}

void BiliController::launchExternalPlayerWithAudioUrlAndSubtitle(const QString &videoUrl, const QString &audioUrl, const QString &subtitlePath) {
  if (videoUrl.isEmpty() || audioUrl.isEmpty()) {
    emit toastMessage("播放地址不完整");
    return;
  }

  QString player = "/userdisk/VideoPlayer";
  if (!QFile::exists(player)) {
    emit toastMessage("外部播放器不存在");
    return;
  }

  QString sub = subtitlePath;
  if (sub.startsWith("file://")) {
    sub = sub.mid(7);
  }

  QStringList args;
  args << videoUrl << ("--audio-file=" + audioUrl);
  if (!sub.isEmpty()) {
    args << ("--sub-file=" + sub);
  }
  args << "--referrer=https://www.bilibili.com"
       << "--user-agent=Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
       << ("--script-opts=bili-aid=" + QString::number(videoAid())
           + ",bili-cid=" + QString::number(m_currentVideo.cid)
           + ",bili-bvid=" + m_currentVideo.bvid);

  bool ok = QProcess::startDetached(player, args);
  if (!ok) {
    emit toastMessage("启动外部播放器失败");
  }
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

void BiliController::setSubtitleOutline(double value) {
  if (value < 0.5) value = 0.5;
  if (value > 6.0) value = 6.0;
  if (qFuzzyCompare(m_subtitleOutline, value)) return;
  m_subtitleOutline = value;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("subtitleOutline", m_subtitleOutline);
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

void BiliController::setSubtitleBold(int value) {
  value = qBound(0, value, 1);
  if (m_subtitleBold == value) return;
  m_subtitleBold = value;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("subtitleBold", m_subtitleBold);
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

  QMap<QString, QString> params;
  params["aid"] = QString::number(m_currentVideo.aid);
  params["cid"] = QString::number(m_currentVideo.cid);
  params["bvid"] = m_currentVideo.bvid;
  params["sid"] = QString::number(m_selectedSubtitleId);
  params["font_size"] = QString::number(m_subtitleFontSize);
  params["outline"] = QString::number(m_subtitleOutline, 'f', 2);
  params["margin_v"] = QString::number(m_subtitleMarginV);
  params["spacing"] = QString::number(m_subtitleSpacing, 'f', 2);
  params["bold"] = QString::number(m_subtitleBold);

  QPointer<BiliController> self(this);
  setIsLoading(true);
  m_network->get(
      "/video/subtitle/ass", params,
      [self](const QJsonObject &data) {
        if (!self)
          return;
        self->setIsLoading(false);
        QString path = data.value("path").toString();
        if (path.isEmpty()) {
          emit self->toastMessage("字幕文件生成失败");
          return;
        }
        self->launchExternalPlayerWithAudioUrlAndSubtitle(self->m_dashVideoUrl, self->m_dashAudioUrl, path);
      },
      [self](int, const QString &msg) {
        if (!self)
          return;
        self->setIsLoading(false);
        emit self->toastMessage(QString("获取字幕失败：%1").arg(msg));
      });
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

                VideoItem item;
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

                VideoItem item;
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

        // 解析可用清晰度（以 accept_quality 为准）
        QVector<int> newAccepts;
        QJsonArray accept = data.value("accept_quality").toArray();
        for (const QJsonValue &v : accept) {
          int q = v.toInt(0);
          if (q > 0) newAccepts.append(q);
        }
        // 兼容 support_formats（仅保留可观看清晰度，作为补充）
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
