package main

import (
	"bufio"
	"bytes"
	"context"
	"crypto/md5"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"html"
	"io"
	"net"
	"net/http"
	"net/url"
	"os"
	"os/signal"
	"path/filepath"
	"runtime/debug"
	"sort"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"golang.org/x/time/rate"
)

// ==================== 日志工具 ====================

const (
	colorReset   = "\x1b[0m"
	colorBright  = "\x1b[1m"
	colorDim     = "\x1b[2m"
	colorRed     = "\x1b[31m"
	colorGreen   = "\x1b[32m"
	colorYellow  = "\x1b[33m"
	colorBlue    = "\x1b[34m"
	colorMagenta = "\x1b[35m"
	colorCyan    = "\x1b[36m"

	asyncLogDefaultQueueSize = 1024
	asyncLogMaxQueueSize     = 65536
	asyncLogBufferSize       = 64 * 1024
	asyncLogFlushSize        = 32 * 1024
	asyncLogFlushInterval    = 200 * time.Millisecond
	asyncLogShutdownTimeout  = 2 * time.Second
)

var (
	debugEnabled     = os.Getenv("DEBUG") == "true"
	accessLogEnabled = debugEnabled || os.Getenv("ACCESS_LOG") == "true"
	asyncLogCh       = make(chan string, asyncLogQueueSize())
	asyncLogFlushCh  = make(chan chan struct{})
	asyncLogDropped  atomic.Uint64
	wbiValueReplacer = strings.NewReplacer("!", "", "'", "", "(", "", ")", "", "*", "")
)

func init() {
	go asyncLogWriter()
}

func getTimestamp() string {
	return time.Now().Format("2006-01-02 15:04:05")
}

func asyncLogQueueSize() int {
	raw := strings.TrimSpace(os.Getenv("LOG_QUEUE_SIZE"))
	if raw == "" {
		return asyncLogDefaultQueueSize
	}

	size, err := strconv.Atoi(raw)
	if err != nil || size <= 0 {
		return asyncLogDefaultQueueSize
	}
	if size > asyncLogMaxQueueSize {
		return asyncLogMaxQueueSize
	}
	return size
}

func asyncLogWriter() {
	writer := bufio.NewWriterSize(os.Stdout, asyncLogBufferSize)
	ticker := time.NewTicker(asyncLogFlushInterval)
	defer ticker.Stop()

	writeDropped := func() {
		if dropped := asyncLogDropped.Swap(0); dropped > 0 {
			_, _ = fmt.Fprintf(writer, "%s[WARN]%s %s%s%s 日志队列已满，丢弃 %d 条日志\n",
				colorYellow, colorReset, colorDim, getTimestamp(), colorReset, dropped)
		}
	}

	writeLine := func(line string) {
		writeDropped()
		_, _ = writer.WriteString(line)
		if writer.Buffered() >= asyncLogFlushSize {
			_ = writer.Flush()
		}
	}

	flush := func() {
		writeDropped()
		_ = writer.Flush()
	}

	for {
		select {
		case line := <-asyncLogCh:
			writeLine(line)
		case done := <-asyncLogFlushCh:
			for {
				select {
				case line := <-asyncLogCh:
					writeLine(line)
				default:
					flush()
					close(done)
					done = nil
				}
				if done == nil {
					break
				}
			}
		case <-ticker.C:
			flush()
		}
	}
}

func enqueueLogLine(line string) {
	select {
	case asyncLogCh <- line:
	default:
		asyncLogDropped.Add(1)
	}
}

func asyncLogFlush(timeout time.Duration) bool {
	done := make(chan struct{})
	timer := time.NewTimer(timeout)
	defer timer.Stop()

	select {
	case asyncLogFlushCh <- done:
	case <-timer.C:
		return false
	}

	select {
	case <-done:
		return true
	case <-timer.C:
		return false
	}
}

func logPlain(message string) {
	enqueueLogLine(message + "\n")
}

func logInfo(message string, args ...interface{}) {
	if !debugEnabled {
		return
	}
	msg := fmt.Sprintf(message, args...)
	enqueueLogLine(fmt.Sprintf("%s[INFO]%s %s%s%s %s\n", colorCyan, colorReset, colorDim, getTimestamp(), colorReset, msg))
}

func logSuccess(message string, args ...interface{}) {
	msg := fmt.Sprintf(message, args...)
	enqueueLogLine(fmt.Sprintf("%s[SUCCESS]%s %s%s%s %s\n", colorGreen, colorReset, colorDim, getTimestamp(), colorReset, msg))
}

func logWarn(message string, args ...interface{}) {
	msg := fmt.Sprintf(message, args...)
	enqueueLogLine(fmt.Sprintf("%s[WARN]%s %s%s%s %s\n", colorYellow, colorReset, colorDim, getTimestamp(), colorReset, msg))
}

func logError(message string, args ...interface{}) {
	msg := fmt.Sprintf(message, args...)
	enqueueLogLine(fmt.Sprintf("%s[ERROR]%s %s%s%s %s\n", colorRed, colorReset, colorDim, getTimestamp(), colorReset, msg))
}

func logDebug(message string, args ...interface{}) {
	if !debugEnabled {
		return
	}
	msg := fmt.Sprintf(message, args...)
	enqueueLogLine(fmt.Sprintf("%s[DEBUG]%s %s%s%s %s\n", colorMagenta, colorReset, colorDim, getTimestamp(), colorReset, msg))
}

func logRequest(method, path string, params map[string]string) {
	paramStr := ""
	if accessLogEnabled && len(params) > 0 {
		b, _ := json.Marshal(params)
		paramStr = string(b)
	}
	enqueueLogLine(fmt.Sprintf("%s[REQUEST]%s %s%s%s %s%s%s %s %s%s%s\n",
		colorBlue, colorReset, colorDim, getTimestamp(), colorReset,
		colorBright, method, colorReset, path, colorDim, paramStr, colorReset))
}

func logResponse(path string, code int, duration time.Duration) {
	if !accessLogEnabled && code == 0 {
		return
	}
	color := colorGreen
	if code != 0 {
		color = colorRed
	}
	enqueueLogLine(fmt.Sprintf("%s[RESPONSE]%s %s%s%s %s %scode=%d%s %s(%dms)%s\n",
		color, colorReset, colorDim, getTimestamp(), colorReset,
		path, color, code, colorReset, colorDim, duration.Milliseconds(), colorReset))
}

// ==================== 配置常量 ====================

const (
	APPKEY = "1d8b6e7d45233436"
	APPSEC = "560c52ccd288fed045859ed18bffd973"
)

var MIXIN_KEY_ENC_TAB = []int{
	46, 47, 18, 2, 53, 8, 23, 32, 15, 50, 10, 31, 58, 3, 45, 35, 27, 43, 5, 49,
	33, 9, 42, 19, 29, 28, 14, 39, 12, 38, 41, 13, 37, 48, 7, 16, 24, 55, 40,
	61, 26, 17, 0, 1, 60, 51, 30, 4, 22, 25, 54, 21, 56, 59, 6, 63, 57, 62, 11,
	36, 20, 34, 44, 52,
}

const (
	defaultUserAgent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
	defaultReferer   = "https://www.bilibili.com/"
)

var rootEndpoints = []string{
	"/popular - 热门视频",
	"/ranking - 排行榜",
	"/search - 搜索视频",
	"/video/info - 视频详情",
	"/video/related - 视频相关推荐",
	"/video/playurl - 播放地址",
	"/video/player/info - 播放器信息",
	"/video/danmaku - 弹幕",
	"/video/comments - 评论",
	"/video/comments/replies - 子评论",
	"/video/subtitle/list - CC字幕列表",
	"/video/subtitle/ass - 生成ASS字幕",
	"/user/info - 用户信息",
	"/user/follow/toggle - 关注/取消关注UP主",
	"/user/videos - 用户投稿",
	"/user/seasons - UP主合集列表",
	"/user/season/videos - UP主合集内视频",
	"/login/info - 登录信息",
	"/login/import - 导入登录Cookie",
	"/hot/search - 热搜",
	"/recommend - 首页推荐",
	"/history/recent - 最近观看",
	"/toview/list - 稍后再看列表",
	"/toview/add - 添加稍后再看",
	"/toview/del - 取消稍后再看",
	"/player/heartbeat - 回调心跳",
	"/fav/folder/list - 收藏夹列表",
	"/fav/resource/list - 收藏夹内容",
	"/fav/status - 收藏状态",
	"/fav/toggle - 收藏切换",
	"/coin/status - 投币状态",
	"/coin/add - 投币",
	"/like/status - 点赞状态",
	"/like/toggle - 点赞/取消点赞",
	"/qrcode/generate - 生成登录二维码",
	"/qrcode/poll - 轮询二维码状态",
}

var startupEndpoints = []string{
	"GET  /popular                - 热门视频",
	"GET  /ranking                - 排行榜",
	"GET  /search                 - 搜索视频",
	"GET  /video/info             - 视频详情",
	"GET  /video/related          - 视频相关推荐",
	"GET  /video/playurl          - 播放地址",
	"GET  /video/player/info      - 播放器信息",
	"GET  /video/danmaku          - 弹幕数据",
	"GET  /video/comments         - 评论列表",
	"GET  /video/comments/replies - 子评论",
	"GET  /video/subtitle/list    - CC字幕列表",
	"GET  /video/subtitle/ass     - 生成ASS字幕",
	"GET  /user/info              - 用户信息",
	"GET  /user/follow/toggle     - 关注/取消关注UP主",
	"GET  /user/videos            - 用户投稿",
	"GET  /user/seasons           - UP主合集列表",
	"GET  /user/season/videos     - UP主合集内视频",
	"GET  /login/info             - 登录信息",
	"POST /login/import           - 导入登录Cookie",
	"GET  /hot/search             - 热搜榜",
	"GET  /recommend              - 首页推荐",
	"GET  /history/recent         - 最近观看",
	"GET  /toview/list            - 稍后再看列表",
	"GET  /toview/add             - 添加稍后再看",
	"GET  /toview/del             - 取消稍后再看",
	"GET  /player/heartbeat       - 回调心跳",
	"GET  /fav/folder/list        - 收藏夹列表",
	"GET  /fav/resource/list      - 收藏夹内容",
	"GET  /fav/status             - 收藏状态",
	"GET  /fav/toggle             - 收藏切换",
	"GET  /coin/status            - 投币状态",
	"GET  /coin/add               - 投币",
	"GET  /like/status            - 点赞状态",
	"GET  /like/toggle            - 点赞/取消点赞",
	"GET  /qrcode/generate        - 生成登录二维码",
	"GET  /qrcode/poll            - 轮询二维码状态",
}

// ==================== 工具函数 ====================

func getMixinKey(orig string) string {
	return mixinKeyConcat(orig, "")
}

// mixinKeyConcat 直接基于两个字符串视图按 MIXIN_KEY_ENC_TAB 挑选最多 32 字节，
// 既避免了 `imgKey + subKey` 这次 64 字节左右的拼接分配，也用栈上 [32]byte
// 替代了 strings.Builder。结果与 "原拼接后取 i 字节、最后截到 32" 完全等价。
func mixinKeyConcat(a, b string) string {
	var buf [32]byte
	n := 0
	total := len(a) + len(b)
	for _, idx := range MIXIN_KEY_ENC_TAB {
		if idx >= total {
			continue
		}
		var ch byte
		if idx < len(a) {
			ch = a[idx]
		} else {
			ch = b[idx-len(a)]
		}
		buf[n] = ch
		n++
		if n == 32 {
			break
		}
	}
	return string(buf[:n])
}

func md5Hash(s string) string {
	return md5HashBytes([]byte(s))
}

// md5HashBytes 用 md5.Sum 直接拿到 [16]byte，再 hex.Encode 到栈上 [32]byte，
// 最后只产生一次 string 分配；旧版 hex.EncodeToString(h.Sum(nil)) 会多一次
// []byte 分配，并且 md5.New() 是堆对象。
func md5HashBytes(b []byte) string {
	sum := md5.Sum(b)
	var dst [32]byte
	hex.Encode(dst[:], sum[:])
	return string(dst[:])
}

// keysPool 复用 sortedKeysInto 用到的 []string，避免每次签名/查询都 alloc。
var keysPool = sync.Pool{
	New: func() any {
		s := make([]string, 0, 16)
		return &s
	},
}

// queryBufPool 复用查询构造与签名拼接用到的 bytes.Buffer。
// 这里不用 strings.Builder：Builder.Reset 会丢掉底层 buffer，无法真正复用，
// bytes.Buffer.Reset 只把长度归零，能重复利用底层数组。
var queryBufPool = sync.Pool{
	New: func() any { return new(bytes.Buffer) },
}

func acquireQueryBuf() *bytes.Buffer {
	return queryBufPool.Get().(*bytes.Buffer)
}

func releaseQueryBuf(b *bytes.Buffer) {
	if b.Cap() > 16<<10 {
		// 防止偶发的大 payload 让池长期持有大内存
		return
	}
	b.Reset()
	queryBufPool.Put(b)
}

func releaseKeys(p *[]string) {
	if cap(*p) > 64 {
		// 容量过大不放回池，避免拖累其它请求
		return
	}
	*p = (*p)[:0]
	keysPool.Put(p)
}

// sortedKeysInto 把 params 的 key 排序后写入 *p 指向的切片，必要时扩容。
// 调用方必须把 p 释放回 keysPool（用 releaseKeys）。
func sortedKeysInto(p *[]string, params map[string]string) []string {
	s := *p
	if cap(s) < len(params) {
		s = make([]string, 0, len(params))
	} else {
		s = s[:0]
	}
	for k := range params {
		s = append(s, k)
	}
	sort.Strings(s)
	*p = s
	return s
}

// writeSortedQuery 按已排序好的 keys，把 url.QueryEscape 后的 k=v&... 写入 buf。
func writeSortedQuery(buf *bytes.Buffer, params map[string]string, keys []string) {
	for i, k := range keys {
		if i > 0 {
			buf.WriteByte('&')
		}
		buf.WriteString(url.QueryEscape(k))
		buf.WriteByte('=')
		buf.WriteString(url.QueryEscape(params[k]))
	}
}

func buildSortedQuery(params map[string]string) string {
	if len(params) == 0 {
		return ""
	}
	pk := keysPool.Get().(*[]string)
	keys := sortedKeysInto(pk, params)
	buf := acquireQueryBuf()
	writeSortedQuery(buf, params, keys)
	out := buf.String()
	releaseQueryBuf(buf)
	releaseKeys(pk)
	return out
}

func encWbi(params map[string]string, imgKey, subKey string) map[string]string {
	mixinKey := mixinKeyConcat(imgKey, subKey)
	params["wts"] = strconv.FormatInt(time.Now().Unix(), 10)

	pk := keysPool.Get().(*[]string)
	keys := sortedKeysInto(pk, params)

	filtered := make(map[string]string, len(params)+1)
	buf := acquireQueryBuf()
	for i, k := range keys {
		v := wbiValueReplacer.Replace(params[k])
		filtered[k] = v
		if i > 0 {
			buf.WriteByte('&')
		}
		buf.WriteString(url.QueryEscape(k))
		buf.WriteByte('=')
		buf.WriteString(url.QueryEscape(v))
	}
	// 直接把 mixinKey 追加到 buffer，避免 builder.String() + mixinKey
	// 这次额外的 string 分配（query 串可能上百字节）。
	buf.WriteString(mixinKey)
	wbiSign := md5HashBytes(buf.Bytes())

	releaseQueryBuf(buf)
	releaseKeys(pk)

	filtered["w_rid"] = wbiSign

	if debugEnabled && len(mixinKey) >= 8 && len(wbiSign) >= 8 {
		logDebug("WBI 签名计算 mixinKey=%s... w_rid=%s...", mixinKey[:8], wbiSign[:8])
	}

	return filtered
}

