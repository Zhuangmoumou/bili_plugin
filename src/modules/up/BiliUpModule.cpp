#include "modules/up/BiliUpModule.h"
#include "BiliController.h"
#include "BiliJsonUtils.h"
#include "BiliModels.h"
#include "BiliNetwork.h"
#include "modules/history/BiliHistoryModule.h"
#include "modules/login/BiliLoginModule.h"
#include "modules/playback/BiliPlaybackModule.h"
#include "modules/season/BiliSeasonModule.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QRegExp>
#include <QSettings>
#include <QStandardPaths>
#include <QStringListModel>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QtGlobal>
#include <algorithm>
#include <functional>
#include <iostream>

// 由插件文件提供的 Go 服务控制函数
extern bool bili_startApiServer();
extern void bili_stopApiServer();

BiliUpModule::BiliUpModule(BiliController *controller)
    : QObject(controller), m_controller(controller) {}

QObject *BiliUpModule::upVideoModel() { return m_controller->m_upVideoModel; }
QObject *BiliUpModule::upSeasonModel() { return m_controller->m_upSeasonModel; }

// ====== UP 主主页 ======

void BiliUpModule::fetchUpInfo(qint64 mid) {
  if (mid <= 0) {
    return;
  }

  if (m_controller->m_upUserMid != mid) {
    m_controller->m_upUserMid = mid;
    m_controller->m_upVideoPage = 1;
    m_controller->m_upVideoHasMore = true;
    m_controller->m_upVideoCursorNext = 0;
    m_controller->setUpVideoTotal(0);
    if (m_controller->m_upVideoModel) {
      m_controller->m_upVideoModel->clear();
      m_controller->m_upVideoModel->setHasMore(true);
    }
    if (m_controller->m_upIsFollowing) {
      m_controller->m_upIsFollowing = false;
      emit m_controller->upFollowChanged();
    }
    // 重置合集筛选 / 合集列表
    if (m_controller->m_upSeasonModel) m_controller->m_upSeasonModel->clear();
    bool seasonChanged = (m_controller->m_upSelectedSeasonId != 0)
                         || !m_controller->m_upSelectedSeasonName.isEmpty()
                         || m_controller->m_upSelectedIsSeries;
    m_controller->m_upSelectedSeasonId = 0;
    m_controller->m_upSelectedSeasonName.clear();
    m_controller->m_upSelectedIsSeries = false;
    m_controller->m_upSeasonVideoPage = 1;
    m_controller->m_upSeasonVideoHasMore = true;
    if (seasonChanged) emit m_controller->upSelectedSeasonChanged();
  }

  QMap<QString, QString> params;
  params["mid"] = QString::number(mid);

  m_controller->apiGet(
      "/user/info", params,
      [this, mid](const QJsonObject &data) {
        if (m_controller->m_upUserMid != mid)
          return;

        const QJsonObject obj = data;

        m_controller->m_upUserName = obj.value("name").toString();
        if (m_controller->m_upUserName.isEmpty()) {
          m_controller->m_upUserName = obj.value("uname").toString();
        }
        m_controller->m_upUserFace = obj.value("face").toString();
        m_controller->m_upUserSign = obj.value("sign").toString();

        int level = obj.value("level").toInt(0);
        if (level <= 0) {
          QJsonObject levelInfo = obj.value("level_info").toObject();
          level = levelInfo.value("current_level").toInt(0);
        }
        m_controller->m_upUserLevel = level;

        auto toIntSafe = [](const QJsonValue &v) -> int {
          if (v.isDouble())
            return v.toInt();
          if (v.isString())
            return v.toString().toInt();
          return 0;
        };
        m_controller->m_upUserFans = toIntSafe(obj.value("follower"));
        m_controller->m_upUserFollowing = toIntSafe(obj.value("following"));

        bool newFollowState = obj.value("is_following").toBool(false)
                              || obj.value("is_followed").toBool(false);
        bool followStateChanged = (m_controller->m_upIsFollowing != newFollowState);
        m_controller->m_upIsFollowing = newFollowState;

        emit m_controller->upUserChanged();
        if (followStateChanged) {
          emit m_controller->upFollowChanged();
        }
      },
      [this](int, const QString &msg) {
        emit m_controller->toastMessage(QString("获取UP主信息失败：%1").arg(msg));
      },
      true);
}

