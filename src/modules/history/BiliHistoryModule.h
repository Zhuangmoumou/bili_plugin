#pragma once

#include <QObject>

class BiliController;

class BiliHistoryModule : public QObject {
  Q_OBJECT
public:
  explicit BiliHistoryModule(BiliController *controller);

  Q_INVOKABLE void fetchRecentHistory();
  Q_INVOKABLE void fetchMoreRecentHistory();
  Q_INVOKABLE void fetchWatchLater(int page = 1, int pageSize = 20);
  Q_INVOKABLE void fetchMoreWatchLater();
  Q_INVOKABLE void addToWatchLater();
  Q_INVOKABLE QObject *recentHistoryModel();
  Q_INVOKABLE QObject *watchLaterModel();

private:
  BiliController *m_controller;
  int m_recentHistoryMax = 0;
  int m_recentHistoryViewAt = 0;
  int m_watchLaterPage = 1;
  bool m_watchLaterHasMore = true;
};