func appSign(params map[string]string) map[string]string {
	params["appkey"] = APPKEY

	pk := keysPool.Get().(*[]string)
	keys := sortedKeysInto(pk, params)
	buf := acquireQueryBuf()
	writeSortedQuery(buf, params, keys)
	// 同 encWbi：直接在原 buffer 上拼接 APPSEC，避免再分配一个 string。
	buf.WriteString(APPSEC)
	sign := md5HashBytes(buf.Bytes())

	releaseQueryBuf(buf)
	releaseKeys(pk)

	params["sign"] = sign
	return params
}

func maskSensitive(value string) string {
	if value == "" {
		return ""
	}
	if len(value) <= 8 {
		return value
	}
	return value[:4] + "..." + value[len(value)-4:]
}

func extractResponseCodeFromHead(head []byte) int {
	if len(head) == 0 {
		return -1
	}

	pattern := []byte(`"code"`)
	idx := bytes.Index(head, pattern)
	if idx < 0 {
		return -1
	}

	rest := head[idx+len(pattern):]
	colon := bytes.IndexByte(rest, ':')
	if colon < 0 {
		return -1
	}
	rest = bytes.TrimSpace(rest[colon+1:])
	if len(rest) == 0 {
		return -1
	}

	pos := 0
	sign := 1
	if rest[0] == '-' {
		sign = -1
		pos = 1
	}
	if pos >= len(rest) || rest[pos] < '0' || rest[pos] > '9' {
		return -1
	}
	// 内联整数解析，避免 strconv.Atoi 接受 string 时产生的 []byte→string 拷贝。
	val := 0
	for pos < len(rest) && rest[pos] >= '0' && rest[pos] <= '9' {
		// 业务 code 都在 ±10^6 量级，1<<30 已足够防御异常输入造成的溢出
		if val > 1<<30 {
			return -1
		}
		val = val*10 + int(rest[pos]-'0')
		pos++
	}
	return val * sign
}

func isJSONContentType(contentType string) bool {
	contentType = strings.ToLower(strings.TrimSpace(contentType))
	if contentType == "" {
		return false
	}
	return strings.Contains(contentType, "application/json") || strings.Contains(contentType, "+json")
}

func limitLogBody(body []byte) string {
	const maxLen = 300
	s := string(body)
	if len(s) <= maxLen {
		return s
	}
	return s[:maxLen] + "..."
}

func logUpstreamBusinessError(apiURL string, body []byte) {
	code := extractResponseCodeFromHead(body)
	if code == -1 || code == 0 {
		return
	}

	msg := ""
	if debugEnabled || code != 0 {
		var resp struct {
			Message string `json:"message"`
			Msg     string `json:"msg"`
		}
		if err := json.Unmarshal(body, &resp); err == nil {
			msg = resp.Message
			if msg == "" {
				msg = resp.Msg
			}
		}
	}

	logWarn("上游业务错误 url=%s code=%d message=%s body=%s", apiURL, code, msg, limitLogBody(body))
}

func extractKeyFromURL(rawURL string) string {
	if rawURL == "" {
		return ""
	}
	parts := strings.Split(rawURL, "/")
	filename := parts[len(parts)-1]
	dotParts := strings.Split(filename, ".")
	return dotParts[0]
}

func (c *BilibiliClient) loadSnapshot() *clientSnapshot {
	s := c.snapshot.Load()
	if s == nil {
		return &emptyClientSnapshot
	}
	return s
}

// rebuildSnapshotLocked 必须在持有 c.mu 写锁时调用：根据当前字段构造新的快照
// 并通过 atomic.Pointer 原子发布，使读路径立即看到最新值。
func (c *BilibiliClient) rebuildSnapshotLocked() {
	s := &clientSnapshot{
		sessdata:        c.sessdata,
		buvid3:          c.buvid3,
		biliJct:         c.biliJct,
		dedeUserID:      c.dedeUserID,
		dedeUserIDCkMd5: c.dedeUserIDCkMd5,
		refreshToken:    c.refreshToken,
		biliTicket:      c.biliTicket,
		imgKey:          c.imgKey,
		subKey:          c.subKey,
	}
	s.cookie = buildCookieString(s)
	c.snapshot.Store(s)
}

func (c *BilibiliClient) rebuildSnapshot() {
	c.mu.Lock()
	c.rebuildSnapshotLocked()
	c.mu.Unlock()
}

// buildCookieString 根据快照字段拼出 Cookie 头值。各字段为空时跳过。
// 这一步在写路径完成，读路径直接读结果，省掉每次请求的 6 段拼接 + Join。
func buildCookieString(s *clientSnapshot) string {
	// 估算容量，避免 strings.Builder 内部多次扩容
	estimate := 0
	add := func(name, value string) {
		if value == "" {
			return
		}
		if estimate > 0 {
			estimate += 2 // "; "
		}
		estimate += len(name) + 1 + len(value)
	}
	add("SESSDATA", s.sessdata)
	add("buvid3", s.buvid3)
	add("bili_jct", s.biliJct)
	add("DedeUserID", s.dedeUserID)
	add("DedeUserID__ckMd5", s.dedeUserIDCkMd5)
	add("bili_ticket", s.biliTicket)
	if estimate == 0 {
		return ""
	}

	var sb strings.Builder
	sb.Grow(estimate)
	first := true
	appendC := func(name, value string) {
		if value == "" {
			return
		}
		if !first {
			sb.WriteString("; ")
		} else {
			first = false
		}
		sb.WriteString(name)
		sb.WriteByte('=')
		sb.WriteString(value)
	}
	appendC("SESSDATA", s.sessdata)
	appendC("buvid3", s.buvid3)
	appendC("bili_jct", s.biliJct)
	appendC("DedeUserID", s.dedeUserID)
	appendC("DedeUserID__ckMd5", s.dedeUserIDCkMd5)
	appendC("bili_ticket", s.biliTicket)
	return sb.String()
}

// loadCookieStore 把磁盘持久化的 CookieStore 写入 client，并立即发布快照。
// 仅在 main() 启动时由单线程调用一次。
func (c *BilibiliClient) loadCookieStore(cs CookieStore) {
	c.mu.Lock()
	c.sessdata = cs.Sessdata
	c.buvid3 = cs.Buvid3
	c.biliJct = cs.BiliJct
	c.dedeUserID = cs.DedeUserID
	c.dedeUserIDCkMd5 = cs.DedeUserIDCkMd5
	c.refreshToken = cs.RefreshToken
	c.rebuildSnapshotLocked()
	c.mu.Unlock()
}

func (c *BilibiliClient) getWbiKeys() (string, string) {
	s := c.loadSnapshot()
	return s.imgKey, s.subKey
}

func (c *BilibiliClient) setWbiKeys(imgKey, subKey string) {
	c.mu.Lock()
	c.imgKey = imgKey
	c.subKey = subKey
	c.rebuildSnapshotLocked()
	c.mu.Unlock()
}

func (c *BilibiliClient) getBiliTicket() string {
	return c.loadSnapshot().biliTicket
}

func (c *BilibiliClient) setBiliTicket(ticket string) {
	c.mu.Lock()
	c.biliTicket = ticket
	c.rebuildSnapshotLocked()
	c.mu.Unlock()
}

func (c *BilibiliClient) UpdateAuth(sessdata, buvid3, biliJct, refreshToken, dedeUserID, dedeUserIDCkMd5 string) {
	c.mu.Lock()

	changed := false
	if sessdata != "" && sessdata != c.sessdata {
		c.sessdata = sessdata
		changed = true
	}
	if buvid3 != "" && buvid3 != c.buvid3 {
		c.buvid3 = buvid3
		changed = true
	}
	if biliJct != "" && biliJct != c.biliJct {
		c.biliJct = biliJct
		changed = true
	}
	if dedeUserID != "" && dedeUserID != c.dedeUserID {
		c.dedeUserID = dedeUserID
		changed = true
	}
	if dedeUserIDCkMd5 != "" && dedeUserIDCkMd5 != c.dedeUserIDCkMd5 {
		c.dedeUserIDCkMd5 = dedeUserIDCkMd5
		changed = true
	}
	if refreshToken != "" && refreshToken != c.refreshToken {
		c.refreshToken = refreshToken
		changed = true
	}

	// 在锁内先快照，磁盘 IO 放到锁外，避免 saveCookies 期间阻塞所有读路径
	var snapshot CookieStore
	if changed {
		c.rebuildSnapshotLocked()
		snapshot = CookieStore{
			Sessdata:        c.sessdata,
			Buvid3:          c.buvid3,
			BiliJct:         c.biliJct,
			DedeUserID:      c.dedeUserID,
			DedeUserIDCkMd5: c.dedeUserIDCkMd5,
			RefreshToken:    c.refreshToken,
		}
	}
	c.mu.Unlock()

	if !changed {
		return
	}
	if err := saveCookies(snapshot); err != nil {
		logWarn("Cookie 持久化失败: %s", err.Error())
	} else {
		logInfo("已更新并持久化全局 Cookie")
	}
}

// 清理登录状态（仅在明确认证失效时调用）
func (c *BilibiliClient) ClearAuth() {
	c.mu.Lock()
	c.sessdata = ""
	c.buvid3 = ""
	c.biliJct = ""
	c.dedeUserID = ""
	c.dedeUserIDCkMd5 = ""
	c.refreshToken = ""
	c.rebuildSnapshotLocked()
	c.mu.Unlock()

	if err := saveCookies(CookieStore{}); err != nil {
		logWarn("清理 Cookie 持久化失败: %s", err.Error())
	} else {
		logInfo("已清理登录状态并持久化")
	}
}

// ==================== Bilibili API 客户端 ====================

// clientSnapshot 是 BilibiliClient 在某一时刻的只读快照，包含每次请求都需要读
// 的认证字段、WBI keys、bili_ticket，以及预先拼接好的 Cookie 字符串。
// 写路径在持有 BilibiliClient.mu 的情况下重建快照后通过 atomic.Pointer 原子替换；
// 读路径完全无锁，避免 setHeaders/buildCookie/getWbiKeys 在每次上游请求都做
// RWMutex.RLock 与字符串 Join。
type clientSnapshot struct {
	sessdata        string
	buvid3          string
	biliJct         string
	dedeUserID      string
	dedeUserIDCkMd5 string
	refreshToken    string
	biliTicket      string
	imgKey          string
	subKey          string
	cookie          string // 预先拼好的 Cookie 头值，供 setHeaders 直接使用
}

// emptyClientSnapshot 在 atomic.Pointer 还未首次发布时充当 zero-value 视图，
// 让 loadSnapshot 始终返回非 nil 指针，省掉每次读路径的 nil 检查分支。
var emptyClientSnapshot clientSnapshot

type BilibiliClient struct {
	mu              sync.RWMutex
	readLimiter     *rate.Limiter
	writeLimiter    *rate.Limiter
	sessdata        string
	buvid3          string
	biliJct         string
	dedeUserID      string
	dedeUserIDCkMd5 string
	refreshToken    string
	biliTicket      string
	imgKey          string
	subKey          string
	httpClient      *http.Client
	snapshot        atomic.Pointer[clientSnapshot]
}

// waitBeforeUpstream 对上游请求做分桶令牌桶限速：
//   - GET 读取类请求：稳态 ~33 rps（每 30ms 一个令牌），burst 5，
//     让同一页面的聚合读请求在空闲后能并发放出，而不是被串成 30ms 阶梯。
//   - 非 GET 写请求：稳态 ~5 rps（每 200ms 一个令牌），burst 2，降低触发风控概率。
//
// 令牌桶长期平均速率与旧漏桶一致，仅新增突发额度；ctx 取消时立即返回。
func (c *BilibiliClient) waitBeforeUpstream(ctx context.Context, method string) error {
	lim := c.readLimiter
	if method != http.MethodGet {
		lim = c.writeLimiter
	}
	return lim.Wait(ctx)
}

func NewBilibiliClient(sessdata, buvid3 string) *BilibiliClient {
	// 自定义 Transport：限制空闲连接、对每个 host 的并发，避免连接堆积/泄漏
	transport := &http.Transport{
		Proxy: http.ProxyFromEnvironment,
		DialContext: (&net.Dialer{
			Timeout:   8 * time.Second,
			KeepAlive: 30 * time.Second,
		}).DialContext,
		ForceAttemptHTTP2:     true,
		MaxIdleConns:          100,
		MaxIdleConnsPerHost:   16,
		MaxConnsPerHost:       64,
		IdleConnTimeout:       90 * time.Second,
		TLSHandshakeTimeout:   10 * time.Second,
		ExpectContinueTimeout: 1 * time.Second,
		ResponseHeaderTimeout: 10 * time.Second,
	}
	c := &BilibiliClient{
		sessdata:     sessdata,
		buvid3:       buvid3,
		readLimiter:  rate.NewLimiter(rate.Every(30*time.Millisecond), 5),
		writeLimiter: rate.NewLimiter(rate.Every(200*time.Millisecond), 2),
		httpClient: &http.Client{
			Timeout:   15 * time.Second,
			Transport: transport,
		},
	}
	// 发布初始快照，确保所有读路径在第一次请求前就能拿到非空 snapshot。
	c.rebuildSnapshot()
	return c
}

func (c *BilibiliClient) Init() {
	logInfo("初始化 Bilibili 客户端...")
	c.setBiliTicket(getBiliTicket())
	c.updateWbiKeys()
	c.ensureBuvid3()
	c.refreshCookies()
	logSuccess("客户端初始化完成")
}

// buildCookie 直接读取已经预拼接好的快照 cookie 串，
// 避免每次请求都做 RLock + 6 段拼接 + strings.Join。
func (c *BilibiliClient) buildCookie() string {
	return c.loadSnapshot().cookie
}

func (c *BilibiliClient) requireLogin() (string, string, error) {
	sessdata, _, biliJct, _, _, _ := c.getAuth()
	if sessdata == "" || biliJct == "" {
		return "", "", fmt.Errorf("登录信息不完整")
	}
	return sessdata, biliJct, nil
}

func (c *BilibiliClient) requireRiskAuth() (string, string, string, error) {
	sessdata, buvid3, biliJct, _, _, _ := c.getAuth()
	if sessdata == "" || biliJct == "" {
		return "", "", "", fmt.Errorf("登录信息不完整")
	}
	if buvid3 == "" {
		return "", "", "", fmt.Errorf("缺少 buvid3，按文档该字段异常会触发风控")
	}
	return sessdata, buvid3, biliJct, nil
}

func (c *BilibiliClient) setHeaders(req *http.Request) {
	h := req.Header
	h.Set("User-Agent", defaultUserAgent)
	h.Set("Referer", defaultReferer)
	if cookie := c.loadSnapshot().cookie; cookie != "" {
		h.Set("Cookie", cookie)
	}
}