void BiliUpModule::fetchUpVideos(qint64 mid, int page, int pageSize) {
  if (mid <= 0) {
    return;
  }
  if (m_controller->m_upVideoModel->loading())
    return;

  page = qBound(1, page, 9999);
  pageSize = qBound(1, pageSize, 30);

  if (m_controller->m_upUserMid != mid) {
    m_controller->m_upUserMid = mid;
    m_controller->m_upVideoPage = 1;
    m_controller->m_upVideoHasMore = true;
    m_controller->m_upVideoCursorNext = 0;
    m_controller->m_upVideoModel->clear();
  }

  m_controller->m_upVideoPage = page;
  if (page == 1) {
    m_controller->m_upVideoCursorNext = 0;
    m_controller->m_upVideoModel->clear();
  }
  m_controller->m_upVideoModel->setLoading(true);
  m_controller->m_upVideoModel->setErrorMessage("");

  QMap<QString, QString> params;
  params["mid"] = QString::number(mid);
  params["ps"] = QString::number(pageSize);

  // 优先使用游标翻页：当 page>1 且已有游标时，带 max 参数请求下一段
  // 这样可避免 UP 新增稿件导致 pn 翻页出现丢失/重复。
  if (page > 1 && m_controller->m_upVideoCursorNext > 0) {
    params["pn"] = "1";
    params["max"] = QString::number(m_controller->m_upVideoCursorNext);
  } else {
    params["pn"] = QString::number(page);
  }

  m_controller->apiGet(
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

        m_controller->m_upVideoModel->appendItems(items);

        QJsonObject pageObj = data.value("page").toObject();
        bool totalKnown = false;
        int total = BiliJson::intValue(data.value("count"), &totalKnown);
        if (!totalKnown) total = BiliJson::intValue(pageObj.value("count"), &totalKnown);
        if (!totalKnown) total = BiliJson::intValue(pageObj.value("total"), &totalKnown);
        if (totalKnown) m_controller->setUpVideoTotal(total);

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
          m_controller->m_upVideoCursorNext = nextCursor;
        }

        bool hasMore = !items.isEmpty();
        if (data.contains("has_next")) {
          hasMore = data.value("has_next").toBool(false);
        } else {
          // 兼容 web 分页
          int count = pageObj.value("count").toInt(0);
          int num = pageObj.value("pn").toInt(page);
          int size = pageObj.value("ps").toInt(pageSize);
          if (count > 0 && size > 0) {
            hasMore = (num * size < count);
          } else {
            // 若是游标模式但缺失 has_next，则按 nextCursor 是否变化判断
            hasMore = (m_controller->m_upVideoCursorNext > 0) && (items.size() >= pageSize);
          }
        }
        m_controller->m_upVideoHasMore = hasMore;
        m_controller->m_upVideoModel->setHasMore(hasMore);
        m_controller->m_upVideoModel->setLoading(false);

        if (items.isEmpty() && m_controller->m_upVideoPage == 1) {
          m_controller->m_upVideoModel->setErrorMessage("暂无投稿");
        }
      },
      [this](int, const QString &msg) {
        m_controller->m_upVideoModel->setLoading(false);
        m_controller->m_upVideoModel->setErrorMessage(msg);
        emit m_controller->toastMessage(QString("加载投稿失败：%1").arg(msg));
      },
      true);
}

void BiliUpModule::fetchMoreUpVideos() {
  // 若当前选中的是合集/系列，则委托到合集翻页
  if (m_controller->m_upSelectedSeasonId > 0) {
    m_controller->m_seasonModule->fetchMoreUpSeasonVideos();
    return;
  }
  if (!m_controller->m_upVideoHasMore || m_controller->m_upVideoModel->loading())
    return;
  m_controller->m_upVideoPage++;
  fetchUpVideos(m_controller->m_upUserMid, m_controller->m_upVideoPage);
}

