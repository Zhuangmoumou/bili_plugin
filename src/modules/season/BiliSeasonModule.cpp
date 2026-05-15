#include "modules/season/BiliSeasonModule.h"

#include "BiliController.h"
#include "BiliJsonUtils.h"
#include "BiliModels.h"
#include "BiliNetwork.h"

#include <QJsonArray>
#include <QMap>
#include <QPointer>
#include <algorithm>

namespace {

QVector<VideoItem> parseSeasonArchiveItems(const QJsonArray &archives, qint64 ownerMid,
                                          const QString &ownerName) {
  QVector<VideoItem> items;
  items.reserve(archives.size());

  for (const QJsonValue &value : archives) {
    if (!value.isObject()) continue;
    QJsonObject obj = value.toObject();
    VideoItem item = VideoListModel::parseVideoItem(obj);
    if (item.aid <= 0) {
      item.aid = obj.value("aid").toVariant().toLongLong();
    }
    if (item.pubdate <= 0) {
      item.pubdate = obj.value("pubdate").toVariant().toLongLong();
    }
    if (item.pubdate <= 0) {
      item.pubdate = obj.value("ctime").toVariant().toLongLong();
    }
    if (item.duration == 0) {
      item.duration = obj.value("duration").toInt();
    }
    QJsonObject statObj = obj.value("stat").toObject();
    if (item.views == 0) {
      item.views = statObj.value("view").toVariant().toLongLong();
    }
    if (item.ownerName.isEmpty()) {
      item.ownerName = ownerName;
    }
    if (item.ownerMid == 0) {
      item.ownerMid = ownerMid;
    }
    items.append(item);
  }

  return items;
}

int seasonArchiveTotal(const QJsonObject &data, bool *ok) {
  QJsonObject pageObj = data.value("page").toObject();
  QJsonObject metaObj = data.value("meta").toObject();
  int total = BiliJson::intValue(pageObj.value("total"), ok);
  if (ok && !*ok) total = BiliJson::intValue(metaObj.value("total"), ok);
  return total;
}

bool seasonArchiveHasMore(const QJsonObject &data, int page, int pageSize, int total,
                          int itemCount) {
  QJsonObject pageObj = data.value("page").toObject();
  int pn = pageObj.value("page_num").toInt(page);
  int ps = pageObj.value("page_size").toInt(pageSize);
  if (total > 0 && ps > 0) {
    return pn * ps < total;
  }
  return itemCount >= pageSize;
}

void sortByNewest(QVector<VideoItem> &items) {
  std::sort(items.begin(), items.end(), [](const VideoItem &a, const VideoItem &b) {
    return a.pubdate > b.pubdate;
  });
}

QString videoRequestKey(const VideoItem &video) {
  return !video.bvid.isEmpty() ? video.bvid : QString::number(video.aid);
}

}  // namespace

BiliSeasonModule::BiliSeasonModule(BiliController *controller)
    : m_controller(controller) {}