func (c *BilibiliClient) updateWbiKeys() {
	logInfo("正在更新 WBI Keys...")

	req, err := http.NewRequest("GET", "https://api.bilibili.com/x/web-interface/nav", nil)
	if err != nil {
		logError("WBI Keys 请求创建失败: %s", err.Error())
		c.setDefaultWbiKeys()
		return
	}
	c.setHeaders(req)

	resp, err := c.httpClient.Do(req)
	if err != nil {
		logError("WBI Keys 更新异常: %s", err.Error())
		c.setDefaultWbiKeys()
		return
	}
	defer resp.Body.Close()

	body, _ := io.ReadAll(resp.Body)
	var result map[string]interface{}
	if err := json.Unmarshal(body, &result); err != nil {
		logError("WBI Keys 解析失败: %s", err.Error())
		c.setDefaultWbiKeys()
		return
	}

	code, _ := result["code"].(float64)
	if int(code) != 0 {
		msg, _ := result["message"].(string)
		logWarn("WBI Keys 获取失败，使用默认值. 错误: %s", msg)
		c.setDefaultWbiKeys()
		return
	}

	data, _ := result["data"].(map[string]interface{})
	wbiImg, _ := data["wbi_img"].(map[string]interface{})
	imgURL, _ := wbiImg["img_url"].(string)
	subURL, _ := wbiImg["sub_url"].(string)

	imgKey := extractKeyFromURL(imgURL)
	subKey := extractKeyFromURL(subURL)
	c.setWbiKeys(imgKey, subKey)

	imgKeyDisplay := imgKey
	if len(imgKeyDisplay) > 8 {
		imgKeyDisplay = imgKeyDisplay[:8] + "..."
	}
	subKeyDisplay := subKey
	if len(subKeyDisplay) > 8 {
		subKeyDisplay = subKeyDisplay[:8] + "..."
	}
	logSuccess("WBI Keys 更新成功: imgKey=%s subKey=%s", imgKeyDisplay, subKeyDisplay)
}

func (c *BilibiliClient) setDefaultWbiKeys() {
	c.setWbiKeys("7cd084941338484aae1ad9425b84077c", "4932caff0ff746eab6f01bf08b70ac45")
}

func (c *BilibiliClient) ensureBuvid3() {
	_, buvid3, _, _, _, _ := c.getAuth()
	if buvid3 != "" {
		return
	}

	logInfo("buvid3 缺失，尝试获取...")
	req, err := http.NewRequest("GET", "https://www.bilibili.com/", nil)
	if err != nil {
		logWarn("buvid3 请求创建失败: %s", err.Error())
		return
	}
	c.setHeaders(req)

	resp, err := c.httpClient.Do(req)
	if err != nil {
		logWarn("buvid3 请求失败: %s", err.Error())
		return
	}
	defer resp.Body.Close()

	newBuvid3 := ""
	for _, ck := range resp.Cookies() {
		if ck.Name == "buvid3" {
			newBuvid3 = ck.Value
			break
		}
	}
	if newBuvid3 != "" {
		c.UpdateAuth("", newBuvid3, "", "", "", "")
		logInfo("buvid3 获取成功")
	} else {
		logWarn("buvid3 获取失败，响应未包含该字段")
	}
}

// 仅启动时刷新一次，避免高频刷新触发风控
func (c *BilibiliClient) startCookieRefreshLoop() {}

func (c *BilibiliClient) doRequest(ctx context.Context, apiURL string, params map[string]string, method string) (json.RawMessage, error) {
	startTime := time.Now()
	query := buildSortedQuery(params)
	fullURL := apiURL
	var bodyReader io.Reader

	switch method {
	case http.MethodGet:
		if query != "" {
			fullURL += "?" + query
		}
	case http.MethodPost:
		bodyReader = strings.NewReader(query)
	default:
		return nil, fmt.Errorf("不支持的请求方法: %s", method)
	}

	req, err := http.NewRequestWithContext(ctx, method, fullURL, bodyReader)
	if err != nil {
		return nil, err
	}
	c.setHeaders(req)
	if method == http.MethodPost {
		req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	}

	if err := c.waitBeforeUpstream(ctx, method); err != nil {
		return nil, err
	}
	resp, err := c.httpClient.Do(req)
	duration := time.Since(startTime)
	if err != nil {
		logError("API 请求失败 (%dms): %s", duration.Milliseconds(), err.Error())
		return nil, err
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, err
	}

	logDebug("API 响应 %s %s (%dms)", method, apiURL, duration.Milliseconds())
	logUpstreamBusinessError(apiURL, body)
	return json.RawMessage(body), nil
}

func (c *BilibiliClient) wbiRequest(ctx context.Context, apiURL string, params map[string]string, method string) (json.RawMessage, error) {
	imgKey, subKey := c.getWbiKeys()
	logDebug("WBI 请求 %s %s", method, apiURL)
	// encWbi 会向 params 中写入 wts 并返回新的 filtered map，
	// 调用方传入的 map 都是当次请求新建的临时 map，无需 copyMap 复制保护。
	return c.doRequest(ctx, apiURL, encWbi(params, imgKey, subKey), method)
}

func (c *BilibiliClient) request(ctx context.Context, apiURL string, params map[string]string, method string) (json.RawMessage, error) {
	logDebug("普通请求 %s %s", method, apiURL)
	return c.doRequest(ctx, apiURL, params, method)
}

func (c *BilibiliClient) rawRequest(ctx context.Context, apiURL string, params map[string]string) ([]byte, error) {
	query := buildSortedQuery(params)
	fullURL := apiURL
	if query != "" {
		if strings.Contains(fullURL, "?") {
			fullURL = fullURL + "&" + query
		} else {
			fullURL = fullURL + "?" + query
		}
	}

	req, err := http.NewRequestWithContext(ctx, "GET", fullURL, nil)
	if err != nil {
		return nil, err
	}
	c.setHeaders(req)

	if err := c.waitBeforeUpstream(ctx, http.MethodGet); err != nil {
		return nil, err
	}
	resp, err := c.httpClient.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	return io.ReadAll(resp.Body)
}

func (c *BilibiliClient) rawGetURL(ctx context.Context, fullURL string) ([]byte, error) {
	req, err := http.NewRequestWithContext(ctx, "GET", fullURL, nil)
	if err != nil {
		return nil, err
	}
	c.setHeaders(req)

	if err := c.waitBeforeUpstream(ctx, http.MethodGet); err != nil {
		return nil, err
	}
	resp, err := c.httpClient.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	return io.ReadAll(resp.Body)
}

func (c *BilibiliClient) webPost(ctx context.Context, apiURL string, params map[string]string) (json.RawMessage, error) {
	query := buildSortedQuery(params)

	var lastErr error
	for attempt := 1; attempt <= 2; attempt++ {
		req, err := http.NewRequestWithContext(ctx, "POST", apiURL, strings.NewReader(query))
		if err != nil {
			return nil, err
		}
		// setHeaders 已经写入 User-Agent / Referer / Cookie，
		// 这里只补 Origin、Content-Type、X-Requested-With 三项 webPost 专属头。
		c.setHeaders(req)
		req.Header.Set("Content-Type", "application/x-www-form-urlencoded; charset=UTF-8")
		req.Header.Set("Origin", "https://www.bilibili.com")
		req.Header.Set("X-Requested-With", "XMLHttpRequest")

		if err := c.waitBeforeUpstream(ctx, http.MethodPost); err != nil {
			return nil, err
		}
		resp, err := c.httpClient.Do(req)
		if err != nil {
			lastErr = err
			logWarn("webPost 请求失败 attempt=%d url=%s err=%s", attempt, apiURL, err.Error())
			if attempt < 2 {
				select {
				case <-time.After(300 * time.Millisecond):
				case <-ctx.Done():
					return nil, ctx.Err()
				}
				continue
			}
			return nil, err
		}

		body, readErr := io.ReadAll(resp.Body)
		resp.Body.Close()
		if readErr != nil {
			lastErr = readErr
			logWarn("webPost 读取响应失败 attempt=%d url=%s err=%s", attempt, apiURL, readErr.Error())
			if attempt < 2 {
				select {
				case <-time.After(300 * time.Millisecond):
				case <-ctx.Done():
					return nil, ctx.Err()
				}
				continue
			}
			return nil, readErr
		}

		if resp.StatusCode == http.StatusForbidden {
			sessdata, buvid3, biliJct, refreshToken, _, _ := c.getAuth()
			logWarn("webPost 命中403 attempt=%d url=%s sessdata=%t buvid3=%t bili_jct=%t refresh_token=%t body=%s",
				attempt,
				apiURL,
				sessdata != "",
				buvid3 != "",
				biliJct != "",
				refreshToken != "",
				string(body))
			lastErr = fmt.Errorf("HTTP 403 Forbidden")
			if attempt < 2 {
				select {
				case <-time.After(300 * time.Millisecond):
				case <-ctx.Done():
					return nil, ctx.Err()
				}
				continue
			}
			return nil, lastErr
		}

		if resp.StatusCode >= 400 {
			logWarn("webPost 非成功状态 attempt=%d url=%s status=%d body=%s", attempt, apiURL, resp.StatusCode, string(body))
			lastErr = fmt.Errorf("HTTP %d", resp.StatusCode)
			if attempt < 2 {
				select {
				case <-time.After(300 * time.Millisecond):
				case <-ctx.Done():
					return nil, ctx.Err()
				}
				continue
			}
			return nil, lastErr
		}

		logUpstreamBusinessError(apiURL, body)
		if debugEnabled && extractResponseCodeFromHead(body) == -1 {
			logDebug("webPost 非JSON响应 body=%s", string(body))
		}

		return body, nil
	}

	if lastErr != nil {
		return nil, lastErr
	}
	return nil, fmt.Errorf("webPost unknown error")
}

// ==================== API 方法 ====================

func (c *BilibiliClient) GetPopularVideos(ctx context.Context, pn, ps int) (json.RawMessage, error) {
	logInfo("获取热门视频 pn=%d ps=%d", pn, ps)
	return c.wbiRequest(ctx, "https://api.bilibili.com/x/web-interface/popular", map[string]string{
		"pn": strconv.Itoa(pn),
		"ps": strconv.Itoa(ps),
	}, "GET")
}

func (c *BilibiliClient) GetRanking(ctx context.Context, rid int, typ string) (json.RawMessage, error) {
	logInfo("获取排行榜 rid=%d type=%s", rid, typ)
	return c.wbiRequest(ctx, "https://api.bilibili.com/x/web-interface/ranking/v2", map[string]string{
		"rid":  strconv.Itoa(rid),
		"type": typ,
	}, "GET")
}

func (c *BilibiliClient) SearchVideos(ctx context.Context, keyword string, page, pageSize int) (json.RawMessage, error) {
	logInfo("搜索视频 keyword=\"%s\" page=%d", keyword, page)
	return c.wbiRequest(ctx, "https://api.bilibili.com/x/web-interface/wbi/search/type", map[string]string{
		"search_type": "video",
		"keyword":     keyword,
		"page":        strconv.Itoa(page),
		"page_size":   strconv.Itoa(pageSize),
	}, "GET")
}

func (c *BilibiliClient) GetVideoInfo(ctx context.Context, aid int, bvid string) (json.RawMessage, error) {
	logInfo("获取视频信息 aid=%d bvid=%s", aid, bvid)
	params := map[string]string{}
	if aid > 0 {
		params["aid"] = strconv.Itoa(aid)
	}
	if bvid != "" {
		params["bvid"] = bvid
	}
	return c.wbiRequest(ctx, "https://api.bilibili.com/x/web-interface/wbi/view", params, "GET")
}

func (c *BilibiliClient) GetVideoRelated(ctx context.Context, aid int, bvid string) (json.RawMessage, error) {
	logInfo("获取视频相关推荐 aid=%d bvid=%s", aid, bvid)
	params := map[string]string{}
	if aid > 0 {
		params["aid"] = strconv.Itoa(aid)
	}
	if bvid != "" {
		params["bvid"] = bvid
	}
	raw, err := c.request(ctx, "https://api.bilibili.com/x/web-interface/archive/related", params, "GET")
	if err != nil {
		return nil, err
	}

	var resp map[string]interface{}
	if err := json.Unmarshal(raw, &resp); err == nil {
		if arr, ok := resp["data"].([]interface{}); ok {
			resp["data"] = map[string]interface{}{"list": arr}
			if merged, mErr := json.Marshal(resp); mErr == nil {
				return merged, nil
			}
		}
	}
	return raw, nil
}

func (c *BilibiliClient) GetVideoComments(ctx context.Context, oid, typ, sortVal, ps, pn int) (json.RawMessage, error) {
	logInfo("获取评论 oid=%d type=%d sort=%d", oid, typ, sortVal)
	return c.request(ctx, "https://api.bilibili.com/x/v2/reply", map[string]string{
		"type": strconv.Itoa(typ),
		"oid":  strconv.Itoa(oid),
		"sort": strconv.Itoa(sortVal),
		"ps":   strconv.Itoa(ps),
		"pn":   strconv.Itoa(pn),
	}, "GET")
}

func (c *BilibiliClient) GetCommentReplies(ctx context.Context, oid, root, typ, ps, pn int) (json.RawMessage, error) {
	logInfo("获取子评论 oid=%d root=%d type=%d", oid, root, typ)
	return c.request(ctx, "https://api.bilibili.com/x/v2/reply/reply", map[string]string{
		"type": strconv.Itoa(typ),
		"oid":  strconv.Itoa(oid),
		"root": strconv.Itoa(root),
		"ps":   strconv.Itoa(ps),
		"pn":   strconv.Itoa(pn),
	}, "GET")
}

func (c *BilibiliClient) GetUserInfo(ctx context.Context, mid int) (json.RawMessage, error) {
	logInfo("获取用户信息 mid=%d", mid)

	baseRaw, err := c.wbiRequest(ctx, "https://api.bilibili.com/x/space/wbi/acc/info", map[string]string{
		"mid": strconv.Itoa(mid),
	}, "GET")
	if err != nil {
		return nil, err
	}

	var baseResp map[string]interface{}
	if err := json.Unmarshal(baseRaw, &baseResp); err != nil {
		return nil, err
	}

	if code, ok := baseResp["code"].(float64); ok && int(code) != 0 {
		return baseRaw, nil
	}

	dataObj, _ := baseResp["data"].(map[string]interface{})
	if dataObj == nil {
		dataObj = map[string]interface{}{}
		baseResp["data"] = dataObj
	}

	// 补充粉丝/关注统计
	relRaw, err := c.request(ctx, "https://api.bilibili.com/x/relation/stat", map[string]string{
		"vmid": strconv.Itoa(mid),
	}, "GET")
	if err == nil {
		var relResp map[string]interface{}
		if json.Unmarshal(relRaw, &relResp) == nil {
			if relCode, ok := relResp["code"].(float64); ok && int(relCode) == 0 {
				if relData, ok := relResp["data"].(map[string]interface{}); ok {
					if follower, ok := relData["follower"]; ok {
						dataObj["follower"] = follower
					}
					if following, ok := relData["following"]; ok {
						dataObj["following"] = following
					}
				}
			}
		}
	}

	// 补充当前登录用户与该 UP 的关注关系。查询自己的信息时不需要打该接口，
	// 否则登录后初始化会额外增加一次上游请求，更容易触发风控。
	sessdata, _, _, _, dedeUserID, _ := c.getAuth()
	if sessdata != "" && dedeUserID != strconv.Itoa(mid) {
		relationRaw, err := c.GetUserRelation(ctx, mid)
		if err == nil {
			var relationResp map[string]interface{}
			if json.Unmarshal(relationRaw, &relationResp) == nil {
				if relationCode, ok := relationResp["code"].(float64); ok && int(relationCode) == 0 {
					if relationData, ok := relationResp["data"].(map[string]interface{}); ok {
						attribute := 0
						if v, ok := relationData["attribute"].(float64); ok {
							attribute = int(v)
						}
						isFollowing := attribute == 2 || attribute == 6 || attribute == 128 || attribute == 129 || attribute == 130
						dataObj["relation_attribute"] = attribute
						dataObj["is_following"] = isFollowing
					}
				}
			}
		}
	}

	merged, err := json.Marshal(baseResp)
	if err != nil {
		return nil, err
	}
	return merged, nil
}

func (c *BilibiliClient) GetUserRelation(ctx context.Context, mid int) (json.RawMessage, error) {
	logInfo("获取用户关系 mid=%d", mid)
	return c.request(ctx, "https://api.bilibili.com/x/relation", map[string]string{
		"fid": strconv.Itoa(mid),
	}, "GET")
}

