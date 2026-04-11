#include "BiliModels.h"
#include <QDateTime>
#include <QDebug>
#include <cmath>

// ============ VideoListModel ============

VideoListModel::VideoListModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_loading(false)
    , m_hasMore(true)
{
}

int VideoListModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_items.count();
}

QVariant VideoListModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_items.count())
        return QVariant();

    const VideoItem &item = m_items[index.row()];

    switch (role) {
    case BvidRole:      return item.bvid;
    case TitleRole:     return item.title;
    case PicRole:       return item.pic;
    case OwnerNameRole: return item.ownerName;
    case OwnerFaceRole: return item.ownerFace;
    case OwnerMidRole:  return item.ownerMid;
    case ViewsRole:     return formatCount(item.views);
    case DanmakuRole:   return formatCount(item.danmaku);
    case LikesRole:     return formatCount(item.likes);
    case DurationRole:  return item.duration;
    case CidRole:       return item.cid;
    case DescRole:      return item.desc;
    case RcmdReasonRole: return item.rcmdReason;
    case DurationTextRole: return formatDuration(item.duration);
    case PartCountRole: return item.partCount;
    default: return QVariant();
    }
}

QHash<int, QByteArray> VideoListModel::roleNames() const
{
    return {
        {BvidRole, "bvid"},
        {TitleRole, "title"},
        {PicRole, "pic"},
        {OwnerNameRole, "ownerName"},
        {OwnerFaceRole, "ownerFace"},
        {OwnerMidRole, "ownerMid"},
        {ViewsRole, "views"},
        {DanmakuRole, "danmakuCount"},
        {LikesRole, "likes"},
        {DurationRole, "duration"},
        {CidRole, "cid"},
        {DescRole, "desc"},
        {RcmdReasonRole, "rcmdReason"},
        {DurationTextRole, "durationText"},
        {PartCountRole, "partCount"}
    };
}

int VideoListModel::count() const { return m_items.count(); }
bool VideoListModel::loading() const { return m_loading; }
bool VideoListModel::hasMore() const { return m_hasMore; }
QString VideoListModel::errorMessage() const { return m_errorMessage; }

void VideoListModel::clear()
{
    beginResetModel();
    m_items.clear();
    m_errorMessage.clear();
    endResetModel();
    emit countChanged();
    emit errorMessageChanged();
}

void VideoListModel::appendItems(const QVector<VideoItem> &items)
{
    if (items.isEmpty()) return;

    beginInsertRows(QModelIndex(), m_items.count(),
                    m_items.count() + items.count() - 1);
    m_items.append(items);
    endInsertRows();
    emit countChanged();
}

void VideoListModel::setLoading(bool loading)
{
    if (m_loading != loading) {
        m_loading = loading;
        emit loadingChanged();
    }
}

void VideoListModel::setHasMore(bool hasMore)
{
    if (m_hasMore != hasMore) {
        m_hasMore = hasMore;
        emit hasMoreChanged();
    }
}

void VideoListModel::setErrorMessage(const QString &msg)
{
    if (m_errorMessage != msg) {
        m_errorMessage = msg;
        emit errorMessageChanged();
    }
}

QString VideoListModel::formatDuration(int seconds)
{
    if (seconds <= 0) return "00:00";
    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;
    if (h > 0) {
        return QString("%1:%2:%3")
            .arg(h)
            .arg(m, 2, 10, QChar('0'))
            .arg(s, 2, 10, QChar('0'));
    }
    return QString("%1:%2")
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'));
}

QString VideoListModel::formatCount(qint64 count)
{
    if (count < 0) return "0";
    if (count < 10000) return QString::number(count);
    double wan = count / 10000.0;
    if (wan < 10000) {
        return QString("%1 万").arg(wan, 0, 'f', 1);
    }
    double yi = count / 100000000.0;
    return QString("%1 亿").arg(yi, 0, 'f', 1);
}