void BiliSeasonModule::fetchRelatedVideos() {
  BiliController *controller = m_controller;
  if (!controller) return;
  if (!controller->m_relatedVideoModel) return;
  if (!controller->m_videoDetailLoadingBvid.isEmpty()) return;
  if (controller->m_currentVideo.bvid.isEmpty() && controller->m_currentVideo.aid <= 0) return;

  const QString requestKey = videoRequestKey(controller->m_currentVideo);
  if (controller->m_relatedVideoLoadingBvid == requestKey) return;
  if (controller->m_relatedVideoBvid == requestKey) return;

  if (controller->m_relatedVideoBvid != requestKey) {
    controller->m_relatedVideoModel->clear();
    controller->m_relatedVideoModel->setHasMore(false);
    controller->m_relatedVideoModel->setErrorMessage("");
  }

  QMap<QString, QString> params;
  if (!controller->m_currentVideo.bvid.isEmpty()) {
    params["bvid"] = controller->m_currentVideo.bvid;
  } else {
    params["aid"] = QString::number(controller->m_currentVideo.aid);
  }

  controller->m_relatedVideoLoadingBvid = requestKey;
  controller->m_relatedVideoModel->setLoading(true);

  QPointer<BiliController> self(controller);
  controller->m_network->get(
      "/video/related", params,
      [self, requestKey](const QJsonObject &data) {
        if (!self || !self->m_relatedVideoModel) return;
        const QString currentKey = videoRequestKey(self->m_currentVideo);
        if (currentKey != requestKey) {
          if (self->m_relatedVideoLoadingBvid == requestKey) {
            self->m_relatedVideoLoadingBvid.clear();
            self->m_relatedVideoModel->setLoading(false);
          }
          return;
        }

        QVector<VideoItem> items;
        QJsonArray list = data.value("list").toArray();
        items.reserve(list.size());
        for (const QJsonValue &value : list) {
          if (!value.isObject()) continue;
          VideoItem item = VideoListModel::parseVideoItem(value.toObject());
          if (item.bvid.isEmpty() || item.bvid == self->m_currentVideo.bvid) continue;
          items.append(item);
        }

        self->m_relatedVideoModel->clear();
        self->m_relatedVideoModel->appendItems(items);
        self->m_relatedVideoModel->setHasMore(false);
        self->m_relatedVideoModel->setLoading(false);
        self->m_relatedVideoBvid = requestKey;
        self->m_relatedVideoLoadingBvid.clear();
      },
      [self, requestKey](int, const QString &) {
        if (!self || !self->m_relatedVideoModel) return;
        if (self->m_relatedVideoLoadingBvid != requestKey) return;
        self->m_relatedVideoModel->setLoading(false);
        self->m_relatedVideoLoadingBvid.clear();
      });
}

void BiliSeasonModule::fetchUpSeasonVideos(int page, int pageSize) {
  BiliController *controller = m_controller;
  if (!controller) return;
  if (controller->m_upUserMid <= 0 || controller->m_upSelectedSeasonId <= 0) return;
  if (!controller->m_upVideoModel) return;
  if (controller->m_upVideoModel->loading()) return;

  page = qBound(1, page, 9999);
  pageSize = qBound(1, pageSize, 100);

  controller->m_upVideoModel->setLoading(true);
  controller->m_upVideoModel->setErrorMessage("");

  const qint64 mid = controller->m_upUserMid;
  const qint64 seasonId = controller->m_upSelectedSeasonId;

  QMap<QString, QString> params;
  params["mid"] = QString::number(mid);
  params["season_id"] = QString::number(seasonId);
  params["sort_reverse"] = "false";
  params["pn"] = QString::number(page);
  params["ps"] = QString::number(pageSize);

  QPointer<BiliController> self(controller);
  controller->apiGet(
      "/user/season/videos", params,
      [self, mid, seasonId, page, pageSize](const QJsonObject &data) {
        if (!self || !self->m_upVideoModel) return;
        if (self->m_upUserMid != mid || self->m_upSelectedSeasonId != seasonId) return;

        QVector<VideoItem> items = parseSeasonArchiveItems(data.value("archives").toArray(), mid,
                                                           self->m_upUserName);
        sortByNewest(items);

        self->m_upSeasonVideoPage = page;
        self->m_upVideoModel->appendItems(items);

        bool totalKnown = false;
        int total = seasonArchiveTotal(data, &totalKnown);
        if (totalKnown) self->setUpVideoTotal(total);
        bool hasMore = seasonArchiveHasMore(data, page, pageSize, total, items.size());
        self->m_upSeasonVideoHasMore = hasMore;
        self->m_upVideoModel->setHasMore(hasMore);
        self->m_upVideoModel->setLoading(false);

        if (items.isEmpty() && page == 1) {
          self->m_upVideoModel->setErrorMessage("该合集暂无视频");
        }
      },
      [self, mid, seasonId](int, const QString &msg) {
        if (!self || !self->m_upVideoModel) return;
        if (self->m_upUserMid != mid || self->m_upSelectedSeasonId != seasonId) return;
        self->m_upVideoModel->setLoading(false);
        self->m_upVideoModel->setErrorMessage(msg);
        emit self->toastMessage(QString("加载合集失败：%1").arg(msg));
      },
      true);
}