func (c *BilibiliClient) ToggleUserFollow(ctx context.Context, mid int, follow bool) (json.RawMessage, error) {
	_, biliJct, err := c.requireLogin()
	if err != nil {
		return nil, err
	}

	act := "2"
	if follow {
		act = "1"
	}

	logInfo("切换关注状态 mid=%d follow=%t", mid, follow)
	return c.webPost(ctx, "https://api.bilibili.com/x/relation/modify", map[string]string{
		"fid":        strconv.Itoa(mid),
		"act":        act,
		"re_src":     "11",
		"csrf":       biliJct,
		"csrf_token": biliJct,
	})
}

// GetUserVideosApp 使用 APP 游标接口获取投稿。
// 注意：该接口实际使用 aid 作为游标参数（不是时间戳）。
// cursorAid=0 表示首次；cursorAid>0 表示从“上一页最后一个视频的 aid”继续向后翻页。
func (c *BilibiliClient) GetUserVideosApp(ctx context.Context, mid int, cursorAid int64, ps int, includeCursor bool) (json.RawMessage, error) {
	if ps <= 0 {
		ps = 20
	}
	if ps > 30 {
		ps = 30
	}
	logInfo("获取用户投稿(APP) mid=%d aid=%d ps=%d", mid, cursorAid, ps)

	params := map[string]string{
		"vmid": strconv.Itoa(mid),
		// 该接口是游标分页：用 aid 翻页更稳定，pn 固定为 1
		"pn":       "1",
		"ps":       strconv.Itoa(ps),
		"order":    "pubdate",
		"platform": "android",
		"mobi_app": "android",
		"device":   "android",
		"ts":       strconv.FormatInt(time.Now().Unix(), 10),
	}
	if cursorAid > 0 {
		params["aid"] = strconv.FormatInt(cursorAid, 10)
	}
	if includeCursor {
		params["include_cursor"] = "true"
	}
	params = appSign(params)
	raw, err := c.request(ctx, "https://app.biliapi.com/x/v2/space/archive/cursor", params, "GET")
	if err != nil {
		return nil, err
	}

	// 修正偶发返回列表顺序异常（旧稿件在前）的问题
	var resp map[string]interface{}
	if err := json.Unmarshal(raw, &resp); err == nil {
		if code, ok := resp["code"].(float64); ok && int(code) == 0 {
			data, _ := resp["data"].(map[string]interface{})
			if data != nil {
				if data["last_watched_locator"] != nil {
					return raw, nil
				}
				list, listKey := data["item"], "item"
				if list == nil {
					list = data["archives"]
					listKey = "archives"
				}
				if arr, ok := list.([]interface{}); ok && len(arr) > 1 {
					sort.SliceStable(arr, func(i, j int) bool {
						getTime := func(v interface{}) int64 {
							obj, _ := v.(map[string]interface{})
							if obj == nil {
								return 0
							}
							if t, ok := obj["pubdate"].(float64); ok {
								return int64(t)
							}
							if t, ok := obj["ctime"].(float64); ok {
								return int64(t)
							}
							if t, ok := obj["created"].(float64); ok {
								return int64(t)
							}
							if arc, ok := obj["archive"].(map[string]interface{}); ok {
								if t, ok := arc["pubdate"].(float64); ok {
									return int64(t)
								}
								if t, ok := arc["ctime"].(float64); ok {
									return int64(t)
								}
								if t, ok := arc["created"].(float64); ok {
									return int64(t)
								}
							}
							return 0
						}
						return getTime(arr[i]) > getTime(arr[j])
					})
					data[listKey] = arr
					resp["data"] = data
					if merged, mErr := json.Marshal(resp); mErr == nil {
						return merged, nil
					}
				}
			}
		}
	}

	return raw, nil
}

func (c *BilibiliClient) GetUserVideos(ctx context.Context, mid, pn, ps int, max, targetAid int64, includeCursor bool) (json.RawMessage, error) {
	logInfo("获取用户投稿 mid=%d pn=%d ps=%d max=%d targetAid=%d includeCursor=%t", mid, pn, ps, max, targetAid, includeCursor)

	// 经验：网页端 x/space/wbi/arc/search 更容易触发 -412 / 风控页，
	// 且 pn 翻页在 UP 有新稿件插入时更容易出现重复/缺失。
	// 因此这里默认使用 APP 游标接口 x/v2/space/archive/cursor。
	//
	// 前端分页策略：
	// - 首次请求 max=0
	// - 加载更多时传入上一次返回的 cursor.next/max 作为 max
	//
	// 兼容 pn>1（但未给 max）的情况：通过多次游标前进到目标页。
	if pn < 1 {
		pn = 1
	}
	if targetAid > 0 {
		return c.GetUserVideosApp(ctx, mid, targetAid, ps, includeCursor)
	}

	cursor := max
	var raw json.RawMessage
	var err error

	// 如果没有提供 cursor，但要求 pn>1，则先把 cursor 向后推进 (pn-1) 页
	// 该接口的 cursor 实际为 aid：即“上一页最后一个视频的 aid”（通常在 item[].param 字段）。
	if cursor <= 0 && pn > 1 {
		for i := 1; i < pn; i++ {
			raw, err = c.GetUserVideosApp(ctx, mid, cursor, ps, false)
			if err != nil {
				return nil, err
			}
			var resp map[string]interface{}
			if json.Unmarshal(raw, &resp) != nil {
				break
			}
			code, _ := resp["code"].(float64)
			if int(code) != 0 {
				// 业务错误直接返回，让上层 wrapResult 透出 message
				return raw, nil
			}
			data, _ := resp["data"].(map[string]interface{})
			if data == nil {
				break
			}
			// 优先从 data.cursor 读取（若存在）
			if cursorObj, ok := data["cursor"].(map[string]interface{}); ok && cursorObj != nil {
				if nextF, ok := cursorObj["next"].(float64); ok && int64(nextF) > 0 {
					cursor = int64(nextF)
				} else if maxF, ok := cursorObj["max"].(float64); ok && int64(maxF) > 0 {
					cursor = int64(maxF)
				}
			}

			// 兜底：从 item 最后一条的 param(=aid) 提取下一页游标
			if cursor <= 0 {
				if arr, ok := data["item"].([]interface{}); ok && len(arr) > 0 {
					if last, ok := arr[len(arr)-1].(map[string]interface{}); ok {
						if p, ok := last["param"].(string); ok {
							if v, e := strconv.ParseInt(p, 10, 64); e == nil {
								cursor = v
							}
						}
					}
				}
			}

			if cursor <= 0 {
				break
			}
		}
	}

	// 取目标页
	raw, err = c.GetUserVideosApp(ctx, mid, cursor, ps, false)
	if err != nil {
		return nil, err
	}
	return raw, nil
}

func metaInt64(obj map[string]interface{}, key string) int64 {
	v, ok := obj[key]
	if !ok || v == nil {
		return 0
	}
	switch x := v.(type) {
	case float64:
		return int64(x)
	case string:
		n, _ := strconv.ParseInt(x, 10, 64)
		return n
	case int:
		return int64(x)
	case int64:
		return x
	default:
		return 0
	}
}

func filterListByMetaMid(list interface{}, mid int64, key string) interface{} {
	items, ok := list.([]interface{})
	if !ok || mid <= 0 {
		return list
	}
	filtered := make([]interface{}, 0, len(items))
	for _, item := range items {
		obj, _ := item.(map[string]interface{})
		meta, _ := obj["meta"].(map[string]interface{})
		metaMid := metaInt64(meta, "mid")
		if metaMid > 0 && metaMid != mid {
			logWarn("过滤归属不匹配的%s条目 request_mid=%d meta_mid=%d", key, mid, metaMid)
			continue
		}
		filtered = append(filtered, item)
	}
	return filtered
}

func filterSeasonsSeriesByMid(raw json.RawMessage, mid int) json.RawMessage {
	var resp map[string]interface{}
	if err := json.Unmarshal(raw, &resp); err != nil {
		return raw
	}
	if code, ok := resp["code"].(float64); ok && int(code) != 0 {
		return raw
	}
	data, _ := resp["data"].(map[string]interface{})
	itemsLists, _ := data["items_lists"].(map[string]interface{})
	if itemsLists == nil {
		return raw
	}

	requestMid := int64(mid)
	itemsLists["seasons_list"] = filterListByMetaMid(itemsLists["seasons_list"], requestMid, "seasons_list")
	itemsLists["series_list"] = filterListByMetaMid(itemsLists["series_list"], requestMid, "series_list")

	merged, err := json.Marshal(resp)
	if err != nil {
		return raw
	}
	return merged
}

func seasonMetaMismatchError(message string, requestMid int, requestSeasonID int, metaMid int64, metaSeasonID int64) json.RawMessage {
	body, _ := json.Marshal(map[string]interface{}{
		"code":    -1,
		"message": message,
		"data": map[string]interface{}{
			"request_mid":       requestMid,
			"request_season_id": requestSeasonID,
			"meta_mid":          metaMid,
			"meta_season_id":    metaSeasonID,
		},
	})
	return body
}

func validateSeasonArchivesMeta(raw json.RawMessage, mid, seasonID int) json.RawMessage {
	var resp map[string]interface{}
	if err := json.Unmarshal(raw, &resp); err != nil {
		return raw
	}
	if code, ok := resp["code"].(float64); ok && int(code) != 0 {
		return raw
	}
	data, _ := resp["data"].(map[string]interface{})
	meta, _ := data["meta"].(map[string]interface{})
	if meta == nil {
		return raw
	}
	metaMid := metaInt64(meta, "mid")
	metaSeasonID := metaInt64(meta, "season_id")
	if metaMid > 0 && metaMid != int64(mid) {
		logWarn("合集归属 mid 不匹配 request_mid=%d meta_mid=%d season_id=%d", mid, metaMid, seasonID)
		return seasonMetaMismatchError("合集归属不匹配", mid, seasonID, metaMid, metaSeasonID)
	}
	if metaSeasonID > 0 && metaSeasonID != int64(seasonID) {
		logWarn("合集 season_id 不匹配 request_season=%d meta_season=%d mid=%d", seasonID, metaSeasonID, mid)
		return seasonMetaMismatchError("合集ID不匹配", mid, seasonID, metaMid, metaSeasonID)
	}
	return raw
}

// 获取 UP 主的合集与系列列表（合集 = seasons，系列 = series）
// 文档接口：/x/polymer/web-space/seasons_series_list，返回 data.items_lists.{seasons_list, series_list, page}。
func (c *BilibiliClient) GetUserSeasonsSeries(ctx context.Context, mid, pn, ps int) (json.RawMessage, error) {
	logInfo("获取 UP 合集/系列列表 mid=%d pn=%d ps=%d", mid, pn, ps)
	raw, err := c.wbiRequest(ctx, "https://api.bilibili.com/x/polymer/web-space/seasons_series_list", map[string]string{
		"mid":          strconv.Itoa(mid),
		"page_num":     strconv.Itoa(pn),
		"page_size":    strconv.Itoa(ps),
		"web_location": "333.999",
	}, "GET")
	if err != nil {
		return nil, err
	}
	return filterSeasonsSeriesByMid(raw, mid), nil
}

// 获取 UP 主合集内的视频列表
func (c *BilibiliClient) GetUserSeasonVideos(ctx context.Context, mid, seasonID, pn, ps int, sortReverse bool) (json.RawMessage, error) {
	logInfo("获取 UP 合集视频 mid=%d season=%d pn=%d ps=%d", mid, seasonID, pn, ps)
	sr := "false"
	if sortReverse {
		sr = "true"
	}
	raw, err := c.wbiRequest(ctx, "https://api.bilibili.com/x/polymer/web-space/seasons_archives_list", map[string]string{
		"mid":          strconv.Itoa(mid),
		"season_id":    strconv.Itoa(seasonID),
		"sort_reverse": sr,
		"page_num":     strconv.Itoa(pn),
		"page_size":    strconv.Itoa(ps),
		"web_location": "333.999",
	}, "GET")
	if err != nil {
		return nil, err
	}
	return validateSeasonArchivesMeta(raw, mid, seasonID), nil
}

func (c *BilibiliClient) GetLoginInfo(ctx context.Context) (json.RawMessage, error) {
	logInfo("获取登录信息")
	return c.request(ctx, "https://api.bilibili.com/x/web-interface/nav", map[string]string{}, "GET")
}

func (c *BilibiliClient) GetPlayerV2(ctx context.Context, aid, cid int, bvid string) (json.RawMessage, error) {
	params := map[string]string{
		"aid": strconv.Itoa(aid),
		"cid": strconv.Itoa(cid),
	}
	if bvid != "" {
		params["bvid"] = bvid
	}
	return c.wbiRequest(ctx, "https://api.bilibili.com/x/player/wbi/v2", params, "GET")
}

func (c *BilibiliClient) GetPlayUrl(ctx context.Context, aid, cid, qn, fnval int, platform string) (json.RawMessage, error) {
	if qn <= 0 {
		qn = 32
	}
	if fnval <= 0 {
		fnval = 4048
	}
	if platform == "" {
		platform = "pc"
	}

	allowedQn := map[int]bool{
		16:  true,
		32:  true,
		64:  true,
		80:  true,
		112: true,
		116: true,
		120: true,
		125: true,
	}
	if !allowedQn[qn] {
		logWarn("不支持的清晰度 qn=%d，回退到 32", qn)
		qn = 32
	}

	logInfo("获取播放地址 aid=%d cid=%d qn=%d fnval=%d platform=%s", aid, cid, qn, fnval, platform)
	return c.wbiRequest(ctx, "https://api.bilibili.com/x/player/wbi/playurl", map[string]string{
		"avid":     strconv.Itoa(aid),
		"cid":      strconv.Itoa(cid),
		"qn":       strconv.Itoa(qn),
		"type":     "",
		"otype":    "json",
		"fourk":    "1",
		"fnver":    "0",
		"fnval":    strconv.Itoa(fnval),
		"platform": platform,
	}, "GET")
}

func (c *BilibiliClient) GetDanmaku(ctx context.Context, cid, segment int) ([]byte, error) {
	logInfo("获取弹幕 cid=%d segment=%d", cid, segment)
	return c.rawRequest(ctx, "https://api.bilibili.com/x/v2/dm/web/seg.so", map[string]string{
		"type":          "1",
		"oid":           strconv.Itoa(cid),
		"segment_index": strconv.Itoa(segment),
	})
}

func (c *BilibiliClient) GetDanmakuConfig(ctx context.Context, oid int, pid int) (json.RawMessage, error) {
	logInfo("获取弹幕配置 oid=%d pid=%d", oid, pid)
	params := map[string]string{
		"type": "1",
		"oid":  strconv.Itoa(oid),
	}
	if pid > 0 {
		params["pid"] = strconv.Itoa(pid)
	}
	return c.request(ctx, "https://api.bilibili.com/x/v2/dm/web/view", params, "GET")
}

func (c *BilibiliClient) GetHotSearch(ctx context.Context, limit int) (json.RawMessage, error) {
	logInfo("获取热搜 limit=%d", limit)
	return c.request(ctx, "https://api.bilibili.com/x/web-interface/search/square", map[string]string{
		"limit": strconv.Itoa(limit),
	}, "GET")
}

func (c *BilibiliClient) GetRecommend(ctx context.Context, freshType int) (json.RawMessage, error) {
	logInfo("获取推荐 feed fresh_type=%d", freshType)
	return c.wbiRequest(ctx, "https://api.bilibili.com/x/web-interface/wbi/index/top/feed/rcmd", map[string]string{
		"fresh_type": strconv.Itoa(freshType),
	}, "GET")
}