VideoItem VideoListModel::parseVideoItem(const QJsonObject &obj)
{
    VideoItem item;
    item.bvid = obj.value("bvid").toString();
    item.title = obj.value("title").toString();
    item.pic = obj.value("pic").toString();
    if (item.pic.isEmpty()) item.pic = obj.value("cover").toString();
    item.desc = obj.value("desc").toString();

    // duration 可能是数字字符串，或 mm:ss / hh:mm:ss
    auto parseDurationString = [](const QString &durationStr) -> int {
        QString s = durationStr.trimmed();
        if (s.contains(':')) {
            QStringList parts = s.split(':');
            if (parts.size() == 2) {
                return parts[0].toInt() * 60 + parts[1].toInt();
            } else if (parts.size() == 3) {
                return parts[0].toInt() * 3600 + parts[1].toInt() * 60 + parts[2].toInt();
            }
            return 0;
        }
        return s.toInt();
    };

    if (obj.value("duration").isString()) {
        item.duration = parseDurationString(obj.value("duration").toString());
    } else {
        item.duration = obj.value("duration").toInt();
    }

    if (item.duration <= 0 && obj.value("length").isString()) {
        item.duration = parseDurationString(obj.value("length").toString());
    }

    item.cid = obj.value("cid").toVariant().toLongLong();
    item.pubdate = obj.value("pubdate").toVariant().toLongLong();

    // 分P数量（列表接口通常为 videos 字段，可能是字符串）
    int videos = obj.value("videos").toVariant().toInt();
    if (videos > 0) {
        item.partCount = videos;
    } else {
        // 有些接口返回 pages 数组或 pages 数值
        QJsonArray pages = obj.value("pages").toArray();
        if (!pages.isEmpty()) {
            item.partCount = pages.size();
        } else {
            int pagesCount = obj.value("pages").toVariant().toInt();
            if (pagesCount > 0) item.partCount = pagesCount;
        }
    }

    QJsonObject owner = obj.value("owner").toObject();
    item.ownerName = owner.value("name").toString();
    if (item.ownerName.isEmpty()) item.ownerName = obj.value("author").toString();
    item.ownerFace = owner.value("face").toString();
    item.ownerMid = owner.value("mid").toVariant().toLongLong();

    QJsonObject stat = obj.value("stat").toObject();
    item.views = stat.value("view").toVariant().toLongLong();
    item.danmaku = stat.value("danmaku").toVariant().toLongLong();
    item.likes = stat.value("like").toVariant().toLongLong();
    item.coins = stat.value("coin").toVariant().toLongLong();
    item.favorites = stat.value("favorite").toVariant().toLongLong();
    item.replies = stat.value("reply").toVariant().toLongLong();

    auto parseCountString = [](const QString &s) -> qint64 {
        QString t = s;
        t.remove(',');
        if (t.contains("万")) {
            bool ok = false;
            double v = t.left(t.indexOf("万")).toDouble(&ok);
            return ok ? static_cast<qint64>(v * 10000) : 0;
        }
        if (t.contains("亿")) {
            bool ok = false;
            double v = t.left(t.indexOf("亿")).toDouble(&ok);
            return ok ? static_cast<qint64>(v * 100000000) : 0;
        }
        bool ok = false;
        qint64 v = t.toLongLong(&ok);
        return ok ? v : 0;
    };

    // 搜索结果常用字段: play / video_review / view
    if (item.views <= 0) {
        if (obj.value("play").isString()) {
            item.views = parseCountString(obj.value("play").toString());
        } else {
            item.views = obj.value("play").toVariant().toLongLong();
        }
    }
    if (item.views <= 0) {
        item.views = obj.value("view").toVariant().toLongLong();
    }
    if (item.danmaku <= 0) {
        item.danmaku = obj.value("video_review").toVariant().toLongLong();
    }

    QJsonObject rcmd = obj.value("rcmd_reason").toObject();
    item.rcmdReason = rcmd.value("content").toString();

    return item;
}

// ============ CommentListModel ============

CommentListModel::CommentListModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_loading(false)
    , m_totalCount(0)
{
}

int CommentListModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_items.count();
}