void BiliSeasonModule::fetchMoreUpSeasonVideos() {
  BiliController *controller = m_controller;
  if (!controller) return;
  if (!controller->m_upSeasonVideoHasMore) return;
  if (!controller->m_upVideoModel || controller->m_upVideoModel->loading()) return;
  fetchUpSeasonVideos(controller->m_upSeasonVideoPage + 1, 30);
}

void BiliSeasonModule::fetchSeasonVideos(qint64 mid, qint64 seasonId, int page, int pageSize) {
  BiliController *controller = m_controller;
  if (!controller) return;
  if (mid <= 0 || seasonId <= 0) return;
  if (!controller->m_seasonVideoModel) return;
  if (controller->m_seasonVideoModel->loading()) return;

  page = qBound(1, page, 9999);
  pageSize = qBound(1, pageSize, 100);

  bool reset = page == 1 || controller->m_seasonVideoMid != mid || controller->m_seasonVideoSeasonId != seasonId;
  controller->m_seasonVideoMid = mid;
  controller->m_seasonVideoSeasonId = seasonId;

  if (reset) {
    controller->m_seasonVideoModel->clear();
    controller->m_seasonVideoModel->setHasMore(true);
    controller->m_seasonVideoHasMore = true;
    controller->setSeasonVideoTotal(0);
  }

  controller->m_seasonVideoModel->setLoading(true);
  controller->m_seasonVideoModel->setErrorMessage("");

  QMap<QString, QString> params;
  params["mid"] = QString::number(mid);
  params["season_id"] = QString::number(seasonId);
  params["sort_reverse"] = "false";
  params["pn"] = QString::number(page);
  params["ps"] = QString::number(pageSize);

  QString ownerName = controller->m_currentVideo.ownerName;
  QPointer<BiliController> self(controller);
  controller->apiGet(
      "/user/season/videos", params,
      [self, mid, seasonId, page, pageSize, ownerName](const QJsonObject &data) {
        if (!self || !self->m_seasonVideoModel) return;
        if (self->m_seasonVideoMid != mid || self->m_seasonVideoSeasonId != seasonId) return;

        QVector<VideoItem> items = parseSeasonArchiveItems(data.value("archives").toArray(), mid,
                                                           ownerName);
        sortByNewest(items);

        self->m_seasonVideoPage = page;
        self->m_seasonVideoModel->appendItems(items);

        bool totalKnown = false;
        int total = seasonArchiveTotal(data, &totalKnown);
        if (totalKnown) self->setSeasonVideoTotal(total);
        bool hasMore = seasonArchiveHasMore(data, page, pageSize, total, items.size());
        self->m_seasonVideoHasMore = hasMore;
        self->m_seasonVideoModel->setHasMore(hasMore);
        self->m_seasonVideoModel->setLoading(false);

        if (items.isEmpty() && page == 1) {
          self->m_seasonVideoModel->setErrorMessage("该合集暂无视频");
        }
      },
      [self, mid, seasonId](int, const QString &msg) {
        if (!self || !self->m_seasonVideoModel) return;
        if (self->m_seasonVideoMid != mid || self->m_seasonVideoSeasonId != seasonId) return;
        self->m_seasonVideoModel->setLoading(false);
        self->m_seasonVideoModel->setErrorMessage(msg);
        emit self->toastMessage(QString("加载合集失败：%1").arg(msg));
      },
      true);
}

void BiliSeasonModule::fetchMoreSeasonVideos() {
  BiliController *controller = m_controller;
  if (!controller) return;
  if (!controller->m_seasonVideoHasMore) return;
  if (!controller->m_seasonVideoModel || controller->m_seasonVideoModel->loading()) return;
  fetchSeasonVideos(controller->m_seasonVideoMid, controller->m_seasonVideoSeasonId,
                    controller->m_seasonVideoPage + 1, 30);
}