func (c *BilibiliClient) GetRecentHistory(ctx context.Context, max, viewAt, ps int) (json.RawMessage, error) {
	logInfo("获取最近观看 max=%d view_at=%d ps=%d", max, viewAt, ps)
	params := map[string]string{}
	if max > 0 {
		params["max"] = strconv.Itoa(max)
	}
	if viewAt > 0 {
		params["view_at"] = strconv.Itoa(viewAt)
	}
	if ps > 0 {
		params["ps"] = strconv.Itoa(ps)
	}
	params["business"] = "archive"
	return c.request(ctx, "https://api.bilibili.com/x/web-interface/history/cursor", params, "GET")
}

func (c *BilibiliClient) GetWatchLaterList(ctx context.Context, pn, ps int) (json.RawMessage, error) {
	if pn <= 0 {
		pn = 1
	}
	if ps <= 0 {
		ps = 20
	}
	if ps > 50 {
		ps = 50
	}
	logInfo("获取稍后再看 pn=%d ps=%d", pn, ps)
	params := map[string]string{
		"pn": strconv.Itoa(pn),
		"ps": strconv.Itoa(ps),
	}
	return c.request(ctx, "https://api.bilibili.com/x/v2/history/toview", params, "GET")
}

func (c *BilibiliClient) AddToWatchLater(ctx context.Context, aid int, bvid string) (json.RawMessage, error) {
	_, biliJct, err := c.requireLogin()
	if err != nil {
		return nil, err
	}
	params := map[string]string{
		"csrf": biliJct,
	}
	if bvid != "" {
		params["bvid"] = bvid
	} else if aid > 0 {
		params["aid"] = strconv.Itoa(aid)
	} else {
		return nil, fmt.Errorf("aid 或 bvid 缺失")
	}
	return c.webPost(ctx, "https://api.bilibili.com/x/v2/history/toview/add", params)
}

func (c *BilibiliClient) RemoveFromWatchLater(ctx context.Context, aid int) (json.RawMessage, error) {
	_, biliJct, err := c.requireLogin()
	if err != nil {
		return nil, err
	}
	if aid <= 0 {
		return nil, fmt.Errorf("aid 缺失")
	}
	params := map[string]string{
		"csrf": biliJct,
		"aid":  strconv.Itoa(aid),
	}
	return c.webPost(ctx, "https://api.bilibili.com/x/v2/history/toview/del", params)
}

func (c *BilibiliClient) GetFavoriteFolders(ctx context.Context, mid int) (json.RawMessage, error) {
	logInfo("获取收藏夹列表 mid=%d", mid)
	return c.request(ctx, "https://api.bilibili.com/x/v3/fav/folder/created/list-all", map[string]string{
		"up_mid": strconv.Itoa(mid),
	}, "GET")
}

func (c *BilibiliClient) GetFavoriteFoldersForResource(ctx context.Context, mid, aid int) (json.RawMessage, error) {
	logInfo("获取视频所在收藏夹 mid=%d aid=%d", mid, aid)
	return c.request(ctx, "https://api.bilibili.com/x/v3/fav/folder/created/list-all", map[string]string{
		"up_mid": strconv.Itoa(mid),
		"type":   "2",
		"rid":    strconv.Itoa(aid),
	}, "GET")
}

func (c *BilibiliClient) GetFavoriteResources(ctx context.Context, mediaId, pn, ps int) (json.RawMessage, error) {
	logInfo("获取收藏夹内容 media_id=%d pn=%d ps=%d", mediaId, pn, ps)
	return c.request(ctx, "https://api.bilibili.com/x/v3/fav/resource/list", map[string]string{
		"media_id": strconv.Itoa(mediaId),
		"pn":       strconv.Itoa(pn),
		"ps":       strconv.Itoa(ps),
		"order":    "mtime",
		"type":     "0",
	}, "GET")
}

func (c *BilibiliClient) GetFavoriteStatus(ctx context.Context, aid int) (json.RawMessage, error) {
	logInfo("获取收藏状态 aid=%d", aid)
	return c.request(ctx, "https://api.bilibili.com/x/v2/fav/video/favoured", map[string]string{
		"aid": strconv.Itoa(aid),
	}, "GET")
}

func (c *BilibiliClient) GetCoinStatus(ctx context.Context, aid int, bvid string) (json.RawMessage, error) {
	logInfo("获取投币状态 aid=%d bvid=%s", aid, bvid)
	params := map[string]string{}
	if bvid != "" {
		params["bvid"] = bvid
	} else if aid > 0 {
		params["aid"] = strconv.Itoa(aid)
	}
	return c.request(ctx, "https://api.bilibili.com/x/web-interface/archive/coins", params, "GET")
}

func (c *BilibiliClient) AddCoin(ctx context.Context, aid int, multiply int, selectLike bool, bvid string) (json.RawMessage, error) {
	_, buvid3, biliJct, err := c.requireRiskAuth()
	if err != nil {
		return nil, err
	}
	logInfo("投币请求认证信息: buvid3=%s", maskSensitive(buvid3))
	if multiply < 1 {
		multiply = 1
	}
	if multiply > 2 {
		multiply = 2
	}
	like := "0"
	if selectLike {
		like = "1"
	}
	params := map[string]string{
		"multiply":    strconv.Itoa(multiply),
		"select_like": like,
		"csrf":        biliJct,
	}
	if bvid != "" {
		params["bvid"] = bvid
	} else if aid > 0 {
		params["aid"] = strconv.Itoa(aid)
	}

	apiURL := "https://api.bilibili.com/x/web-interface/coin/add"
	return c.webPost(ctx, apiURL, params)
}

func (c *BilibiliClient) GetLikeStatus(ctx context.Context, aid int, bvid string) (json.RawMessage, error) {
	logInfo("获取点赞状态 aid=%d bvid=%s", aid, bvid)
	params := map[string]string{}
	if bvid != "" {
		params["bvid"] = bvid
	} else if aid > 0 {
		params["aid"] = strconv.Itoa(aid)
	}
	return c.request(ctx, "https://api.bilibili.com/x/web-interface/archive/has/like", params, "GET")
}

func (c *BilibiliClient) ToggleLike(ctx context.Context, aid int, like int, bvid string) (json.RawMessage, error) {
	_, buvid3, biliJct, err := c.requireRiskAuth()
	if err != nil {
		return nil, err
	}
	logInfo("点赞请求认证信息: buvid3=%s", maskSensitive(buvid3))
	if like != 1 && like != 2 {
		like = 1
	}
	params := map[string]string{
		"like": strconv.Itoa(like),
		"csrf": biliJct,
	}
	if bvid != "" {
		params["bvid"] = bvid
	} else if aid > 0 {
		params["aid"] = strconv.Itoa(aid)
	}

	apiURL := "https://api.bilibili.com/x/web-interface/archive/like"
	return c.webPost(ctx, apiURL, params)
}

func (c *BilibiliClient) ToggleFavorite(ctx context.Context, aid int, add bool, mediaIds []int) (json.RawMessage, error) {
	_, biliJct, err := c.requireLogin()
	if err != nil {
		return nil, err
	}
	mediaIDParam := joinIntParams(mediaIds)
	if mediaIDParam == "" {
		return nil, fmt.Errorf("收藏夹ID无效")
	}

	addMedia := ""
	delMedia := ""
	if add {
		addMedia = mediaIDParam
	} else {
		delMedia = mediaIDParam
	}

	return c.webPost(ctx, "https://api.bilibili.com/x/v3/fav/resource/deal", map[string]string{
		"rid":           strconv.Itoa(aid),
		"type":          "2",
		"add_media_ids": addMedia,
		"del_media_ids": delMedia,
		"csrf":          biliJct,
	})
}

func (c *BilibiliClient) ReportHeartbeat(ctx context.Context, aid, cid int, bvid string, playedTime int) (json.RawMessage, error) {
	_, biliJct, err := c.requireLogin()
	if err != nil {
		return nil, err
	}
	params := map[string]string{
		"aid":              strconv.Itoa(aid),
		"cid":              strconv.Itoa(cid),
		"bvid":             bvid,
		"played_time":      strconv.Itoa(playedTime),
		"real_played_time": strconv.Itoa(playedTime),
		"start_ts":         strconv.FormatInt(time.Now().Unix(), 10),
		"csrf":             biliJct,
	}
	return c.request(ctx, "https://api.bilibili.com/x/click-interface/web/heartbeat", params, "POST")
}

// ==================== HTTP 辅助函数 ====================

func writeJSON(w http.ResponseWriter, statusCode int, data interface{}) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(statusCode)
	encoder := json.NewEncoder(w)
	encoder.SetEscapeHTML(false)
	encoder.Encode(data)
}

func writeError(w http.ResponseWriter, statusCode int, message string) {
	writeJSON(w, statusCode, map[string]interface{}{
		"code":    -1,
		"message": message,
		"data":    nil,
	})
}

func wrapResult(raw json.RawMessage) map[string]interface{} {
	var result map[string]interface{}
	if err := json.Unmarshal(raw, &result); err != nil {
		// 尝试截取首尾 JSON 对象，避免上游返回多余前后缀导致解析失败
		start := bytes.IndexByte(raw, '{')
		end := bytes.LastIndexByte(raw, '}')
		if start >= 0 && end > start {
			trimmed := raw[start : end+1]
			if json.Unmarshal(trimmed, &result) == nil {
				raw = trimmed
			} else {
				logWarn("wrapResult JSON 解析失败: %s, raw=%s", err.Error(), string(raw))
				return map[string]interface{}{
					"code":    -1,
					"message": "上游返回非 JSON",
					"data":    nil,
				}
			}
		} else {
			logWarn("wrapResult JSON 解析失败: %s, raw=%s", err.Error(), string(raw))
			return map[string]interface{}{
				"code":    -1,
				"message": "解析响应失败",
				"data":    nil,
			}
		}
	}

	code := 0
	if c, ok := result["code"].(float64); ok {
		code = int(c)
	}
	message := ""
	if m, ok := result["message"].(string); ok {
		message = m
	}
	var data interface{}
	if d, ok := result["data"]; ok {
		data = d
	}

	return map[string]interface{}{
		"code":    code,
		"message": message,
		"data":    data,
	}
}

func joinIntParams(values []int) string {
	parts := make([]string, 0, len(values))
	for _, v := range values {
		if v > 0 {
			parts = append(parts, strconv.Itoa(v))
		}
	}
	return strings.Join(parts, ",")
}

func getCookie(r *http.Request, name string) string {
	c, err := r.Cookie(name)
	if err != nil {
		return ""
	}
	return c.Value
}

func getAuthFromRequest(r *http.Request) (string, string) {
	sessdata := getCookie(r, "SESSDATA")
	buvid3 := getCookie(r, "buvid3")
	if sessdata == "" {
		sessdata = r.Header.Get("X-SESSDATA")
	}
	if buvid3 == "" {
		buvid3 = r.Header.Get("X-BUVID3")
	}
	return sessdata, buvid3
}

func intParam(val string, min int, defaultVal int, hasMin bool) (int, error) {
	if val == "" {
		return defaultVal, nil
	}
	n, err := strconv.Atoi(val)
	if err != nil {
		return 0, fmt.Errorf("参数必须为整数，收到: %s", val)
	}
	if hasMin && n < min {
		return 0, fmt.Errorf("参数不得小于 %d，收到: %d", min, n)
	}
	return n, nil
}

func getIntQuery(w http.ResponseWriter, q url.Values, name string, min, def int, hasMin bool) (int, bool) {
	n, err := intParam(q.Get(name), min, def, hasMin)
	if err != nil {
		writeError(w, 400, err.Error())
		return 0, false
	}
	return n, true
}

func requireIntQuery(w http.ResponseWriter, q url.Values, name string) (int, bool) {
	v, ok := getIntQuery(w, q, name, 1, 0, true)
	if !ok {
		return 0, false
	}
	if v == 0 {
		logWarn("%s 缺失", name)
		writeError(w, 400, name+" 为必填参数")
		return 0, false
	}
	return v, true
}

func getInt64Query(q url.Values, names ...string) int64 {
	for _, name := range names {
		if val := q.Get(name); val != "" {
			if v, err := strconv.ParseInt(val, 10, 64); err == nil {
				return v
			}
		}
	}
	return 0
}

func getPageParams(w http.ResponseWriter, q url.Values, defaultPS int) (pn, ps int, ok bool) {
	pn, ok = getIntQuery(w, q, "pn", 1, 1, true)
	if !ok {
		return 0, 0, false
	}
	ps, ok = getIntQuery(w, q, "ps", 1, defaultPS, true)
	if !ok {
		return 0, 0, false
	}
	return pn, ps, true
}

func normalizeSubtitleColorPreset(value string) string {
	switch value {
	case "white", "yellow", "cyan", "black":
		return value
	default:
		return "white"
	}
}

func subtitleStyleParamsFromRequest(w http.ResponseWriter, q url.Values) (fontSize int, marginV int, spacing float64, weight int, colorPreset string, outlineEnabled bool, outlineWidth int, backgroundEnabled bool, backgroundOpacity float64, ok bool) {
	fontSize, err := intParam(q.Get("font_size"), 6, 10, true)
	if err != nil {
		writeError(w, 400, err.Error())
		return 0, 0, 0, 0, "", false, 0, false, 0, false
	}
	marginV, err = intParam(q.Get("margin_v"), 0, 2, false)
	if err != nil {
		writeError(w, 400, err.Error())
		return 0, 0, 0, 0, "", false, 0, false, 0, false
	}
	spacing, err = floatParam(q.Get("spacing"), 2.0)
	if err != nil {
		writeError(w, 400, err.Error())
		return 0, 0, 0, 0, "", false, 0, false, 0, false
	}
	weight, err = intParam(q.Get("weight"), 100, 700, true)
	if err != nil {
		writeError(w, 400, err.Error())
		return 0, 0, 0, 0, "", false, 0, false, 0, false
	}
	if weight > 900 {
		weight = 900
	}
	colorPreset = normalizeSubtitleColorPreset(q.Get("color_preset"))
	outlineFlag, err := intParam(q.Get("outline_enabled"), 0, 0, true)
	if err != nil {
		writeError(w, 400, err.Error())
		return 0, 0, 0, 0, "", false, 0, false, 0, false
	}
	outlineEnabled = outlineFlag != 0
	outlineWidth, err = intParam(q.Get("outline_width"), 1, 1, true)
	if err != nil {
		writeError(w, 400, err.Error())
		return 0, 0, 0, 0, "", false, 0, false, 0, false
	}
	if outlineWidth > 6 {
		outlineWidth = 6
	}
	backgroundFlag, err := intParam(q.Get("background_enabled"), 0, 0, true)
	if err != nil {
		writeError(w, 400, err.Error())
		return 0, 0, 0, 0, "", false, 0, false, 0, false
	}
	backgroundEnabled = backgroundFlag != 0
	backgroundOpacity, err = floatParam(q.Get("background_opacity"), 0.5)
	if err != nil {
		writeError(w, 400, err.Error())
		return 0, 0, 0, 0, "", false, 0, false, 0, false
	}
	if backgroundOpacity < 0 {
		backgroundOpacity = 0
	}
	if backgroundOpacity > 1 {
		backgroundOpacity = 1
	}
	return fontSize, marginV, spacing, weight, colorPreset, outlineEnabled, outlineWidth, backgroundEnabled, backgroundOpacity, true
}

func writeJSONBytes(w http.ResponseWriter, statusCode int, data []byte) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(statusCode)
	_, _ = w.Write(data)
}

