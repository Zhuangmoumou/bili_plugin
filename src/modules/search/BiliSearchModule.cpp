#include "modules/search/BiliSearchModule.h"
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
#include <iostream>

// 由插件文件提供的 Go 服务控制函数
extern bool bili_startApiServer();
extern void bili_stopApiServer();

BiliSearchModule::BiliSearchModule(BiliController *controller)
    : m_controller(controller) {}

// ====== API: 搜索 ======

void BiliSearchModule::search(const QString &keyword, int page) {
  QString trimmed = keyword.trimmed();
  if (trimmed.isEmpty()) {
    emit m_controller->toastMessage("请输入搜索关键词");
    return;
  }
  if (m_controller->m_searchModel->loading())
    return;

  // 限制关键词长度
  if (trimmed.length() > 100) {
    trimmed = trimmed.left(100);
  }

  page = qBound(1, page, 100);

  m_controller->m_searchKeyword = trimmed;
  m_controller->m_searchPage = page;

  if (page == 1) {
    m_controller->m_searchModel->clear();
  }
  m_controller->m_searchModel->setKeyword(m_controller->m_searchKeyword);
  m_controller->m_searchModel->setLoading(true);
  m_controller->setIsLoading(true);

  // 更新搜索历史
  m_controller->m_searchHistory.removeAll(trimmed);          // 移除旧的重复项
  m_controller->m_searchHistory.prepend(trimmed);            // 添加到最前面
  while (m_controller->m_searchHistory.size() > 10) {         // 保持最多10个
    m_controller->m_searchHistory.removeLast();
  }
  m_controller->m_searchHistoryModel->setStringList(m_controller->m_searchHistory);
  m_controller->saveSearchHistory();

  QMap<QString, QString> params;
  params["keyword"] = m_controller->m_searchKeyword;
  params["page"] = QString::number(page);

  QPointer<BiliController> self(m_controller);

  m_controller->m_network->get(
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

void BiliSearchModule::searchMore() {
  if (!m_controller->m_searchModel->hasMore() || m_controller->m_searchModel->loading())
    return;
  m_controller->m_searchPage++;
  search(m_controller->m_searchKeyword, m_controller->m_searchPage);
}

void BiliSearchModule::clearSearchHistory() {
  m_controller->m_searchHistory.clear();
  m_controller->m_searchHistoryModel->setStringList(m_controller->m_searchHistory);
  m_controller->saveSearchHistory();
}

void BiliSearchModule::removeSearchHistory(const QString &keyword) {
  QString kw = keyword.trimmed();
  if (kw.isEmpty()) return;
  int removed = m_controller->m_searchHistory.removeAll(kw);
  if (removed <= 0) return;
  m_controller->m_searchHistoryModel->setStringList(m_controller->m_searchHistory);
  m_controller->saveSearchHistory();
  emit m_controller->toastMessage(QString("已删除搜索历史：%1").arg(kw));
}
