#pragma once

#include <QObject>
#include <QString>
#include <QtGlobal>

class BiliController;

class BiliCommentModule : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool replyHasMore READ replyHasMore NOTIFY replyHasMoreChanged)
public:
  explicit BiliCommentModule(BiliController *controller);
  bool replyHasMore() const { return m_commentReplyHasMore; }
  Q_INVOKABLE void fetchComments(int page = 1);
  Q_INVOKABLE void fetchCommentReplies(qint64 rootRpid);
  Q_INVOKABLE void fetchMoreCommentReplies();
  Q_INVOKABLE void fetchMoreComments();
  Q_INVOKABLE QObject *commentModel();
  Q_INVOKABLE QObject *commentReplyModel();
  void resetReplyState();

signals:
  void replyHasMoreChanged();

private:
  BiliController *m_controller;
  int m_commentPage = 1;
  QString m_commentNextOffset;
  bool m_commentHasMore = true;
  int m_commentReplyPage = 1;
  bool m_commentReplyHasMore = false;
  qint64 m_currentCommentRootRpid = 0;
};