func handleAPI(w http.ResponseWriter, action string, call func(*BilibiliClient) (json.RawMessage, error)) {
	result, err := call(getClient())
	if err != nil {
		logError("处理 %s 请求失败: %s", action, err.Error())
		writeError(w, 500, err.Error())
		return
	}

	trimmed := bytes.TrimSpace(result)
	if len(trimmed) > 0 && trimmed[0] == '{' && trimmed[len(trimmed)-1] == '}' && json.Valid(trimmed) {
		writeJSONBytes(w, 200, trimmed)
		return
	}

	writeJSON(w, 200, wrapResult(result))
}

// ==================== 全局客户端 ====================

var globalClient *BilibiliClient

// 统一使用服务器端持久化的登录态，不接受客户端传入 Cookie
func getClient() *BilibiliClient {
	return globalClient
}

// ==================== 日志中间件 ====================

// loggingResponseWriter 仅缓存响应体的前 N 字节，用于从业务层 JSON 中
// 提取 code 字段写入访问日志，避免把整个响应（可能数 MB）保留在内存中。
type loggingResponseWriter struct {
	http.ResponseWriter
	statusCode  int
	bodyHead    []byte
	headBudget  int
	wroteHeader bool
}

func (lrw *loggingResponseWriter) Write(b []byte) (int, error) {
	if !lrw.wroteHeader {
		lrw.wroteHeader = true
	}
	if lrw.headBudget > 0 {
		n := lrw.headBudget
		if n > len(b) {
			n = len(b)
		}
		lrw.bodyHead = append(lrw.bodyHead, b[:n]...)
		lrw.headBudget -= n
	}
	return lrw.ResponseWriter.Write(b)
}

func (lrw *loggingResponseWriter) WriteHeader(code int) {
	if lrw.wroteHeader {
		return
	}
	lrw.wroteHeader = true
	lrw.statusCode = code
	lrw.ResponseWriter.WriteHeader(code)
}

// Flush 透传给底层 ResponseWriter（如果支持），便于 SSE / 流式响应
func (lrw *loggingResponseWriter) Flush() {
	if f, ok := lrw.ResponseWriter.(http.Flusher); ok {
		f.Flush()
	}
}

const loggingHeadBudget = 512

// loggingRWPool 复用 loggingResponseWriter，避免每个请求都做一次 heap 分配。
// bodyHead 预分配到 loggingHeadBudget 上限，下次请求 reset 到长度 0，cap 不变。
var loggingRWPool = sync.Pool{
	New: func() any {
		return &loggingResponseWriter{
			bodyHead: make([]byte, 0, loggingHeadBudget),
		}
	},
}

func loggingMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		startTime := time.Now()

		if accessLogEnabled {
			params := make(map[string]string)
			for k, v := range r.URL.Query() {
				if len(v) > 0 {
					params[k] = v[0]
				}
			}
			logRequest(r.Method, r.URL.Path, params)
		}

		lrw := loggingRWPool.Get().(*loggingResponseWriter)
		lrw.ResponseWriter = w
		lrw.statusCode = 200
		lrw.bodyHead = lrw.bodyHead[:0]
		lrw.headBudget = loggingHeadBudget
		lrw.wroteHeader = false

		next.ServeHTTP(lrw, r)

		duration := time.Since(startTime)

		code := extractResponseCodeFromHead(lrw.bodyHead)
		statusCode := lrw.statusCode
		contentType := lrw.Header().Get("Content-Type")
		if code == -1 && statusCode >= 200 && statusCode < 300 &&
			!isJSONContentType(contentType) {
			code = 0
		}

		// 释放前清空对外引用，避免 sync.Pool 持续持有 ResponseWriter / 上层资源。
		lrw.ResponseWriter = nil
		loggingRWPool.Put(lrw)

		logResponse(r.URL.Path, code, duration)
	})
}

// recoverMiddleware 在 handler panic 时返回 500 而不是让连接被强制断开，
// 同时避免单个请求 panic 拖垮整个进程的 goroutine 监控信息。
func recoverMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		defer func() {
			if rec := recover(); rec != nil {
				// 客户端连接关闭引发的 ErrAbortHandler 直接重新抛出，让 net/http 自行处理
				if rec == http.ErrAbortHandler {
					panic(rec)
				}
				stack := debug.Stack()
				logError("处理请求时 panic: %s %s err=%v\n%s", r.Method, r.URL.Path, rec, string(stack))

				// 若已写过 header 则只能放弃 —— 双重保险防止再次 panic
				defer func() { _ = recover() }()
				writeError(w, http.StatusInternalServerError, "internal server error")
			}
		}()
		next.ServeHTTP(w, r)
	})
}

// ==================== 路由处理函数 ====================

func handleRoot(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/" {
		logWarn("404 未找到路由: %s %s", r.Method, r.URL.Path)
		writeJSON(w, 404, map[string]interface{}{
			"code":    -404,
			"message": "接口不存在",
			"data":    nil,
		})
		return
	}
	writeJSON(w, 200, map[string]interface{}{
		"code":    0,
		"message": "Bilibili API Server 运行中",
		"data": map[string]interface{}{
			"version":   "1.0.0",
			"endpoints": rootEndpoints,
		},
	})
}

func handlePopular(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	pn, ps, ok := getPageParams(w, q, 20)
	if !ok {
		return
	}
	handleAPI(w, "/popular", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetPopularVideos(r.Context(), pn, ps)
	})
}

func handleRanking(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	rid, ok := getIntQuery(w, q, "rid", 0, 0, true)
	if !ok {
		return
	}
	typ := q.Get("type")
	if typ == "" {
		typ = "all"
	}
	handleAPI(w, "/ranking", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetRanking(r.Context(), rid, typ)
	})
}

func handleSearch(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	keyword := q.Get("keyword")
	if keyword == "" {
		logWarn("搜索关键词为空")
		writeError(w, 400, "keyword 不能为空")
		return
	}
	page, ok := getIntQuery(w, q, "page", 1, 1, true)
	if !ok {
		return
	}
	pageSize, ok := getIntQuery(w, q, "page_size", 1, 20, true)
	if !ok {
		return
	}
	handleAPI(w, "/search", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.SearchVideos(r.Context(), keyword, page, pageSize)
	})
}

func handleVideoInfo(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	aid, ok := getIntQuery(w, q, "aid", 1, 0, true)
	if !ok {
		return
	}
	bvid := q.Get("bvid")
	if aid == 0 && bvid == "" {
		logWarn("aid 和 bvid 都未提供")
		writeError(w, 400, "aid 或 bvid 至少提供一个")
		return
	}
	handleAPI(w, "/video/info", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetVideoInfo(r.Context(), aid, bvid)
	})
}

func handleVideoRelated(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	aid, ok := getIntQuery(w, q, "aid", 1, 0, true)
	if !ok {
		return
	}
	bvid := q.Get("bvid")
	if aid == 0 && bvid == "" {
		logWarn("aid 和 bvid 都未提供")
		writeError(w, 400, "aid 或 bvid 至少提供一个")
		return
	}
	handleAPI(w, "/video/related", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetVideoRelated(r.Context(), aid, bvid)
	})
}

func handleVideoPlayurl(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	aid, ok := getIntQuery(w, q, "aid", 1, 0, true)
	if !ok {
		return
	}
	cid, ok := getIntQuery(w, q, "cid", 1, 0, true)
	if !ok {
		return
	}
	if aid == 0 || cid == 0 {
		logWarn("aid 或 cid 缺失")
		writeError(w, 400, "aid 和 cid 为必填参数")
		return
	}
	qn, ok := getIntQuery(w, q, "qn", 0, 64, true)
	if !ok {
		return
	}
	fnval, ok := getIntQuery(w, q, "fnval", 0, 1, false)
	if !ok {
		return
	}
	platform := q.Get("platform")
	client := getClient()
	result, err := client.GetPlayUrl(r.Context(), aid, cid, qn, fnval, platform)
	if err != nil {
		logError("处理 /video/playurl 请求失败: %s", err.Error())
		writeError(w, 500, err.Error())
		return
	}

	// 过滤不可观看的清晰度（以 accept_quality 为主，结合 support_formats 限制）
	var resp map[string]interface{}
	if err := json.Unmarshal(result, &resp); err == nil {
		if code, ok := resp["code"].(float64); ok && int(code) == 0 {
			if data, ok := resp["data"].(map[string]interface{}); ok {
				allowed := make(map[int]bool)

				// 先以 accept_quality 为主
				if aq, ok := data["accept_quality"].([]interface{}); ok {
					for _, v := range aq {
						qf, _ := v.(float64)
						if int(qf) > 0 {
							allowed[int(qf)] = true
						}
					}
				}

				// 再用 support_formats 的限制条件剔除不可观看清晰度
				if sf, ok := data["support_formats"].([]interface{}); ok {
					filtered := make([]interface{}, 0)
					for _, v := range sf {
						obj, _ := v.(map[string]interface{})
						q, _ := obj["quality"].(float64)
						canWatch, _ := obj["can_watch_qn_reason"].(float64)
						limit, _ := obj["limit_watch_reason"].(float64)
						if int(q) <= 0 {
							continue
						}
						if int(canWatch) != 0 || int(limit) != 0 {
							delete(allowed, int(q))
							continue
						}
						filtered = append(filtered, obj)
					}
					data["support_formats"] = filtered
				}

				// 重建 accept_quality（保持原顺序）
				if aq, ok := data["accept_quality"].([]interface{}); ok {
					newAq := make([]interface{}, 0)
					for _, v := range aq {
						qf, _ := v.(float64)
						if allowed[int(qf)] {
							newAq = append(newAq, v)
						}
					}
					if len(newAq) > 0 {
						data["accept_quality"] = newAq
					}
				}
			}
		}
	}

	if len(resp) > 0 {
		writeJSON(w, 200, resp)
		return
	}
	writeJSON(w, 200, wrapResult(result))
}

func handleVideoDanmaku(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	cid, ok := getIntQuery(w, q, "cid", 1, 0, true)
	if !ok {
		return
	}
	if cid == 0 {
		logWarn("cid 缺失")
		writeError(w, 400, "cid 为必填参数")
		return
	}
	segment, ok := getIntQuery(w, q, "segment", 1, 1, true)
	if !ok {
		return
	}
	client := getClient()
	data, err := client.GetDanmaku(r.Context(), cid, segment)
	if err != nil {
		logError("处理 /video/danmaku 请求失败: %s", err.Error())
		writeError(w, 500, err.Error())
		return
	}
	writeJSON(w, 200, map[string]interface{}{
		"code":    0,
		"message": "success",
		"data": map[string]interface{}{
			"cid":     cid,
			"segment": segment,
			"size":    len(data),
			"note":    "弹幕数据为 Protobuf 二进制格式，需使用专用解析器",
		},
	})
}

func formatAssTime(sec float64) string {
	if sec < 0 {
		sec = 0
	}
	h := int(sec) / 3600
	m := (int(sec) % 3600) / 60
	s := int(sec) % 60
	cs := int((sec - float64(int(sec))) * 100)
	if cs < 0 {
		cs = 0
	}
	if cs > 99 {
		cs = 99
	}
	return fmt.Sprintf("%d:%02d:%02d.%02d", h, m, s, cs)
}

func floatParam(val string, defaultVal float64) (float64, error) {
	if val == "" {
		return defaultVal, nil
	}
	f, err := strconv.ParseFloat(val, 64)
	if err != nil {
		return 0, fmt.Errorf("参数必须为数字，收到: %s", val)
	}
	return f, nil
}

func getSubtitleURL(subs []interface{}, sid int) string {
	for _, item := range subs {
		obj, ok := item.(map[string]interface{})
		if !ok {
			continue
		}
		idVal, _ := obj["id"].(float64)
		if int(idVal) == sid {
			url, _ := obj["subtitle_url"].(string)
			if strings.HasPrefix(url, "//") {
				url = "https:" + url
			}
			return url
		}
	}
	return ""
}

func fetchPlayerSubtitleList(ctx context.Context, client *BilibiliClient, aid, cid int, bvid string) ([]interface{}, error) {
	result, err := client.GetPlayerV2(ctx, aid, cid, bvid)
	if err != nil {
		return nil, err
	}

	var resp map[string]interface{}
	if err := json.Unmarshal(result, &resp); err != nil {
		return nil, fmt.Errorf("字幕列表解析失败")
	}
	code, _ := resp["code"].(float64)
	if int(code) != 0 {
		msg, _ := resp["message"].(string)
		if msg == "" {
			msg = "获取字幕列表失败"
		}
		return nil, errors.New(msg)
	}

	data, _ := resp["data"].(map[string]interface{})
	subtitle, _ := data["subtitle"].(map[string]interface{})
	subs, _ := subtitle["subtitles"].([]interface{})
	return subs, nil
}

func fetchSubtitleBody(ctx context.Context, client *BilibiliClient, subtitleURL string) ([]interface{}, error) {
	bodyRaw, err := client.rawGetURL(ctx, subtitleURL)
	if err != nil {
		return nil, fmt.Errorf("字幕下载失败: %w", err)
	}
	var subResp map[string]interface{}
	if err := json.Unmarshal(bodyRaw, &subResp); err != nil {
		return nil, fmt.Errorf("字幕内容解析失败")
	}
	body, _ := subResp["body"].([]interface{})
	if len(body) == 0 {
		return nil, fmt.Errorf("该字幕内容为空")
	}
	return body, nil
}

func sanitizeASSText(text string) string {
	text = strings.ReplaceAll(text, "\r\n", "\\N")
	text = strings.ReplaceAll(text, "\n", "\\N")
	text = strings.ReplaceAll(text, "\r", "\\N")
	// 避免 ASS override block 被字幕正文意外触发。
	text = strings.ReplaceAll(text, "{", "（")
	text = strings.ReplaceAll(text, "}", "）")
	return text
}

func subtitleASSColorWithOpacity(rgb string, opacity float64) string {
	if opacity < 0 {
		opacity = 0
	}
	if opacity > 1 {
		opacity = 1
	}
	alpha := 255 - int(opacity*255+0.5)
	return fmt.Sprintf("&H%02X%s", alpha, rgb)
}

func subtitleASSColors(colorPreset string) (primary string, outline string, back string) {
	switch normalizeSubtitleColorPreset(colorPreset) {
	case "yellow":
		return "&H004AD5FF", "&H00404040", "&H88404040"
	case "cyan":
		return "&H00FCD37D", "&H00404040", "&H88404040"
	case "black":
		return "&H00000000", "&H00FFFFFF", "&H88FFFFFF"
	default:
		return "&H00FFFFFF", "&H00404040", "&H88404040"
	}
}

func buildASSFromSubtitleBody(body []interface{}, fontSize int, marginV int, spacing float64, weight int, colorPreset string, outlineEnabled bool, outlineWidth int, backgroundEnabled bool, backgroundOpacity float64) string {
	var sb strings.Builder
	sb.WriteString("[Script Info]\n")
	sb.WriteString("ScriptType: v4.00+\n")
	sb.WriteString("PlayResX: 320\n")
	sb.WriteString("PlayResY: 170\n")
	sb.WriteString("WrapStyle: 2\n")
	sb.WriteString("ScaledBorderAndShadow: no\n\n")
	sb.WriteString("[V4+ Styles]\n")
	sb.WriteString("Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n")
	spacingStr := strconv.FormatFloat(spacing, 'f', 1, 64)
	primaryColor, outlineColor, backColor := subtitleASSColors(colorPreset)
	borderStyle := 1
	outline := 0
	if outlineEnabled {
		outline = outlineWidth
	}
	if backgroundEnabled {
		borderStyle = 3
		outlineColor = subtitleASSColorWithOpacity("404040", backgroundOpacity)
		backColor = outlineColor
		outline = 2
	}
	styleLine := fmt.Sprintf("Style: Default,Arial,%d,%s,%s,%s,%s,0,0,0,0,100,100,%s,0,%d,%d,0,2,8,8,%d,1\n\n", fontSize, primaryColor, primaryColor, outlineColor, backColor, spacingStr, borderStyle, outline, marginV)
	sb.WriteString(styleLine)
	sb.WriteString("[Events]\n")
	sb.WriteString("Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n")

	for _, item := range body {
		obj, ok := item.(map[string]interface{})
		if !ok {
			continue
		}
		from, _ := obj["from"].(float64)
		to, _ := obj["to"].(float64)
		content, _ := obj["content"].(string)
		if to <= from {
			to = from + 2
		}
		text := sanitizeASSText(html.UnescapeString(content))
		sb.WriteString(fmt.Sprintf("Dialogue: 0,%s,%s,Default,,0,0,0,,{\\b%d}%s\n", formatAssTime(from), formatAssTime(to), weight, text))
	}

	return sb.String()
}