void BiliUpModule::fetchUpSeasons(qint64 mid) {
  if (mid <= 0 || !m_controller->m_upSeasonModel) return;
  if (m_controller->m_upSeasonModel->loading()) return;

  // 切换 UP 主时由 fetchUpInfo 负责清空 model；这里只在 mid 一致时拉取
  if (m_controller->m_upUserMid != mid) {
    m_controller->m_upUserMid = mid;
  }

  m_controller->m_upSeasonModel->setLoading(true);

  QMap<QString, QString> params;
  params["mid"] = QString::number(mid);
  params["pn"] = "1";
  params["ps"] = "20";

  QPointer<BiliController> self(m_controller);
  m_controller->apiGet(
      "/user/seasons", params,
      [self, mid](const QJsonObject &data) {
        if (!self || self->m_upUserMid != mid) return;
        if (!self->m_upSeasonModel) return;

        QVector<UpSeasonItem> items;
        QJsonObject itemsLists = data.value("items_lists").toObject();

        auto parseList = [&](const QJsonArray &arr, bool isSeries) {
          for (const QJsonValue &v : arr) {
            if (!v.isObject()) continue;
            QJsonObject obj = v.toObject();
            QJsonObject meta = obj.value("meta").toObject();
            UpSeasonItem it;
            if (isSeries) {
              it.seasonId = meta.value("series_id").toVariant().toLongLong();
            } else {
              it.seasonId = meta.value("season_id").toVariant().toLongLong();
            }
            it.name = meta.value("name").toString();
            it.cover = meta.value("cover").toString();
            it.total = meta.value("total").toInt();
            it.isSeries = isSeries;
            if (it.seasonId > 0 && !it.name.isEmpty())
              items.append(it);
          }
        };

        parseList(itemsLists.value("seasons_list").toArray(), false);
        parseList(itemsLists.value("series_list").toArray(), true);

        self->m_upSeasonModel->setItems(items);
        self->m_upSeasonModel->setLoading(false);
      },
      [self](int, const QString &msg) {
        if (!self || !self->m_upSeasonModel) return;
        self->m_upSeasonModel->setLoading(false);
        // 静默：合集列表失败不弹 toast，避免主页打扰
        Q_UNUSED(msg);
      },
      false);
}

void BiliUpModule::selectUpSeason(qint64 seasonId, const QString &name, bool isSeries, int total) {
  if (m_controller->m_upUserMid <= 0) return;
  if (m_controller->m_upSelectedSeasonId == seasonId && m_controller->m_upSelectedIsSeries == isSeries) {
    // 已选中，无需切换
    return;
  }
  m_controller->m_upSelectedSeasonId = seasonId;
  m_controller->m_upSelectedSeasonName = (seasonId == 0) ? QString() : name;
  m_controller->m_upSelectedIsSeries = (seasonId == 0) ? false : isSeries;
  m_controller->setUpVideoTotal(seasonId == 0 ? 0 : total);
  emit m_controller->upSelectedSeasonChanged();

  if (!m_controller->m_upVideoModel) return;
  // 切换：清空当前视频列表，重置翻页状态，重新拉取
  m_controller->m_upVideoModel->clear();
  m_controller->m_upVideoModel->setHasMore(true);
  m_controller->m_upVideoModel->setErrorMessage("");

  if (seasonId == 0) {
    // 恢复为“视频”全部投稿
    m_controller->m_upVideoPage = 1;
    m_controller->m_upVideoHasMore = true;
    m_controller->m_upVideoCursorNext = 0;
    fetchUpVideos(m_controller->m_upUserMid, 1, 20);
  } else {
    m_controller->m_upSeasonVideoPage = 1;
    m_controller->m_upSeasonVideoHasMore = true;
    m_controller->m_seasonModule->fetchUpSeasonVideos(1, 30);
  }
}

