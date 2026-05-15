#pragma once

#include <QtGlobal>
#include <QString>

class BiliController;

class BiliSearchModule {
public:
  explicit BiliSearchModule(BiliController *controller);
  void search(const QString &keyword, int page = 1);
  void searchMore();
  void clearSearchHistory();
  void removeSearchHistory(const QString &keyword);

private:
  BiliController *m_controller;
};