func handleVideoDanmakuConfig(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	oid, ok := getIntQuery(w, q, "oid", 1, 0, true)
	if !ok {
		return
	}
	if oid == 0 {
		logWarn("oid 缺失")
		writeError(w, 400, "oid 为必填参数")
		return
	}
	pid, ok := getIntQuery(w, q, "pid", 1, 0, true)
	if !ok {
		return
	}
	handleAPI(w, "/video/danmaku/config", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetDanmakuConfig(r.Context(), oid, pid)
	})
}

func handleVideoCommentsReplies(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	oid, ok := requireIntQuery(w, q, "oid")
	if !ok {
		return
	}
	root, ok := requireIntQuery(w, q, "root")
	if !ok {
		return
	}
	typ, ok := getIntQuery(w, q, "type", 0, 1, true)
	if !ok {
		return
	}
	pn, ps, ok := getPageParams(w, q, 20)
	if !ok {
		return
	}
	handleAPI(w, "/video/comments/replies", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetCommentReplies(r.Context(), oid, root, typ, ps, pn)
	})
}

func handleVideoComments(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	oid, ok := requireIntQuery(w, q, "oid")
	if !ok {
		return
	}
	typ, ok := getIntQuery(w, q, "type", 0, 1, true)
	if !ok {
		return
	}
	sortVal, ok := getIntQuery(w, q, "sort", 0, 0, true)
	if !ok {
		return
	}
	pn, ps, ok := getPageParams(w, q, 20)
	if !ok {
		return
	}
	handleAPI(w, "/video/comments", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetVideoComments(r.Context(), oid, typ, sortVal, ps, pn)
	})
}

func handleUserVideos(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	mid, ok := requireIntQuery(w, q, "mid")
	if !ok {
		return
	}
	pn, ps, ok := getPageParams(w, q, 20)
	if !ok {
		return
	}
	maxVal := getInt64Query(q, "max", "cursor")
	targetAid := getInt64Query(q, "aid")
	includeCursor := strings.EqualFold(q.Get("include_cursor"), "true") || q.Get("include_cursor") == "1"

	handleAPI(w, "/user/videos", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetUserVideos(r.Context(), mid, pn, ps, maxVal, targetAid, includeCursor)
	})
}

func handleUserSeasons(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	mid, ok := requireIntQuery(w, q, "mid")
	if !ok {
		return
	}
	pn, ps, ok := getPageParams(w, q, 20)
	if !ok {
		return
	}
	handleAPI(w, "/user/seasons", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetUserSeasonsSeries(r.Context(), mid, pn, ps)
	})
}

func handleUserSeasonVideos(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	mid, ok := requireIntQuery(w, q, "mid")
	if !ok {
		return
	}
	seasonID, ok := requireIntQuery(w, q, "season_id")
	if !ok {
		return
	}
	pn, ps, ok := getPageParams(w, q, 30)
	if !ok {
		return
	}
	sortReverse := strings.EqualFold(q.Get("sort_reverse"), "true")
	handleAPI(w, "/user/season/videos", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetUserSeasonVideos(r.Context(), mid, seasonID, pn, ps, sortReverse)
	})
}

func handleUserInfo(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	mid, ok := requireIntQuery(w, q, "mid")
	if !ok {
		return
	}
	handleAPI(w, "/user/info", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetUserInfo(r.Context(), mid)
	})
}

func handleLoginInfo(w http.ResponseWriter, r *http.Request) {
	client := getClient()
	result, err := client.GetLoginInfo(r.Context())
	if err != nil {
		logError("处理 /login/info 请求失败: %s", err.Error())
		writeError(w, 500, err.Error())
		return
	}

	// 仅在明确认证失效时清理状态并通知客户端
	var resp map[string]interface{}
	if err := json.Unmarshal(result, &resp); err == nil {
		if c, ok := resp["code"].(float64); ok {
			code := int(c)
			if code == -101 || code == -401 {
				logWarn("登录已过期，清理服务器端登录状态")
				client.ClearAuth()
				writeJSON(w, 200, map[string]interface{}{
					"code":    -101,
					"message": "登录已过期",
					"data":    nil,
				})
				return
			}
		}
	}

	writeJSON(w, 200, wrapResult(result))
}

// /login/import: 导入短信登录（bili-login /pull）拿到的 cookies
// 请求体 JSON： {"SESSDATA":"...", "bili_jct":"...", "buvid3":"...", "refresh_token":"...", "DedeUserID":"...", "DedeUserID__ckMd5":"..."}
func handleLoginImport(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeJSON(w, http.StatusMethodNotAllowed, map[string]interface{}{
			"code":    405,
			"message": "method not allowed",
			"data":    nil,
		})
		return
	}

	body, err := io.ReadAll(r.Body)
	if err != nil {
		writeError(w, 400, "读取请求体失败")
		return
	}
	var payload map[string]string
	if err := json.Unmarshal(body, &payload); err != nil {
		writeError(w, 400, "JSON 解析失败")
		return
	}

	sessdata := payload["SESSDATA"]
	biliJct := payload["bili_jct"]
	buvid3 := payload["buvid3"]
	refreshToken := payload["refresh_token"]
	dedeUserID := payload["DedeUserID"]
	dedeCkMd5 := payload["DedeUserID__ckMd5"]

	if sessdata == "" {
		writeError(w, 400, "SESSDATA 不能为空")
		return
	}

	// 写入全局 Cookie（服务器端统一维护登录态）
	globalClient.UpdateAuth(sessdata, buvid3, biliJct, refreshToken, dedeUserID, dedeCkMd5)
	if buvid3 == "" {
		globalClient.ensureBuvid3()
	}

	writeJSON(w, 200, map[string]interface{}{
		"code":    0,
		"message": "ok",
		"data": map[string]interface{}{
			"sessdata": sessdata != "",
			"buvid3":   buvid3 != "",
			"bili_jct": biliJct != "",
		},
	})
}

func handleLogout(w http.ResponseWriter, r *http.Request) {
	client := getClient()

	// 按文档退出登录（web端）：POST https://passport.bilibili.com/login/exit/v2
	// 必须包含 Cookie: DedeUserID bili_jct SESSDATA，并在表单中带 biliCSRF=bili_jct
	sessdata, _, biliJct, _, dedeUserID, _ := client.getAuth()
	if sessdata == "" || biliJct == "" || dedeUserID == "" {
		// 条件不足：仍然清理本地，避免前端卡住
		logWarn("退出登录所需 cookie 不完整，直接清理本地状态 sessdata=%t bili_jct=%t DedeUserID=%t", sessdata != "", biliJct != "", dedeUserID != "")
		client.ClearAuth()
		writeJSON(w, 200, map[string]interface{}{
			"code":    0,
			"message": "logout(local)",
			"data":    nil,
		})
		return
	}

	form := url.Values{}
	form.Set("biliCSRF", biliJct)

	req, err := http.NewRequest("POST", "https://passport.bilibili.com/login/exit/v2", strings.NewReader(form.Encode()))
	if err != nil {
		logWarn("创建退出登录请求失败: %s", err.Error())
		client.ClearAuth()
		writeJSON(w, 200, map[string]interface{}{"code": 0, "message": "logout(local)", "data": nil})
		return
	}
	// headers
	client.setHeaders(req)
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	// 强制带必要 cookie（避免 buildCookie 缺字段时失败）
	req.Header.Set("Cookie", "DedeUserID="+dedeUserID+"; bili_jct="+biliJct+"; SESSDATA="+sessdata)

	hc := &http.Client{Timeout: 10 * time.Second}
	resp, err := hc.Do(req)
	if err != nil {
		logWarn("退出登录请求失败: %s", err.Error())
		client.ClearAuth()
		writeJSON(w, 200, map[string]interface{}{"code": 0, "message": "logout(local)", "data": nil})
		return
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(resp.Body)

	// 若返回 HTML（cookie 失效时），也视为已退出
	trim := bytes.TrimSpace(body)
	if len(trim) > 0 && trim[0] == '<' {
		logInfo("退出登录返回HTML，视为登录已失效")
		client.ClearAuth()
		writeJSON(w, 200, map[string]interface{}{"code": 0, "message": "logout(expired)", "data": nil})
		return
	}

	var out map[string]interface{}
	if json.Unmarshal(body, &out) != nil {
		logWarn("退出登录返回非JSON，直接清理本地")
		client.ClearAuth()
		writeJSON(w, 200, map[string]interface{}{"code": 0, "message": "logout(local)", "data": nil})
		return
	}

	// code==0 && status==true 认为成功
	codeF, _ := out["code"].(float64)
	status, _ := out["status"].(bool)
	if int(codeF) == 0 && status {
		logInfo("退出登录成功(远端)")
	} else {
		logWarn("退出登录远端返回异常: %s", string(body))
	}

	client.ClearAuth()
	writeJSON(w, 200, map[string]interface{}{
		"code":    0,
		"message": "logout",
		"data":    nil,
	})
}

func handleHotSearch(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	limit, ok := getIntQuery(w, q, "limit", 1, 10, true)
	if !ok {
		return
	}
	handleAPI(w, "/hot/search", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetHotSearch(r.Context(), limit)
	})
}

func handleRecommend(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	freshType, ok := getIntQuery(w, q, "fresh_type", 0, 3, true)
	if !ok {
		return
	}
	handleAPI(w, "/recommend", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetRecommend(r.Context(), freshType)
	})
}

func handleRecentHistory(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	maxVal, ok := getIntQuery(w, q, "max", 0, 0, false)
	if !ok {
		return
	}
	viewAt, ok := getIntQuery(w, q, "view_at", 0, 0, false)
	if !ok {
		return
	}
	ps, ok := getIntQuery(w, q, "ps", 1, 0, true)
	if !ok {
		return
	}
	handleAPI(w, "/history/recent", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetRecentHistory(r.Context(), maxVal, viewAt, ps)
	})
}

func handleToviewList(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	pn, ps, ok := getPageParams(w, q, 20)
	if !ok {
		return
	}
	handleAPI(w, "/toview/list", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetWatchLaterList(r.Context(), pn, ps)
	})
}

func handleToviewAdd(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	aid, bvid, ok := requireAidOrBvid(w, q)
	if !ok {
		return
	}
	handleAPI(w, "/toview/add", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.AddToWatchLater(r.Context(), aid, bvid)
	})
}

func handleToviewDel(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	aid, ok := getIntQuery(w, q, "aid", 1, 0, true)
	if !ok {
		return
	}
	if aid == 0 {
		writeError(w, 400, "aid 为必填参数")
		return
	}
	handleAPI(w, "/toview/del", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.RemoveFromWatchLater(r.Context(), aid)
	})
}

func handlePlayerHeartbeat(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	aid, err := intParam(q.Get("aid"), 1, 0, true)
	if err != nil {
		writeError(w, 400, err.Error())
		return
	}
	cid, err := intParam(q.Get("cid"), 1, 0, true)
	if err != nil {
		writeError(w, 400, err.Error())
		return
	}
	playedTime, err := intParam(q.Get("played_time"), -1, 0, true)
	if err != nil {
		writeError(w, 400, err.Error())
		return
	}
	bvid := q.Get("bvid")
	if bvid == "" {
		writeError(w, 400, "bvid 为必填参数")
		return
	}

	client := getClient()
	result, err := client.ReportHeartbeat(r.Context(), aid, cid, bvid, playedTime)
	if err != nil {
		logError("处理 /player/heartbeat 请求失败: %s", err.Error())
		writeError(w, 500, err.Error())
		return
	}
	writeJSON(w, 200, wrapResult(result))
}

func handleFavFolderList(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	mid, ok := requireIntQuery(w, q, "mid")
	if !ok {
		return
	}
	handleAPI(w, "/fav/folder/list", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetFavoriteFolders(r.Context(), mid)
	})
}

func handleFavResourceList(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	mediaId, ok := requireIntQuery(w, q, "media_id")
	if !ok {
		return
	}
	pn, ps, ok := getPageParams(w, q, 20)
	if !ok {
		return
	}
	handleAPI(w, "/fav/resource/list", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetFavoriteResources(r.Context(), mediaId, pn, ps)
	})
}

type favoriteFolderListResponse struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
	Data    *struct {
		List []struct {
			ID       int `json:"id"`
			Fid      int `json:"fid"`
			FavState int `json:"fav_state"`
		} `json:"list"`
	} `json:"data"`
}

func loginMidFromNav(raw json.RawMessage) (int, error) {
	var resp struct {
		Code    int    `json:"code"`
		Message string `json:"message"`
		Data    struct {
			Mid     int  `json:"mid"`
			IsLogin bool `json:"isLogin"`
		} `json:"data"`
	}
	if err := json.Unmarshal(raw, &resp); err != nil {
		return 0, fmt.Errorf("登录信息解析失败")
	}
	if resp.Code != 0 {
		msg := resp.Message
		if msg == "" {
			msg = "登录信息请求失败"
		}
		return 0, fmt.Errorf("%s", msg)
	}
	if resp.Data.Mid <= 0 || !resp.Data.IsLogin {
		return 0, fmt.Errorf("未登录")
	}
	return resp.Data.Mid, nil
}

func favoriteFolderMediaIDs(raw json.RawMessage, favoredOnly bool) ([]int, error) {
	var resp favoriteFolderListResponse
	if err := json.Unmarshal(raw, &resp); err != nil {
		return nil, fmt.Errorf("收藏夹解析失败")
	}
	if resp.Code != 0 {
		msg := resp.Message
		if msg == "" {
			msg = "收藏夹列表请求失败"
		}
		return nil, fmt.Errorf("%s", msg)
	}
	if resp.Data == nil || len(resp.Data.List) == 0 {
		return nil, nil
	}

	mediaIDs := make([]int, 0, len(resp.Data.List))
	for _, folder := range resp.Data.List {
		if favoredOnly && folder.FavState != 1 {
			continue
		}
		mediaID := folder.ID
		if mediaID <= 0 {
			mediaID = folder.Fid
		}
		if mediaID > 0 {
			mediaIDs = append(mediaIDs, mediaID)
		}
	}
	return mediaIDs, nil
}

func requireAidOrBvid(w http.ResponseWriter, q url.Values) (int, string, bool) {
	aid, err := intParam(q.Get("aid"), 1, 0, true)
	if err != nil {
		writeError(w, 400, err.Error())
		return 0, "", false
	}
	bvid := q.Get("bvid")
	if aid == 0 && bvid == "" {
		logWarn("aid 和 bvid 均缺失")
		writeError(w, 400, "aid 或 bvid 至少提供一个")
		return 0, "", false
	}
	return aid, bvid, true
}

func handleFavStatus(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	aid, ok := requireIntQuery(w, q, "aid")
	if !ok {
		return
	}
	handleAPI(w, "/fav/status", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetFavoriteStatus(r.Context(), aid)
	})
}

func handleCoinStatus(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	aid, bvid, ok := requireAidOrBvid(w, q)
	if !ok {
		return
	}
	handleAPI(w, "/coin/status", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetCoinStatus(r.Context(), aid, bvid)
	})
}

