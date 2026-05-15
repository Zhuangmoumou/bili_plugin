#pragma once

#include <QtGlobal>

class BiliController;

class BiliSeasonModule {
public:
  explicit BiliSeasonModule(BiliController *controller);

  void fetchRelatedVideos();
  void fetchUpSeasonVideos(int page = 1, int pageSize = 30);
  void fetchMoreUpSeasonVideos();
  void fetchSeasonVideos(qint64 mid, qint64 seasonId, int page = 1, int pageSize = 30);
  void fetchMoreSeasonVideos();

private:
  BiliController *m_controller;
};
