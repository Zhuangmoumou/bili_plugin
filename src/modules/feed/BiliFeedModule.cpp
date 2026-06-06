#include "modules/feed/BiliFeedModule.h"
#include "BiliAsyncUtils.hpp"
#include "BiliController.h"
#include "BiliJsonUtils.h"
#include "BiliModels.h"
#include "BiliNetwork.h"
#include "modules/history/BiliHistoryModule.h"
#include "modules/login/BiliLoginModule.h"
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

// 由插件文件提供的 Go 服务控制函数
extern bool bili_startApiServer();
extern void bili_stopApiServer();

namespace {

struct ParsedVideoList {
  int page = 1;
  QVector<VideoItem> items;
  bool hasMore = false;
};

ParsedVideoList parsePopularPayload(const QJsonObject &data, int page) {
  ParsedVideoList result;
  result.page = page;

  QJsonArray list = data.value("list").toArray();
  if (list.isEmpty()) {
    list = data.value("item").toArray();
  }
  if (list.isEmpty()) {
    list = data.value("items").toArray();
  }

  result.items.reserve(list.size());
  for (const QJsonValue &v : list) {
    if (v.isObject()) {
      result.items.append(VideoListModel::parseVideoItem(v.toObject()));
    }
  }

  const bool noMore = data.value("no_more").toBool(false);
  result.hasMore = noMore ? false : !result.items.isEmpty();
  return result;
}

QVector<VideoItem> parseRankingPayload(const QJsonObject &data) {
  QJsonArray list = data.value("list").toArray();
  QVector<VideoItem> items;
  items.reserve(list.size());
  for (const QJsonValue &v : list) {
    if (v.isObject()) {
      items.append(VideoListModel::parseVideoItem(v.toObject()));
    }
  }
  return items;
}

QVector<HotSearchItem> parseHotSearchPayload(const QJsonObject &data) {
  QJsonObject trending = data.value("trending").toObject();
  QJsonArray list = trending.value("list").toArray();

  QVector<HotSearchItem> items;
  items.reserve(qMin(list.size(), 50));
  int pos = 1;
  for (const QJsonValue &v : list) {
    if (pos > 50)
      break;
    QJsonObject obj = v.toObject();
    HotSearchItem item;
    item.keyword = obj.value("keyword").toString().left(100);
    item.icon = obj.value("icon").toString();
    item.position = pos++;
    items.append(item);
  }
  return items;
}

} // namespace

BiliFeedModule::BiliFeedModule(BiliController *controller)
    : QObject(controller), m_controller(controller) {}

QObject *BiliFeedModule::popularModel() { return m_controller->popularListModel(); }
QObject *BiliFeedModule::rankingModel() { return m_controller->rankingListModel(); }
QObject *BiliFeedModule::hotSearchModel() { return m_controller->hotSearchListModel(); }

// ====== API: 热门视频 ======

void BiliFeedModule::fetchPopular(int page, int pageSize) {
  if (m_controller->popularListModel()->loading())
    return;

  // 参数校验
  page = qBound(1, page, 1000);
  pageSize = qBound(1, pageSize, 30);

  m_popularPage = page;
  if (page == 1) {
    m_controller->popularListModel()->clear();
  }
  m_controller->popularListModel()->setLoading(true);
  m_controller->popularListModel()->setErrorMessage("");
  m_controller->setIsLoading(true);

  QString apiPath = "/recommend";

  QMap<QString, QString> paramsRecommend;
  // fresh_type: 3 表示换一换推荐，4 用于后续刷新
  paramsRecommend["fresh_type"] = (page <= 1) ? "3" : "4";

  QPointer<BiliController> self(m_controller);
  VideoListModel *popularModel = m_controller->popularListModel();

  auto onSuccess = [this, self, popularModel, page](const QJsonObject &data) {
    if (!self || !popularModel)
      return;
    biliRunInWorker(
        self, [data, page]() { return parsePopularPayload(data, page); },
        [this, self, popularModel](ParsedVideoList result) {
          if (!self || !popularModel || m_popularPage != result.page)
            return;
          popularModel->appendItems(result.items);
          popularModel->setHasMore(result.hasMore);
          popularModel->setLoading(false);
          self->setIsLoading(false);

          if (result.items.isEmpty() && result.page == 1) {
            popularModel->setErrorMessage("暂无推荐视频");
          }
        });
  };

  auto onErrorFinal = [self, popularModel](int code, const QString &msg) {
    if (!self || !popularModel)
      return;

    if (code == -101 || code == -401 || code == 401) {
      self->clearLocalLoginState();
    }

    popularModel->setLoading(false);
    popularModel->setErrorMessage(msg);
    self->setIsLoading(false);
    emit self->toastMessage(QString("加载失败：%1").arg(msg));
  };

  m_controller->network()->get(apiPath, paramsRecommend, onSuccess, onErrorFinal);
}

