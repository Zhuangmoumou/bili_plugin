#pragma once

#include <QtGlobal>
#include <QString>

class BiliController;

class BiliVideoModule {
public:
  explicit BiliVideoModule(BiliController *controller);
  void reportCurrentVideoAsRecentViewIfNeeded();
  void fetchVideoDetail(const QString &bvid);

private:
  BiliController *m_controller;
};
