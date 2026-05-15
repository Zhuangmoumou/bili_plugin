#pragma once

#include <QtGlobal>

class BiliController;

class BiliCommentModule {
public:
  explicit BiliCommentModule(BiliController *controller);
  void fetchComments(int page = 1);
  void fetchCommentReplies(qint64 rootRpid);
  void fetchMoreCommentReplies();
  void fetchMoreComments();

private:
  BiliController *m_controller;
};