QVariant CommentListModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_items.count())
        return QVariant();

    const CommentItem &item = m_items[index.row()];

    switch (role) {
    case RpidRole:     return item.rpid;
    case UserNameRole: return item.userName;
    case AvatarRole:   return item.avatar;
    case LevelRole:    return item.level;
    case ContentRole:  return item.content;
    case LikesRole:    return item.likes;
    case RcountRole:   return item.rcount;
    case CtimeRole:    return item.ctime;
    case CtimeTextRole: return formatTime(item.ctime);
    case IsVipRole:    return item.isVip;
    case IsTopRole:    return item.isTop;
    default: return QVariant();
    }
}

QHash<int, QByteArray> CommentListModel::roleNames() const
{
    return {
        {RpidRole, "rpid"},
        {UserNameRole, "userName"},
        {AvatarRole, "avatar"},
        {LevelRole, "level"},
        {ContentRole, "content"},
        {LikesRole, "likes"},
        {RcountRole, "rcount"},
        {CtimeRole, "ctime"},
        {CtimeTextRole, "ctimeText"},
        {IsVipRole, "isVip"},
        {IsTopRole, "isTop"}
    };
}

int CommentListModel::count() const { return m_items.count(); }
bool CommentListModel::loading() const { return m_loading; }
int CommentListModel::totalCount() const { return m_totalCount; }
QString CommentListModel::errorMessage() const { return m_errorMessage; }

void CommentListModel::clear()
{
    beginResetModel();
    m_items.clear();
    m_errorMessage.clear();
    endResetModel();
    emit countChanged();
    emit errorMessageChanged();
}

void CommentListModel::appendItems(const QVector<CommentItem> &items)
{
    if (items.isEmpty()) return;
    beginInsertRows(QModelIndex(), m_items.count(),
                    m_items.count() + items.count() - 1);
    m_items.append(items);
    endInsertRows();
    emit countChanged();
}

void CommentListModel::setLoading(bool loading)
{
    if (m_loading != loading) {
        m_loading = loading;
        emit loadingChanged();
    }
}

void CommentListModel::setTotalCount(int total)
{
    if (m_totalCount != total) {
        m_totalCount = total;
        emit totalCountChanged();
    }
}

void CommentListModel::setErrorMessage(const QString &msg)
{
    if (m_errorMessage != msg) {
        m_errorMessage = msg;
        emit errorMessageChanged();
    }
}

CommentItem CommentListModel::parseCommentItem(const QJsonObject &obj)
{
    CommentItem item;
    item.rpid = obj.value("rpid").toVariant().toLongLong();
    item.ctime = obj.value("ctime").toVariant().toLongLong();
    item.likes = obj.value("like").toVariant().toLongLong();
    item.rcount = obj.value("rcount").toVariant().toLongLong();

    QJsonObject member = obj.value("member").toObject();
    item.userName = member.value("uname").toString();
    item.avatar = member.value("avatar").toString();

    QJsonObject levelInfo = member.value("level_info").toObject();
    item.level = levelInfo.value("current_level").toInt();

    QJsonObject vip = member.value("vip").toObject();
    item.isVip = (vip.value("vipStatus").toInt() == 1);

    QJsonObject content = obj.value("content").toObject();
    item.content = content.value("message").toString();

    item.isTop = obj.value("is_top").toInt(0) == 1 || obj.value("is_top").toBool(false);

    return item;
}

QString CommentListModel::formatTime(qint64 timestamp)
{
    QDateTime dt = QDateTime::fromSecsSinceEpoch(timestamp);
    QDateTime now = QDateTime::currentDateTime();
    qint64 diff = now.toSecsSinceEpoch() - timestamp;

    if (diff < 60) return "刚刚";
    if (diff < 3600) return QString("%1 分钟前").arg(diff / 60);
    if (diff < 86400) return QString("%1 小时前").arg(diff / 3600);
    if (diff < 2592000) return QString("%1 天前").arg(diff / 86400);

    return dt.toString("yyyy-MM-dd");
}

// ============ CommentReplyListModel ============

CommentReplyListModel::CommentReplyListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int CommentReplyListModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_items.count();
}