void BiliUpModule::toggleUpFollow() {
  if (!m_controller->m_loggedIn) {
    emit m_controller->toastMessage("请先登录后再关注");
    return;
  }
  if (m_controller->m_upUserMid <= 0) {
    emit m_controller->toastMessage("UP 主信息无效");
    return;
  }
  if (m_controller->m_userId > 0 && m_controller->m_userId == m_controller->m_upUserMid) {
    emit m_controller->toastMessage("不能关注自己");
    return;
  }
  if (m_controller->m_upFollowLoading) {
    return;
  }

  const bool targetFollow = !m_controller->m_upIsFollowing;
  m_controller->m_upFollowLoading = true;

  QMap<QString, QString> params;
  params["mid"] = QString::number(m_controller->m_upUserMid);
  params["action"] = targetFollow ? "1" : "0";

  QPointer<BiliController> self(m_controller);
  m_controller->apiGet(
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

void BiliUpModule::playVideoPart(int index) {
    if (!m_controller->m_videoPartModel || index < 0 || index >= m_controller->m_videoPartModel->count()) {
        return;
    }

    QModelIndex modelIndex = m_controller->m_videoPartModel->index(index, 0);
    qint64 newCid = m_controller->m_videoPartModel->data(modelIndex, VideoPartListModel::CidRole).toLongLong();

    if (newCid > 0 && m_controller->m_currentVideo.cid != newCid) {
        m_controller->m_currentVideo.cid = newCid;
        emit m_controller->videoDetailChanged(); // 更新cid
        emit m_controller->playbackProgressChanged();
        emit m_controller->toastMessage(QString("切换到 P%1").arg(index + 1));
        // 这里可以根据需求决定是否立即播放
        // downloadAndPlay(m_controller->m_playQuality); 
        // 或者只获取URL
        m_controller->m_playbackModule->fetchPlayUrl(m_controller->m_playQuality);
    }
}

void BiliUpModule::restartGoServer() {
    emit m_controller->toastMessage("正在重启 Go 服务端...");
    bili_stopApiServer();
    if (bili_startApiServer()) {
        emit m_controller->toastMessage("Go 服务端已重启");
    } else {
        emit m_controller->toastMessage("Go 服务端重启失败");
    }
}

// ====== 下载到指定目录 ======

void BiliUpModule::downloadVideoToDisk(int quality) {
  if (m_controller->m_currentVideo.bvid.isEmpty() || m_controller->m_currentVideo.cid == 0) {
    emit m_controller->toastMessage("视频信息不完整，无法下载");
    return;
  }

  if (m_controller->m_isDownloading) {
    emit m_controller->toastMessage("已有下载任务进行中");
    return;
  }

  const bool audioOnly = (quality == 0);
  const int requestQuality = audioOnly ? 16 : qBound(16, quality, 127);
  quality = requestQuality;

  // 1. 定义存储目录并确保它存在
  QString downloadDir = "/userdisk/Music/bili";
  QDir dir(downloadDir);
  if (!dir.exists()) {
      if (!dir.mkpath(".")) {
          emit m_controller->toastMessage("创建下载目录失败");
          return;
      }
  }

  // 2. 构建并净化文件名
  QString title = m_controller->videoTitle();

  // 多P视频：选集标题前10字符 + ... + 当前P标题
  if (m_controller->m_videoPartModel && m_controller->m_videoPartModel->count() > 1) {
      QString partTitle;
      for (int i = 0; i < m_controller->m_videoPartModel->count(); ++i) {
          QModelIndex idx = m_controller->m_videoPartModel->index(i, 0);
          qint64 cid = m_controller->m_videoPartModel->data(idx, VideoPartListModel::CidRole).toLongLong();
          if (cid == m_controller->m_currentVideo.cid) {
              partTitle = m_controller->m_videoPartModel->data(idx, VideoPartListModel::PartRole).toString();
              break;
          }
      }
      if (!partTitle.isEmpty()) {
          QString collectionTitle = m_controller->videoTitle().left(10);
          title = collectionTitle + "..." + partTitle;
      }
  }

  // 移除Windows和Linux文件名中的非法字符
  title.remove(QRegExp("[/\\\\:*?\"<>|]"));
  title = title.trimmed().left(100); // 限制文件名长度
  if (title.isEmpty()) {
      title = m_controller->m_currentVideo.bvid; // 如果标题为空，使用bvid作为备用
  }
  QString targetPath = dir.filePath(title + (audioOnly ? ".m4a" : ".mp4"));
  QString subtitleUrl;
  QString subtitlePath;
  if (m_controller->m_selectedSubtitleId > 0) {
      QUrl url(m_controller->m_network->apiBase() + "/video/subtitle/ass/file");
      QUrlQuery query;
      query.addQueryItem("aid", QString::number(m_controller->m_currentVideo.aid));
      query.addQueryItem("cid", QString::number(m_controller->m_currentVideo.cid));
      query.addQueryItem("bvid", m_controller->m_currentVideo.bvid);
      query.addQueryItem("sid", QString::number(m_controller->m_selectedSubtitleId));
      query.addQueryItem("font_size", QString::number(m_controller->m_subtitleFontSize));
      query.addQueryItem("margin_v", QString::number(m_controller->m_subtitleMarginV));
      query.addQueryItem("spacing", QString::number(m_controller->m_subtitleSpacing, 'f', 2));
      query.addQueryItem("weight", QString::number(m_controller->m_subtitleWeight));
      query.addQueryItem("color_preset", m_controller->m_subtitleColorPreset);
      query.addQueryItem("outline_enabled", m_controller->m_subtitleOutlineEnabled ? "1" : "0");
      query.addQueryItem("outline_width", QString::number(m_controller->m_subtitleOutlineWidth));
      query.addQueryItem("background_enabled", m_controller->m_subtitleBackgroundEnabled ? "1" : "0");
      url.setQuery(query);
      subtitleUrl = url.toString();
      subtitlePath = dir.filePath(QFileInfo(targetPath).completeBaseName() + ".ass");
  }

  // 3. 检查文件是否已存在
  if (QFile::exists(targetPath)) {
      emit m_controller->toastMessage("文件已存在");
      return;
  }

  m_controller->setIsLoading(true);

  QMap<QString, QString> params;
  params["aid"] = QString::number(m_controller->videoAid());
  params["cid"] = QString::number(m_controller->m_currentVideo.cid);
  params["qn"] = QString::number(requestQuality);
  params["bvid"] = m_controller->m_currentVideo.bvid;
  params["fnval"] = audioOnly ? "4048" : "1";

  QPointer<BiliController> self(m_controller);

  m_controller->m_network->get(
      "/video/playurl", params,
      [self, targetPath, quality, requestQuality, audioOnly, subtitleUrl, subtitlePath](const QJsonObject &data) {
        if (!self) return;

        self->updateAcceptQualities(data);

        QString downloadUrl;
        if (audioOnly) {
          downloadUrl = self->pickDashUrls(data, requestQuality).audioUrl;
        } else {
          downloadUrl = self->pickMp4Url(data);
        }

        if (downloadUrl.isEmpty()) {
          emit self->toastMessage(audioOnly ? "未获取到音频下载地址" : "未获取到下载地址");
          self->setIsLoading(false);
          return;
        }

        self->m_downloadStatus = audioOnly ? "准备下载音频..." : "准备下载...";
        emit self->downloadStateChanged();
        emit self->toastMessage(audioOnly ? "开始下载音频..." : "开始下载...");

        self->startDownloadTask(downloadUrl, QString(), targetPath, QString(),
                                audioOnly ? 0 : quality, false,
                                audioOnly ? QStringLiteral("音频下载完成: ") : QStringLiteral("下载完成: "),
                                audioOnly ? QStringLiteral("音频下载失败: ") : QStringLiteral("下载失败: "),
                                subtitleUrl, subtitlePath);
      },
      [self](int, const QString &msg) {
        if (!self) return;
        self->setIsLoading(false);
        emit self->toastMessage("获取下载地址失败: " + msg);
      });
}