void BiliFeedModule::fetchMorePopular() {
  if (!m_controller->popularListModel()->hasMore() || m_controller->popularListModel()->loading())
    return;
  if (m_controller->popularListModel()->count() <= 0)
    return;
  m_popularPage++;
  fetchPopular(m_popularPage, 10);
}

// ====== API: 排行榜 ======

void BiliFeedModule::fetchRanking(int rid) {
  if (m_controller->rankingListModel()->loading())
    return;

  rid = qBound(0, rid, 9999);

  m_controller->rankingListModel()->clear();
  m_controller->rankingListModel()->setLoading(true);
  m_controller->setIsLoading(true);

  QMap<QString, QString> params;
  params["rid"] = QString::number(rid);
  params["type"] = "all";

  QPointer<BiliController> self(m_controller);
  VideoListModel *rankingModel = m_controller->rankingListModel();

  m_controller->network()->get(
      "/ranking", params,
      [self, rankingModel](const QJsonObject &data) {
        if (!self || !rankingModel)
          return;
        biliRunInWorker(
            self, [data]() { return parseRankingPayload(data); },
            [self, rankingModel](QVector<VideoItem> items) {
              if (!self || !rankingModel)
                return;
              rankingModel->appendItems(items);
              rankingModel->setHasMore(false);
              rankingModel->setLoading(false);
              self->setIsLoading(false);
            });
      },
      [self, rankingModel](int code, const QString &msg) {
        Q_UNUSED(code)
        if (!self || !rankingModel)
          return;

        rankingModel->setLoading(false);
        rankingModel->setErrorMessage(msg);
        self->setIsLoading(false);
        emit self->toastMessage(QString("排行榜加载失败：%1").arg(msg));
      });
}

// ====== API: 热搜 ======

void BiliFeedModule::fetchHotSearch() {
  if (m_controller->hotSearchListModel()->loading())
    return;

  m_controller->hotSearchListModel()->setLoading(true);

  QMap<QString, QString> params;
  params["limit"] = "10";

  QPointer<BiliController> self(m_controller);
  HotSearchModel *hotSearchModel = m_controller->hotSearchListModel();

  m_controller->network()->get(
      "/hot/search", params,
      [self, hotSearchModel](const QJsonObject &data) {
        if (!self || !hotSearchModel)
          return;
        biliRunInWorker(
            self, [data]() { return parseHotSearchPayload(data); },
            [self, hotSearchModel](QVector<HotSearchItem> items) {
              if (!self || !hotSearchModel)
                return;
              hotSearchModel->setItems(items);
              hotSearchModel->setLoading(false);
            });
      },
      [self, hotSearchModel](int code, const QString &msg) {
        Q_UNUSED(code)
        if (!self || !hotSearchModel)
          return;

        hotSearchModel->setLoading(false);
        emit self->toastMessage(QString("热搜加载失败：%1").arg(msg));
      });
}
