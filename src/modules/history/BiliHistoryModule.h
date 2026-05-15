#pragma once

class BiliController;

class BiliHistoryModule {
public:
  explicit BiliHistoryModule(BiliController *controller);

  void fetchRecentHistory();
  void fetchMoreRecentHistory();
  void fetchWatchLater(int page = 1, int pageSize = 20);
  void fetchMoreWatchLater();
  void addToWatchLater();

private:
  BiliController *m_controller;
};
