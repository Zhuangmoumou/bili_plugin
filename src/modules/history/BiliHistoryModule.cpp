#include "modules/history/BiliHistoryModule.h"

#include "BiliController.h"
#include "BiliModels.h"
#include "BiliNetwork.h"

#include <QJsonArray>
#include <QMap>
#include <QPointer>
#include <QtGlobal>

namespace {

QJsonArray recentHistoryList(const QJsonObject &data) {
  QJsonArray list = data.value("list").toArray();
  if (list.isEmpty()) {
    list = data.value("items").toArray();
  }
  return list;
}

QVector<VideoItem> parseRecentHistoryItems(const QJsonObject &data) {
  QJsonArray list = recentHistoryList(data);
  QVector<VideoItem> items;
  items.reserve(list.size());

  for (const QJsonValue &value : list) {
    if (!value.isObject()) continue;
    QJsonObject obj = value.toObject();
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

  return items;
}

QVector<VideoItem> parseWatchLaterItems(const QJsonObject &data) {
  QJsonArray list = data.value("list").toArray();
  if (list.isEmpty()) {
    list = data.value("data").toArray();
  }

  QVector<VideoItem> items;
  items.reserve(list.size());
  for (const QJsonValue &value : list) {
    if (!value.isObject()) continue;
    QJsonObject obj = value.toObject();

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

  return items;
}

}  // namespace

BiliHistoryModule::BiliHistoryModule(BiliController *controller)
    : m_controller(controller) {}

void BiliHistoryModule::fetchRecentHistory() {
  BiliController *controller = m_controller;
  if (!controller) return;
  if (!controller->m_loggedIn) {
    emit controller->toastMessage("请先登录后查看最近观看");
    return;
  }

  if (controller->m_recentHistoryModel->loading())
    return;

  controller->m_recentHistoryMax = 0;
  controller->m_recentHistoryViewAt = 0;
  controller->m_recentHistoryModel->clear();
  controller->m_recentHistoryModel->setLoading(true);
  controller->setIsLoading(true);

  QMap<QString, QString> params;
  QPointer<BiliController> self(controller);
  controller->m_network->get(
      "/history/recent", params,
      [self](const QJsonObject &data) {
        if (!self) return;

        QJsonObject cursor = data.value("cursor").toObject();
        self->m_recentHistoryMax = cursor.value("max").toVariant().toInt();
        self->m_recentHistoryViewAt = cursor.value("view_at").toVariant().toInt();

        QVector<VideoItem> items = parseRecentHistoryItems(data);
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

void BiliHistoryModule::fetchMoreRecentHistory() {
  BiliController *controller = m_controller;
  if (!controller) return;
  if (controller->m_recentHistoryModel->loading() || !controller->m_recentHistoryModel->hasMore())
    return;

  QMap<QString, QString> params;
  if (controller->m_recentHistoryMax > 0) {
    params["max"] = QString::number(controller->m_recentHistoryMax);
  }
  if (controller->m_recentHistoryViewAt > 0) {
    params["view_at"] = QString::number(controller->m_recentHistoryViewAt);
  }

  controller->m_recentHistoryModel->setLoading(true);
  controller->setIsLoading(true);

  QPointer<BiliController> self(controller);
  controller->m_network->get(
      "/history/recent", params,
      [self](const QJsonObject &data) {
        if (!self) return;

        QJsonObject cursor = data.value("cursor").toObject();
        self->m_recentHistoryMax = cursor.value("max").toVariant().toInt();
        self->m_recentHistoryViewAt = cursor.value("view_at").toVariant().toInt();

        QVector<VideoItem> items = parseRecentHistoryItems(data);
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

void BiliHistoryModule::fetchWatchLater(int page, int pageSize) {
  BiliController *controller = m_controller;
  if (!controller) return;
  if (!controller->m_loggedIn) {
    emit controller->toastMessage("请先登录后查看稍后再看");
    return;
  }
  if (controller->m_watchLaterModel->loading())
    return;

  page = qBound(1, page, 1000);
  pageSize = qBound(1, pageSize, 30);

  if (page == 1) {
    controller->m_watchLaterModel->clear();
  }
  controller->m_watchLaterModel->setLoading(true);
  controller->m_watchLaterModel->setErrorMessage("");
  controller->setIsLoading(true);

  QMap<QString, QString> params;
  params["pn"] = QString::number(page);
  params["ps"] = QString::number(pageSize);

  QPointer<BiliController> self(controller);
  controller->m_network->get(
      "/toview/list", params,
      [self, page, pageSize](const QJsonObject &data) {
        if (!self) return;

        QVector<VideoItem> items = parseWatchLaterItems(data);
        bool hasMore = data.value("has_more").toBool(false);
        if (!data.contains("has_more")) {
          hasMore = items.size() >= pageSize;
        }

        self->m_watchLaterPage = page;
        self->m_watchLaterHasMore = hasMore;
        self->m_watchLaterModel->appendItems(items);
        self->m_watchLaterModel->setHasMore(hasMore);
        self->m_watchLaterModel->setLoading(false);
        self->setIsLoading(false);

        if (items.isEmpty() && page == 1) {
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

void BiliHistoryModule::fetchMoreWatchLater() {
  BiliController *controller = m_controller;
  if (!controller) return;
  if (!controller->m_watchLaterHasMore || controller->m_watchLaterModel->loading())
    return;
  if (controller->m_watchLaterModel->count() <= 0)
    return;
  fetchWatchLater(controller->m_watchLaterPage + 1, 20);
}

void BiliHistoryModule::addToWatchLater() {
  BiliController *controller = m_controller;
  if (!controller) return;
  controller->toggleWatchLater();
}
