#pragma once

#include <QtGlobal>

class BiliController;

class BiliFavoriteModule {
public:
  explicit BiliFavoriteModule(BiliController *controller);
  void fetchFavoriteFolders();
  void fetchFavoriteItems(qint64 mediaId, int page = 1, int pageSize = 20);
  void fetchMoreFavoriteItems();
  void fetchFavoriteStatus();
  void fetchCoinStatus();
  void addCoin(int multiply = 1, bool selectLike = false);
  void fetchLikeStatus();
  void fetchWatchLaterStatus();
  void toggleLike();
  void toggleFavorite();
  void toggleFavoriteTo(qint64 mediaId);
  void toggleWatchLater();

private:
  BiliController *m_controller;
};
