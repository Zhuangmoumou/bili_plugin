#pragma once

#include <QtGlobal>
#include <QString>

class BiliController;

class BiliUpModule {
public:
  explicit BiliUpModule(BiliController *controller);
  void fetchUpInfo(qint64 mid);
  void fetchUpVideos(qint64 mid, int page = 1, int pageSize = 20);
  void fetchMoreUpVideos();
  void fetchUpSeasons(qint64 mid);
  void selectUpSeason(qint64 seasonId, const QString &name = QString(), bool isSeries = false, int total = 0);
  void toggleUpFollow();
  void playVideoPart(int index);
  void restartGoServer();
  void downloadVideoToDisk(int quality);

private:
  BiliController *m_controller;
};
