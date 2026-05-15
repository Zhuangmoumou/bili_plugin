#pragma once

#include <QtGlobal>

class BiliController;

class BiliFeedModule {
public:
  explicit BiliFeedModule(BiliController *controller);
  void fetchPopular(int page = 1, int pageSize = 10);
  void fetchMorePopular();
  void fetchRanking(int rid = 0);
  void fetchHotSearch();

private:
  BiliController *m_controller;
};