func handleCoinAdd(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	aid, bvid, ok := requireAidOrBvid(w, q)
	if !ok {
		return
	}
	multiply, ok := getIntQuery(w, q, "multiply", 1, 1, true)
	if !ok {
		return
	}
	if multiply > 2 {
		multiply = 2
	}
	selectLike := q.Get("select_like") == "1"
	handleAPI(w, "/coin/add", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.AddCoin(r.Context(), aid, multiply, selectLike, bvid)
	})
}

func handleLikeStatus(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	aid, bvid, ok := requireAidOrBvid(w, q)
	if !ok {
		return
	}
	client := getClient()
	result, err := client.GetLikeStatus(r.Context(), aid, bvid)
	if err != nil {
		logError("处理 /like/status 请求失败: %s", err.Error())
		writeError(w, 500, err.Error())
		return
	}

	// 将 data 的数值包装为对象，保证客户端解析
	var resp map[string]interface{}
	if err := json.Unmarshal(result, &resp); err == nil {
		if code, ok := resp["code"].(float64); ok && int(code) == 0 {
			liked := 0
			if v, ok := resp["data"].(float64); ok {
				liked = int(v)
			}
			writeJSON(w, 200, map[string]interface{}{
				"code":    0,
				"message": "success",
				"data": map[string]interface{}{
					"liked": liked,
				},
			})
			return
		}
	}
	writeJSON(w, 200, wrapResult(result))
}

func handleLikeToggle(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	aid, bvid, ok := requireAidOrBvid(w, q)
	if !ok {
		return
	}
	likeVal, ok := getIntQuery(w, q, "like", 1, 1, true)
	if !ok {
		return
	}
	handleAPI(w, "/like/toggle", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.ToggleLike(r.Context(), aid, likeVal, bvid)
	})
}

func handleFavToggle(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	aid, err := intParam(q.Get("aid"), 1, 0, true)
	if err != nil {
		writeError(w, 400, err.Error())
		return
	}
	if aid == 0 {
		logWarn("aid 缺失")
		writeError(w, 400, "aid 为必填参数")
		return
	}
	action := q.Get("action")
	add := action != "0"

	mediaId, err := intParam(q.Get("media_id"), 1, 0, true)
	if err != nil {
		mediaId = 0
	}

	client := getClient()
	mediaIds := []int{}
	if mediaId > 0 {
		mediaIds = []int{mediaId}
	}

	if len(mediaIds) == 0 {
		loginRaw, err := client.GetLoginInfo(r.Context())
		if err != nil {
			logError("获取登录信息失败: %s", err.Error())
			writeError(w, 500, err.Error())
			return
		}
		mid, err := loginMidFromNav(loginRaw)
		if err != nil {
			status := 500
			if err.Error() == "未登录" {
				status = 401
			}
			writeError(w, status, err.Error())
			return
		}

		var foldersRaw json.RawMessage
		if add {
			foldersRaw, err = client.GetFavoriteFolders(r.Context(), mid)
		} else {
			foldersRaw, err = client.GetFavoriteFoldersForResource(r.Context(), mid, aid)
		}
		if err != nil {
			logError("获取收藏夹失败: %s", err.Error())
			writeError(w, 500, err.Error())
			return
		}

		ids, err := favoriteFolderMediaIDs(foldersRaw, !add)
		if err != nil {
			writeError(w, 500, err.Error())
			return
		}
		if len(ids) == 0 {
			if add {
				writeError(w, 500, "未找到收藏夹")
			} else {
				writeError(w, 500, "未找到视频所在收藏夹")
			}
			return
		}
		if add {
			mediaIds = []int{ids[0]}
		} else {
			mediaIds = ids
		}
	}

	if len(mediaIds) == 0 {
		if add {
			writeError(w, 500, "未找到收藏夹")
		} else {
			writeError(w, 500, "未找到视频所在收藏夹")
		}
		return
	}

	result, err := client.ToggleFavorite(r.Context(), aid, add, mediaIds)
	if err != nil {
		logError("处理 /fav/toggle 请求失败: %s", err.Error())
		writeError(w, 500, err.Error())
		return
	}
	writeJSON(w, 200, wrapResult(result))
}

func handleUserFollowToggle(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	mid, ok := requireIntQuery(w, q, "mid")
	if !ok {
		return
	}

	action := q.Get("action")
	follow := action != "0"

	result, err := getClient().ToggleUserFollow(r.Context(), mid, follow)
	if err != nil {
		logError("处理 /user/follow/toggle 请求失败: %s", err.Error())
		writeError(w, 500, err.Error())
		return
	}

	var resp map[string]interface{}
	if err := json.Unmarshal(result, &resp); err == nil {
		if code, ok := resp["code"].(float64); ok && int(code) == 0 {
			writeJSON(w, 200, map[string]interface{}{
				"code":    0,
				"message": "success",
				"data": map[string]interface{}{
					"following": follow,
				},
			})
			return
		}
	}

	writeJSON(w, 200, wrapResult(result))
}

func handleVideoPlayerInfo(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	aid, ok := requireIntQuery(w, q, "aid")
	if !ok {
		return
	}
	cid, ok := requireIntQuery(w, q, "cid")
	if !ok {
		return
	}
	bvid := q.Get("bvid")
	handleAPI(w, "/video/player/info", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetPlayerV2(r.Context(), aid, cid, bvid)
	})
}

func handleVideoSubtitleList(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	aid, ok := requireIntQuery(w, q, "aid")
	if !ok {
		return
	}
	cid, ok := requireIntQuery(w, q, "cid")
	if !ok {
		return
	}
	bvid := q.Get("bvid")
	subs, err := fetchPlayerSubtitleList(r.Context(), getClient(), aid, cid, bvid)
	if err != nil {
		logError("处理 /video/subtitle/list 请求失败: %s", err.Error())
		writeError(w, 500, err.Error())
		return
	}
	writeJSON(w, 200, map[string]interface{}{
		"code":    0,
		"message": "success",
		"data": map[string]interface{}{
			"subtitles": subs,
		},
	})
}

func handleVideoSubtitleASS(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	aid, ok := requireIntQuery(w, q, "aid")
	if !ok {
		return
	}
	cid, ok := requireIntQuery(w, q, "cid")
	if !ok {
		return
	}
	sid, ok := requireIntQuery(w, q, "sid")
	if !ok {
		return
	}
	bvid := q.Get("bvid")

	fontSize, marginV, spacing, weight, colorPreset, outlineEnabled, outlineWidth, backgroundEnabled, backgroundOpacity, ok := subtitleStyleParamsFromRequest(w, q)
	if !ok {
		return
	}

	client := getClient()
	subs, err := fetchPlayerSubtitleList(r.Context(), client, aid, cid, bvid)
	if err != nil {
		logError("处理 /video/subtitle/ass 请求失败: %s", err.Error())
		writeError(w, 500, err.Error())
		return
	}

	subtitleURL := getSubtitleURL(subs, sid)
	if subtitleURL == "" {
		writeError(w, 404, "未找到指定字幕")
		return
	}

	body, err := fetchSubtitleBody(r.Context(), client, subtitleURL)
	if err != nil {
		status := 500
		if err.Error() == "该字幕内容为空" {
			status = 404
		}
		writeError(w, status, err.Error())
		return
	}

	assContent := buildASSFromSubtitleBody(body, fontSize, marginV, spacing, weight, colorPreset, outlineEnabled, outlineWidth, backgroundEnabled, backgroundOpacity)
	tmpPath := filepath.Join(os.TempDir(), fmt.Sprintf("bili_cc_%d_%d_%d.ass", aid, cid, sid))
	if err := os.WriteFile(tmpPath, []byte(assContent), 0644); err != nil {
		writeError(w, 500, "字幕文件写入失败")
		return
	}

	writeJSON(w, 200, map[string]interface{}{
		"code":    0,
		"message": "success",
		"data": map[string]interface{}{
			"path": tmpPath,
		},
	})
}

func handleVideoSubtitleASSFile(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	aid, ok := requireIntQuery(w, q, "aid")
	if !ok {
		return
	}
	cid, ok := requireIntQuery(w, q, "cid")
	if !ok {
		return
	}
	sid, ok := requireIntQuery(w, q, "sid")
	if !ok {
		return
	}
	bvid := q.Get("bvid")

	fontSize, marginV, spacing, weight, colorPreset, outlineEnabled, outlineWidth, backgroundEnabled, backgroundOpacity, ok := subtitleStyleParamsFromRequest(w, q)
	if !ok {
		return
	}

	client := getClient()
	subs, err := fetchPlayerSubtitleList(r.Context(), client, aid, cid, bvid)
	if err != nil {
		logError("处理 /video/subtitle/ass/file 请求失败: %s", err.Error())
		writeError(w, 500, err.Error())
		return
	}

	subtitleURL := getSubtitleURL(subs, sid)
	if subtitleURL == "" {
		writeError(w, 404, "未找到指定字幕")
		return
	}

	body, err := fetchSubtitleBody(r.Context(), client, subtitleURL)
	if err != nil {
		status := 500
		if err.Error() == "该字幕内容为空" {
			status = 404
		}
		writeError(w, status, err.Error())
		return
	}

	assContent := buildASSFromSubtitleBody(body, fontSize, marginV, spacing, weight, colorPreset, outlineEnabled, outlineWidth, backgroundEnabled, backgroundOpacity)
	w.Header().Set("Content-Type", "application/x-ass; charset=utf-8")
	w.Header().Set("Content-Disposition", fmt.Sprintf("inline; filename=\"bili_cc_%d_%d_%d.ass\"", aid, cid, sid))
	w.WriteHeader(http.StatusOK)
	_, _ = io.WriteString(w, assContent)
}

// ==================== 路由注册 ====================

func setupRoutes(mux *http.ServeMux) {
	mux.HandleFunc("/", handleRoot)
	mux.HandleFunc("/popular", handlePopular)
	mux.HandleFunc("/ranking", handleRanking)
	mux.HandleFunc("/search", handleSearch)
	mux.HandleFunc("/video/info", handleVideoInfo)
	mux.HandleFunc("/video/related", handleVideoRelated)
	mux.HandleFunc("/video/playurl", handleVideoPlayurl)
	mux.HandleFunc("/video/player/info", handleVideoPlayerInfo)
	mux.HandleFunc("/video/danmaku", handleVideoDanmaku)
	mux.HandleFunc("/video/danmaku/config", handleVideoDanmakuConfig)
	mux.HandleFunc("/video/comments", handleVideoComments)
	mux.HandleFunc("/video/comments/replies", handleVideoCommentsReplies)
	mux.HandleFunc("/video/subtitle/list", handleVideoSubtitleList)
	mux.HandleFunc("/video/subtitle/ass", handleVideoSubtitleASS)
	mux.HandleFunc("/video/subtitle/ass/file", handleVideoSubtitleASSFile)
	mux.HandleFunc("/user/info", handleUserInfo)
	mux.HandleFunc("/user/follow/toggle", handleUserFollowToggle)
	mux.HandleFunc("/user/videos", handleUserVideos)
	mux.HandleFunc("/user/seasons", handleUserSeasons)
	mux.HandleFunc("/user/season/videos", handleUserSeasonVideos)
	mux.HandleFunc("/login/info", handleLoginInfo)
	mux.HandleFunc("/login/import", handleLoginImport)
	mux.HandleFunc("/logout", handleLogout)
	mux.HandleFunc("/hot/search", handleHotSearch)
	mux.HandleFunc("/recommend", handleRecommend)
	mux.HandleFunc("/history/recent", handleRecentHistory)
	mux.HandleFunc("/toview/list", handleToviewList)
	mux.HandleFunc("/toview/add", handleToviewAdd)
	mux.HandleFunc("/toview/del", handleToviewDel)
	mux.HandleFunc("/player/heartbeat", handlePlayerHeartbeat)
	mux.HandleFunc("/fav/folder/list", handleFavFolderList)
	mux.HandleFunc("/fav/resource/list", handleFavResourceList)
	mux.HandleFunc("/fav/status", handleFavStatus)
	mux.HandleFunc("/fav/toggle", handleFavToggle)
	mux.HandleFunc("/coin/status", handleCoinStatus)
	mux.HandleFunc("/coin/add", handleCoinAdd)
	mux.HandleFunc("/like/status", handleLikeStatus)
	mux.HandleFunc("/like/toggle", handleLikeToggle)
	mux.HandleFunc("/qrcode/generate", handleQrcodeGenerate)
	mux.HandleFunc("/qrcode/poll", handleQrcodePoll)
}

// ==================== 主函数 ====================

func printBanner(debug bool) {
	logPlain("")
	logPlain(strings.Repeat("=", 60))
	logInfo("🚀 Bilibili API Server 启动中...")
	if debug {
		logInfo("调试模式: 开启 (DEBUG=true)")
	} else {
		logInfo("调试模式: 关闭 (设置 DEBUG=true 开启)")
	}
	logPlain(strings.Repeat("=", 60))
	logPlain("")
}

func printEndpoints(host, port string) {
	logPlain("")
	logPlain(strings.Repeat("=", 60))
	logSuccess("✅ 服务器运行于 http://%s:%s", host, port)
	logInfo("可用接口列表:")
	for _, e := range startupEndpoints {
		logPlain("  " + e)
	}
	logPlain(strings.Repeat("=", 60))
	logPlain("")
	logInfo("服务器已就绪，等待请求...")
	logPlain("")
}

func main() {
	port := os.Getenv("PORT")
	if port == "" {
		port = "8000"
	}
	debug := os.Getenv("DEBUG") == "true"
	host := "127.0.0.1"
	if debug {
		host = "0.0.0.0"
	}

	printBanner(debug)

	globalClient = NewBilibiliClient("", "")

	// 启动时加载本地 Cookie 缓存：loadCookieStore 内部会写字段并发布快照
	if cs, err := loadCookies(); err == nil {
		globalClient.loadCookieStore(cs)
		logInfo("已加载本地 Cookie 缓存")
	} else {
		logWarn("未加载到本地 Cookie 缓存: %s", err.Error())
	}

	globalClient.Init()

	mux := http.NewServeMux()
	setupRoutes(mux)

	handler := recoverMiddleware(loggingMiddleware(mux))

	// 关键：给 HTTP 服务器配置超时，避免慢客户端 / 半开连接耗尽 goroutine 与 fd
	srv := &http.Server{
		Addr:              host + ":" + port,
		Handler:           handler,
		ReadHeaderTimeout: 10 * time.Second,
		ReadTimeout:       30 * time.Second,
		WriteTimeout:      60 * time.Second,
		IdleTimeout:       120 * time.Second,
		MaxHeaderBytes:    1 << 20, // 1 MiB
	}

	// 退出：用 Shutdown 替代 os.Exit，让正在处理的请求有机会完成
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)
	shutdownDone := make(chan struct{})

	go func() {
		defer close(shutdownDone)
		sig := <-sigChan
		logPlain("")
		logWarn("收到 %s 信号，正在关闭...", sig.String())

		shutdownCtx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel()

		if err := srv.Shutdown(shutdownCtx); err != nil {
			logWarn("优雅关闭超时，强制结束: %s", err.Error())
			_ = srv.Close()
		} else {
			logSuccess("服务器已关闭")
		}
	}()

	printEndpoints(host, port)

	if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
		logError("❌ 启动失败: %s", err.Error())
		_ = asyncLogFlush(asyncLogShutdownTimeout)
		os.Exit(1)
	} else if err == http.ErrServerClosed {
		select {
		case <-shutdownDone:
		case <-time.After(11 * time.Second):
			logWarn("等待关闭流程完成超时")
		}
	}
	logInfo("服务器主循环已退出")
	_ = asyncLogFlush(asyncLogShutdownTimeout)
}