QVariant CommentReplyListModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_items.count())
        return QVariant();

    const CommentReplyItem &item = m_items[index.row()];

    switch (role) {
    case RpidRole: return item.rpid;
    case UserNameRole: return item.userName;
    case AvatarRole: return item.avatar;
    case LevelRole: return item.level;
    case ContentRole: return item.content;
    case LikesRole: return item.likes;
    case CtimeRole: return item.ctime;
    case CtimeTextRole: return CommentListModel::formatTime(item.ctime);
    case IsVipRole: return item.isVip;
    default: return QVariant();
    }
}

QHash<int, QByteArray> CommentReplyListModel::roleNames() const
{
    return {
        {RpidRole, "rpid"},
        {UserNameRole, "userName"},
        {AvatarRole, "avatar"},
        {LevelRole, "level"},
        {ContentRole, "content"},
        {LikesRole, "likes"},
        {CtimeRole, "ctime"},
        {CtimeTextRole, "ctimeText"},
        {IsVipRole, "isVip"}
    };
}

int CommentReplyListModel::count() const { return m_items.count(); }
bool CommentReplyListModel::loading() const { return m_loading; }
QString CommentReplyListModel::errorMessage() const { return m_errorMessage; }

void CommentReplyListModel::clear()
{
    beginResetModel();
    m_items.clear();
    m_errorMessage.clear();
    endResetModel();
    emit countChanged();
    emit errorMessageChanged();
}

void CommentReplyListModel::setItems(const QVector<CommentReplyItem> &items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
    emit countChanged();
}

void CommentReplyListModel::appendItems(const QVector<CommentReplyItem> &items)
{
    if (items.isEmpty()) return;
    beginInsertRows(QModelIndex(), m_items.count(), m_items.count() + items.count() - 1);
    m_items.append(items);
    endInsertRows();
    emit countChanged();
}

void CommentReplyListModel::setLoading(bool loading)
{
    if (m_loading != loading) {
        m_loading = loading;
        emit loadingChanged();
    }
}

void CommentReplyListModel::setErrorMessage(const QString &msg)
{
    if (m_errorMessage != msg) {
        m_errorMessage = msg;
        emit errorMessageChanged();
    }
}

CommentReplyItem CommentReplyListModel::parseCommentReplyItem(const QJsonObject &obj)
{
    CommentReplyItem item;
    item.rpid = obj.value("rpid").toVariant().toLongLong();
    item.ctime = obj.value("ctime").toVariant().toLongLong();
    item.likes = obj.value("like").toVariant().toLongLong();

    QJsonObject member = obj.value("member").toObject();
    item.userName = member.value("uname").toString();
    item.avatar = member.value("avatar").toString();

    QJsonObject levelInfo = member.value("level_info").toObject();
    item.level = levelInfo.value("current_level").toInt();

    QJsonObject vip = member.value("vip").toObject();
    item.isVip = (vip.value("vipStatus").toInt() == 1);

    QJsonObject content = obj.value("content").toObject();
    item.content = content.value("message").toString();

    return item;
}

// ============ HotSearchModel ============

HotSearchModel::HotSearchModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_loading(false)
{
}

int HotSearchModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_items.count();
}

QVariant HotSearchModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_items.count())
        return QVariant();

    const HotSearchItem &item = m_items[index.row()];

    switch (role) {
    case KeywordRole: return item.keyword;
    case IconRole:    return item.icon;
    case PositionRole: return item.position;
    default: return QVariant();
    }
}

QHash<int, QByteArray> HotSearchModel::roleNames() const
{
    return {
        {KeywordRole, "keyword"},
        {IconRole, "icon"},
        {PositionRole, "position"}
    };
}

int HotSearchModel::count() const { return m_items.count(); }
bool HotSearchModel::loading() const { return m_loading; }

void HotSearchModel::clear()
{
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
}

void HotSearchModel::setItems(const QVector<HotSearchItem> &items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
    emit countChanged();
}

void HotSearchModel::setLoading(bool loading)
{
    if (m_loading != loading) {
        m_loading = loading;
        emit loadingChanged();
    }
}

// ============ SearchResultModel ============

SearchResultModel::SearchResultModel(QObject *parent)
    : VideoListModel(parent)
{
}

QString SearchResultModel::keyword() const { return m_keyword; }

void SearchResultModel::setKeyword(const QString &keyword)
{
    if (m_keyword != keyword) {
        m_keyword = keyword;
        emit keywordChanged();
    }
}

void SearchResultModel::clear()
{
    VideoListModel::clear(); // 调用基类的 clear
    setKeyword("");          // 清空关键词
}

// ============ VideoPartListModel ============

VideoPartListModel::VideoPartListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int VideoPartListModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_items.count();
}

QVariant VideoPartListModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_items.count())
        return QVariant();

    const VideoPartItem &item = m_items[index.row()];

    switch (role) {
    case CidRole:      return item.cid;
    case PageRole:     return item.page;
    case PartRole:     return item.part;
    case DurationRole: return item.duration;
    case DurationTextRole: return VideoListModel::formatDuration(item.duration);
    default: return QVariant();
    }
}

QHash<int, QByteArray> VideoPartListModel::roleNames() const
{
    return {
        {CidRole, "cid"},
        {PageRole, "page"},
        {PartRole, "part"},
        {DurationRole, "duration"},
        {DurationTextRole, "durationText"}
    };
}

int VideoPartListModel::count() const { return m_items.count(); }

void VideoPartListModel::clear()
{
    if (m_items.isEmpty()) return;
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
}

void VideoPartListModel::setItems(const QVector<VideoPartItem> &items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
    emit countChanged();
}

VideoPartItem VideoPartListModel::parseVideoPartItem(const QJsonObject &obj)
{
    VideoPartItem item;
    item.cid = obj.value("cid").toVariant().toLongLong();
    item.page = obj.value("page").toInt();
    item.part = obj.value("part").toString();
    item.duration = obj.value("duration").toInt();
    return item;
}

// ============ FavoriteFolderModel ============

FavoriteFolderModel::FavoriteFolderModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_loading(false)
{
}

int FavoriteFolderModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_items.count();
}

QVariant FavoriteFolderModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_items.count())
        return QVariant();

    const FavoriteFolderItem &item = m_items[index.row()];

    switch (role) {
    case IdRole: return item.id;
    case FidRole: return item.fid;
    case TitleRole: return item.title;
    case CoverRole: return item.cover;
    case MediaCountRole: return item.mediaCount;
    case IntroRole: return item.intro;
    case AttrRole: return item.attr;
    default: return QVariant();
    }
}

QHash<int, QByteArray> FavoriteFolderModel::roleNames() const
{
    return {
        {IdRole, "id"},
        {FidRole, "fid"},
        {TitleRole, "title"},
        {CoverRole, "cover"},
        {MediaCountRole, "mediaCount"},
        {IntroRole, "intro"},
        {AttrRole, "attr"}
    };
}

int FavoriteFolderModel::count() const { return m_items.count(); }

bool FavoriteFolderModel::loading() const { return m_loading; }

void FavoriteFolderModel::clear()
{
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
}

void FavoriteFolderModel::setItems(const QVector<FavoriteFolderItem> &items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
    emit countChanged();
}

void FavoriteFolderModel::setLoading(bool loading)
{
    if (m_loading != loading) {
        m_loading = loading;
        emit loadingChanged();
    }
}

void FavoriteFolderModel::updateCover(qint64 id, const QString &cover)
{
    if (cover.isEmpty()) return;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].id == id || m_items[i].fid == id) {
            if (m_items[i].cover == cover) return;
            m_items[i].cover = cover;
            QModelIndex idx = index(i, 0);
            emit dataChanged(idx, idx, {CoverRole});
            return;
        }
    }
}

FavoriteFolderItem FavoriteFolderModel::parseFavoriteFolderItem(const QJsonObject &obj)
{
    FavoriteFolderItem item;
    item.id = obj.value("id").toVariant().toLongLong();
    item.fid = obj.value("fid").toVariant().toLongLong();
    if (item.id == 0) item.id = item.fid;
    if (item.fid == 0) item.fid = item.id;
    item.title = obj.value("title").toString();
    item.cover = obj.value("cover").toString();
    item.mediaCount = obj.value("media_count").toInt();
    item.intro = obj.value("intro").toString();
    item.attr = obj.value("attr").toInt();
    return item;
}
