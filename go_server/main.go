package main

import (
	"bytes"
	"crypto/hmac"
	"crypto/md5"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"html"
	"io"
	"log"
	"math/big"
	"net/http"
	"net/url"
	"os"
	"os/signal"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/skip2/go-qrcode"
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
)

func getTimestamp() string {
	return time.Now().Format("2006-01-02 15:04:05")
}

func logInfo(message string, args ...interface{}) {
	msg := fmt.Sprintf(message, args...)
	fmt.Printf("%s[INFO]%s %s%s%s %s\n", colorCyan, colorReset, colorDim, getTimestamp(), colorReset, msg)
}

func logSuccess(message string, args ...interface{}) {
	msg := fmt.Sprintf(message, args...)
	fmt.Printf("%s[SUCCESS]%s %s%s%s %s\n", colorGreen, colorReset, colorDim, getTimestamp(), colorReset, msg)
}

func logWarn(message string, args ...interface{}) {
	msg := fmt.Sprintf(message, args...)
	fmt.Printf("%s[WARN]%s %s%s%s %s\n", colorYellow, colorReset, colorDim, getTimestamp(), colorReset, msg)
}

func logError(message string, args ...interface{}) {
	msg := fmt.Sprintf(message, args...)
	fmt.Printf("%s[ERROR]%s %s%s%s %s\n", colorRed, colorReset, colorDim, getTimestamp(), colorReset, msg)
}

func logDebug(message string, args ...interface{}) {
	if os.Getenv("DEBUG") == "true" {
		msg := fmt.Sprintf(message, args...)
		fmt.Printf("%s[DEBUG]%s %s%s%s %s\n", colorMagenta, colorReset, colorDim, getTimestamp(), colorReset, msg)
	}
}

func logRequest(method, path string, params map[string]string) {
	paramStr := ""
	if len(params) > 0 {
		b, _ := json.Marshal(params)
		paramStr = string(b)
	}
	fmt.Printf("%s[REQUEST]%s %s%s%s %s%s%s %s %s%s%s\n",
		   colorBlue, colorReset, colorDim, getTimestamp(), colorReset,
		   colorBright, method, colorReset, path, colorDim, paramStr, colorReset)
}

func logResponse(path string, code int, duration time.Duration) {
	color := colorGreen
	if code != 0 {
		color = colorRed
	}
	fmt.Printf("%s[RESPONSE]%s %s%s%s %s %scode=%d%s %s(%dms)%s\n",
		   color, colorReset, colorDim, getTimestamp(), colorReset,
		   path, color, code, colorReset, colorDim, duration.Milliseconds(), colorReset)
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

var DEFAULT_HEADERS = map[string]string{
	"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
	"Referer":    "https://www.bilibili.com/",
}

// ==================== Cookie 持久化 ====================

type CookieStore struct {
	Sessdata     string `json:"sessdata"`
	Buvid3       string `json:"buvid3"`
	BiliJct      string `json:"bili_jct"`
	RefreshToken string `json:"refresh_token"`
}

const cookieFile = "cookies.json"

func loadCookies() (CookieStore, error) {
	var cs CookieStore
	b, err := os.ReadFile(cookieFile)
	if err != nil {
		return cs, err
	}
	if err := json.Unmarshal(b, &cs); err != nil {
		return cs, err
	}
	return cs, nil
}

func saveCookies(cs CookieStore) error {
	b, err := json.MarshalIndent(cs, "", "  ")
	if err != nil {
		return err
	}
	// 0600：仅当前用户可读写
	return os.WriteFile(cookieFile, b, 0600)
}

// ==================== 工具函数 ====================

func getMixinKey(orig string) string {
	var sb strings.Builder
	for _, i := range MIXIN_KEY_ENC_TAB {
		if i < len(orig) {
			sb.WriteByte(orig[i])
		}
	}
	result := sb.String()
	if len(result) > 32 {
		result = result[:32]
	}
	return result
}

func md5Hash(s string) string {
	h := md5.New()
	h.Write([]byte(s))
	return hex.EncodeToString(h.Sum(nil))
}

func buildSortedQuery(params map[string]string) string {
	keys := make([]string, 0, len(params))
	for k := range params {
		keys = append(keys, k)
	}
	sort.Strings(keys)

	parts := make([]string, 0, len(keys))
	for _, k := range keys {
		parts = append(parts, url.QueryEscape(k)+"="+url.QueryEscape(params[k]))
	}
	return strings.Join(parts, "&")
}

func encWbi(params map[string]string, imgKey, subKey string) map[string]string {
	mixinKey := getMixinKey(imgKey + subKey)
	currTime := strconv.FormatInt(time.Now().Unix(), 10)
	params["wts"] = currTime

	// 按 key 排序
	keys := make([]string, 0, len(params))
	for k := range params {
		keys = append(keys, k)
	}
	sort.Strings(keys)

	// 过滤特殊字符
	filtered := make(map[string]string)
	for _, k := range keys {
		v := params[k]
		v = strings.NewReplacer("!", "", "'", "", "(", "", ")", "", "*", "").Replace(v)
		filtered[k] = v
	}

	// 构建查询字符串并签名
	query := buildSortedQuery(filtered)
	wbiSign := md5Hash(query + mixinKey)
	filtered["w_rid"] = wbiSign

	if len(mixinKey) >= 8 && len(wbiSign) >= 8 {
		logDebug("WBI 签名计算 mixinKey=%s... w_rid=%s...", mixinKey[:8], wbiSign[:8])
	}

	return filtered
}

func appSign(params map[string]string) map[string]string {
	params["appkey"] = APPKEY
	query := buildSortedQuery(params)
	params["sign"] = md5Hash(query + APPSEC)
	return params
}

func hmacSha256(key, message string) string {
	mac := hmac.New(sha256.New, []byte(key))
	mac.Write([]byte(message))
	return hex.EncodeToString(mac.Sum(nil))
}

func getBiliTicket() string {
	logInfo("正在获取 bili_ticket...")
	ts := strconv.FormatInt(time.Now().Unix(), 10)
	hexsign := hmacSha256("XgwSnGZ1p", "ts"+ts)

	params := url.Values{}
	params.Set("key_id", "ec02")
	params.Set("hexsign", hexsign)
	params.Set("context[ts]", ts)
	params.Set("csrf", "")

	apiURL := "https://api.bilibili.com/bapis/bilibili.api.ticket.v1.Ticket/GenWebTicket?" + params.Encode()

	client := &http.Client{Timeout: 5 * time.Second}
	req, err := http.NewRequest("POST", apiURL, nil)
	if err != nil {
		logError("bili_ticket 请求创建失败: %s", err.Error())
		return ""
	}
	req.Header.Set("User-Agent", DEFAULT_HEADERS["User-Agent"])

	resp, err := client.Do(req)
	if err != nil {
		logError("bili_ticket 获取异常: %s", err.Error())
		return ""
	}
	defer resp.Body.Close()

	body, _ := io.ReadAll(resp.Body)
	var result map[string]interface{}
	if err := json.Unmarshal(body, &result); err != nil {
		logError("bili_ticket 解析失败: %s", err.Error())
		return ""
	}

	code, _ := result["code"].(float64)
	if int(code) == 0 {
		data, _ := result["data"].(map[string]interface{})
	if data == nil {
		data = map[string]interface{}{}
	}
		ticket, _ := data["ticket"].(string)
		if len(ticket) > 20 {
			logSuccess("bili_ticket 获取成功: %s...", ticket[:20])
		} else {
			logSuccess("bili_ticket 获取成功: %s", ticket)
		}
		return ticket
	}

	msg, _ := result["message"].(string)
	logWarn("bili_ticket 获取失败: %s", msg)
	return ""
}

func copyMap(m map[string]string) map[string]string {
	result := make(map[string]string, len(m))
	for k, v := range m {
		result[k] = v
	}
	return result
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

func extractKeyFromURL(rawURL string) string {
	if rawURL == "" {
		return ""
	}
	parts := strings.Split(rawURL, "/")
	filename := parts[len(parts)-1]
	dotParts := strings.Split(filename, ".")
	return dotParts[0]
}

// 给BilibiliClient增加读写认证信息方法
func (c *BilibiliClient) getAuth() (string, string, string, string) {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.sessdata, c.buvid3, c.biliJct, c.refreshToken
}

func (c *BilibiliClient) getWbiKeys() (string, string) {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.imgKey, c.subKey
}

func (c *BilibiliClient) setWbiKeys(imgKey, subKey string) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.imgKey = imgKey
	c.subKey = subKey
}

func (c *BilibiliClient) getBiliTicket() string {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.biliTicket
}

func (c *BilibiliClient) setBiliTicket(ticket string) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.biliTicket = ticket
}

func (c *BilibiliClient) UpdateAuth(sessdata, buvid3, biliJct, refreshToken string) {
	c.mu.Lock()
	defer c.mu.Unlock()

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
	if refreshToken != "" && refreshToken != c.refreshToken {
		c.refreshToken = refreshToken
		changed = true
	}

	if changed {
		if err := saveCookies(CookieStore{
			Sessdata:     c.sessdata,
			Buvid3:       c.buvid3,
			BiliJct:      c.biliJct,
			RefreshToken: c.refreshToken,
		}); err != nil {
			logWarn("Cookie 持久化失败: %s", err.Error())
		} else {
			logInfo("已更新并持久化全局 Cookie")
		}
	}
}

// 清理登录状态（仅在明确认证失效时调用）
func (c *BilibiliClient) ClearAuth() {
	c.mu.Lock()
	c.sessdata = ""
	c.buvid3 = ""
	c.biliJct = ""
	c.refreshToken = ""
	c.mu.Unlock()

	if err := saveCookies(CookieStore{}); err != nil {
		logWarn("清理 Cookie 持久化失败: %s", err.Error())
	} else {
		logInfo("已清理登录状态并持久化")
	}
}


// ==================== Bilibili API 客户端 ====================

type BilibiliClient struct {
	mu         sync.RWMutex
	sessdata     string
	buvid3       string
	biliJct      string
	refreshToken string
	biliTicket   string
	imgKey       string
	subKey       string
	httpClient   *http.Client
}

func NewBilibiliClient(sessdata, buvid3 string) *BilibiliClient {
	return &BilibiliClient{
		sessdata:   sessdata,
		buvid3:     buvid3,
		httpClient: &http.Client{Timeout: 10 * time.Second},
	}
}

func (c *BilibiliClient) Init() {
	logInfo("初始化 Bilibili 客户端...")
	c.setBiliTicket(getBiliTicket())
	c.updateWbiKeys()
	c.ensureBuvid3()
	c.refreshCookies()
	logSuccess("客户端初始化完成")
}

func (c *BilibiliClient) buildCookie() string {
	sessdata, buvid3, biliJct, _ := c.getAuth()

	var parts []string
	if sessdata != "" {
		parts = append(parts, "SESSDATA="+sessdata)
	}
	if buvid3 != "" {
		parts = append(parts, "buvid3="+buvid3)
	}
	if biliJct != "" {
		parts = append(parts, "bili_jct="+biliJct)
	}
	if biliTicket := c.getBiliTicket(); biliTicket != "" {
		parts = append(parts, "bili_ticket="+biliTicket)
	}
	return strings.Join(parts, "; ")
}

func (c *BilibiliClient) setHeaders(req *http.Request) {
	for k, v := range DEFAULT_HEADERS {
		req.Header.Set(k, v)
	}
	cookie := c.buildCookie()
	if cookie != "" {
		req.Header.Set("Cookie", cookie)
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
	_, buvid3, _, _ := c.getAuth()
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
		c.UpdateAuth("", newBuvid3, "", "")
		logInfo("buvid3 获取成功")
	} else {
		logWarn("buvid3 获取失败，响应未包含该字段")
	}
}

// 仅启动时刷新一次，避免高频刷新触发风控
func (c *BilibiliClient) startCookieRefreshLoop() {}

func buildCorrespondPath(ts int64) (string, error) {
	// 对 refresh_{timestamp} 进行 RSA-OAEP(SHA256) 加密，输出小写 hex
	message := []byte("refresh_" + strconv.FormatInt(ts, 10))

	// 公钥参数来自官方文档
	modulusHex := "y4HdjgJHBlbaBN04VERG4qNBIFHP6a3GozCl75AihQloSWCXC5HDNgyinEnhaQ_4-gaMud_GF50elYXLlCToR9se9Z8z433U3KjM-3Yx7ptKkmQNAMggQwAVKgq3zYAoidNEWuxpkY_mAitTSRLnsJW-NCTa0bqBFF6Wm1MxgfE"
	modulusBytes, err := base64.RawURLEncoding.DecodeString(modulusHex)
	if err != nil {
		return "", err
	}
	modulus := new(big.Int).SetBytes(modulusBytes)
	pub := &rsa.PublicKey{N: modulus, E: 65537}

	ciphertext, err := rsa.EncryptOAEP(sha256.New(), rand.Reader, pub, message, nil)
	if err != nil {
		return "", err
	}
	return strings.ToLower(hex.EncodeToString(ciphertext)), nil
}

func getRefreshCSRF(c *BilibiliClient, sessdata, biliJct string, ts int64) string {
	correspondPath, err := buildCorrespondPath(ts)
	if err != nil {
		logWarn("生成 correspondPath 失败: %s", err.Error())
		return ""
	}
	urlStr := "https://www.bilibili.com/correspond/1/" + correspondPath
	req, err := http.NewRequest("GET", urlStr, nil)
	if err != nil {
		logWarn("请求 refresh_csrf 失败: %s", err.Error())
		return ""
	}
	c.setHeaders(req)
	req.Header.Set("Cookie", "SESSDATA="+sessdata+"; bili_jct="+biliJct)

	resp, err := c.httpClient.Do(req)
	if err != nil {
		logWarn("refresh_csrf 请求失败: %s", err.Error())
		return ""
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(resp.Body)
	text := string(body)

	// 尝试多种模式提取 refresh_csrf
	re1 := regexp.MustCompile(`refresh_csrf"\s*:\s*"([^"]+)"`)
	re2 := regexp.MustCompile(`name="refresh_csrf"\s+value="([^"]+)"`)
	if m := re1.FindStringSubmatch(text); len(m) > 1 {
		return m[1]
	}
	if m := re2.FindStringSubmatch(text); len(m) > 1 {
		return m[1]
	}
	return ""
}

func (c *BilibiliClient) refreshCookies() {
	sessdata, _, biliJct, refreshToken := c.getAuth()
	logInfo("Cookie 刷新检查: sessdata=%t bili_jct=%t refresh_token=%t", sessdata != "", biliJct != "", refreshToken != "")
	if sessdata == "" || biliJct == "" || refreshToken == "" {
		logInfo("Cookie 刷新条件不足: sessdata=%t bili_jct=%t refresh_token=%t", sessdata != "", biliJct != "", refreshToken != "")
		return
	}

	// Step 1: 获取 refresh_csrf
	infoURL := "https://passport.bilibili.com/x/passport-login/web/cookie/info?csrf=" + url.QueryEscape(biliJct)
	req, err := http.NewRequest("GET", infoURL, nil)
	if err != nil {
		logWarn("Cookie info 请求创建失败: %s", err.Error())
		return
	}
	c.setHeaders(req)
	req.Header.Set("Cookie", "SESSDATA="+sessdata+"; bili_jct="+biliJct)

	resp, err := c.httpClient.Do(req)
	if err != nil {
		logWarn("Cookie info 请求失败: %s", err.Error())
		return
	}
	defer resp.Body.Close()

	body, _ := io.ReadAll(resp.Body)
	var infoResult map[string]interface{}
	if err := json.Unmarshal(body, &infoResult); err != nil {
		logWarn("Cookie info 解析失败: %s", err.Error())
		return
	}
	if code, ok := infoResult["code"].(float64); ok && int(code) != 0 {
		logWarn("Cookie info 返回异常: code=%d", int(code))
		return
	}
	data, _ := infoResult["data"].(map[string]interface{})
	refreshNeeded, _ := data["refresh"].(bool)
	refreshCSRF, _ := data["refresh_csrf"].(string)
	if rt, ok := data["refresh_token"].(string); ok && rt != "" {
		refreshToken = rt
	}

	timestamp := int64(0)
	if ts, ok := data["timestamp"].(float64); ok {
		timestamp = int64(ts)
	}

	logInfo("Cookie 刷新状态: refresh=%t refresh_token=%t", refreshNeeded, refreshToken != "")
	if !refreshNeeded {
		logInfo("Cookie 无需刷新")
		return
	}
	if refreshCSRF == "" {
		if timestamp <= 0 {
			logInfo("refresh_csrf 缺失且 timestamp 不可用，跳过本次刷新")
			return
		}
		refreshCSRF = getRefreshCSRF(c, sessdata, biliJct, timestamp)
		if refreshCSRF == "" {
			logInfo("refresh_csrf 获取失败，跳过本次刷新")
			return
		}
	}

	// Step 2: 刷新 Cookie
	params := url.Values{}
	params.Set("csrf", biliJct)
	params.Set("refresh_csrf", refreshCSRF)
	params.Set("refresh_token", refreshToken)
	params.Set("source", "main_web")

	refreshReq, err := http.NewRequest("POST", "https://passport.bilibili.com/x/passport-login/web/cookie/refresh", strings.NewReader(params.Encode()))
	if err != nil {
		logWarn("Cookie refresh 请求创建失败: %s", err.Error())
		return
	}
	c.setHeaders(refreshReq)
	refreshReq.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	refreshReq.Header.Set("Cookie", "SESSDATA="+sessdata+"; bili_jct="+biliJct)

	refreshResp, err := c.httpClient.Do(refreshReq)
	if err != nil {
		logWarn("Cookie refresh 请求失败: %s", err.Error())
		return
	}
	defer refreshResp.Body.Close()

	refreshBody, _ := io.ReadAll(refreshResp.Body)
	var refreshResult map[string]interface{}
	if err := json.Unmarshal(refreshBody, &refreshResult); err != nil {
		logWarn("Cookie refresh 解析失败: %s", err.Error())
		return
	}
	if code, ok := refreshResult["code"].(float64); ok && int(code) != 0 {
		codeInt := int(code)
		if codeInt == 86095 {
			logInfo("Cookie 当前无需刷新或刷新条件不满足: code=%d", codeInt)
			return
		}
		logWarn("Cookie refresh 返回异常: code=%d", codeInt)
		return
	}

	// 从 Set-Cookie 更新 SESSDATA / bili_jct
	newSessdata := ""
	newBiliJct := ""
	for _, ck := range refreshResp.Cookies() {
		switch ck.Name {
		case "SESSDATA":
			newSessdata = ck.Value
		case "bili_jct":
			newBiliJct = ck.Value
		}
	}

	// 获取新的 refresh_token
	if data, ok := refreshResult["data"].(map[string]interface{}); ok {
		if rt, ok := data["refresh_token"].(string); ok && rt != "" {
			refreshToken = rt
		}
	}

	if newSessdata != "" || newBiliJct != "" || refreshToken != "" {
		c.UpdateAuth(newSessdata, "", newBiliJct, refreshToken)
		logInfo("Cookie 刷新成功")
	}
}

func (c *BilibiliClient) doRequest(apiURL string, params map[string]string, method string) (json.RawMessage, error) {
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

	req, err := http.NewRequest(method, fullURL, bodyReader)
	if err != nil {
		return nil, err
	}
	c.setHeaders(req)
	if method == http.MethodPost {
		req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
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
	return json.RawMessage(body), nil
}

func (c *BilibiliClient) wbiRequest(apiURL string, params map[string]string, method string) (json.RawMessage, error) {
	imgKey, subKey := c.getWbiKeys()
	logDebug("WBI 请求 %s %s", method, apiURL)
	return c.doRequest(apiURL, encWbi(copyMap(params), imgKey, subKey), method)
}

func (c *BilibiliClient) request(apiURL string, params map[string]string, method string) (json.RawMessage, error) {
	logDebug("普通请求 %s %s", method, apiURL)
	return c.doRequest(apiURL, params, method)
}

func (c *BilibiliClient) rawRequest(apiURL string, params map[string]string) ([]byte, error) {
	query := buildSortedQuery(params)
	fullURL := apiURL
	if query != "" {
		if strings.Contains(fullURL, "?") {
			fullURL = fullURL + "&" + query
		} else {
			fullURL = fullURL + "?" + query
		}
	}

	req, err := http.NewRequest("GET", fullURL, nil)
	if err != nil {
		return nil, err
	}
	c.setHeaders(req)

	resp, err := c.httpClient.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	return io.ReadAll(resp.Body)
}

func (c *BilibiliClient) rawGetURL(fullURL string) ([]byte, error) {
	req, err := http.NewRequest("GET", fullURL, nil)
	if err != nil {
		return nil, err
	}
	c.setHeaders(req)

	resp, err := c.httpClient.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	return io.ReadAll(resp.Body)
}

func (c *BilibiliClient) webPost(apiURL string, params map[string]string) (json.RawMessage, error) {
	query := buildSortedQuery(params)

	var lastErr error
	for attempt := 1; attempt <= 2; attempt++ {
		cookie := c.buildCookie()
		req, err := http.NewRequest("POST", apiURL, strings.NewReader(query))
		if err != nil {
			return nil, err
		}
		c.setHeaders(req)
		req.Header.Set("Content-Type", "application/x-www-form-urlencoded; charset=UTF-8")
		req.Header.Set("Origin", "https://www.bilibili.com")
		req.Header.Set("Referer", "https://www.bilibili.com/")
		req.Header.Set("X-Requested-With", "XMLHttpRequest")
		req.Header.Set("Cookie", cookie)

		resp, err := c.httpClient.Do(req)
		if err != nil {
			lastErr = err
			logWarn("webPost 请求失败 attempt=%d url=%s err=%s", attempt, apiURL, err.Error())
			if attempt < 2 {
				time.Sleep(300 * time.Millisecond)
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
				time.Sleep(300 * time.Millisecond)
				continue
			}
			return nil, readErr
		}

		if resp.StatusCode == http.StatusForbidden {
			sessdata, buvid3, biliJct, refreshToken := c.getAuth()
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
				time.Sleep(300 * time.Millisecond)
				continue
			}
			return nil, lastErr
		}

		if resp.StatusCode >= 400 {
			logWarn("webPost 非成功状态 attempt=%d url=%s status=%d body=%s", attempt, apiURL, resp.StatusCode, string(body))
			lastErr = fmt.Errorf("HTTP %d", resp.StatusCode)
			if attempt < 2 {
				time.Sleep(300 * time.Millisecond)
				continue
			}
			return nil, lastErr
		}

		// 业务层错误日志（code != 0）
		var respObj map[string]interface{}
		if err := json.Unmarshal(body, &respObj); err == nil {
			if c, ok := respObj["code"].(float64); ok && int(c) != 0 {
				msg, _ := respObj["message"].(string)
				logWarn("webPost 业务错误 url=%s code=%d message=%s", apiURL, int(c), msg)
				if os.Getenv("DEBUG") == "true" {
					logDebug("webPost 业务响应 body=%s", string(body))
				}
			}
		} else if os.Getenv("DEBUG") == "true" {
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

func (c *BilibiliClient) GetPopularVideos(pn, ps int) (json.RawMessage, error) {
	logInfo("获取热门视频 pn=%d ps=%d", pn, ps)
	return c.wbiRequest("https://api.bilibili.com/x/web-interface/popular", map[string]string{
		"pn": strconv.Itoa(pn),
			    "ps": strconv.Itoa(ps),
	}, "GET")
}

func (c *BilibiliClient) GetRanking(rid int, typ string) (json.RawMessage, error) {
	logInfo("获取排行榜 rid=%d type=%s", rid, typ)
	return c.wbiRequest("https://api.bilibili.com/x/web-interface/ranking/v2", map[string]string{
		"rid":  strconv.Itoa(rid),
			    "type": typ,
	}, "GET")
}

func (c *BilibiliClient) SearchVideos(keyword string, page, pageSize int) (json.RawMessage, error) {
	logInfo("搜索视频 keyword=\"%s\" page=%d", keyword, page)
	return c.wbiRequest("https://api.bilibili.com/x/web-interface/wbi/search/type", map[string]string{
		"search_type": "video",
		"keyword":     keyword,
		"page":        strconv.Itoa(page),
			    "page_size":   strconv.Itoa(pageSize),
	}, "GET")
}

func (c *BilibiliClient) GetVideoInfo(aid int, bvid string) (json.RawMessage, error) {
	logInfo("获取视频信息 aid=%d bvid=%s", aid, bvid)
	params := map[string]string{}
	if aid > 0 {
		params["aid"] = strconv.Itoa(aid)
	}
	if bvid != "" {
		params["bvid"] = bvid
	}
	return c.wbiRequest("https://api.bilibili.com/x/web-interface/wbi/view", params, "GET")
}

func (c *BilibiliClient) GetVideoComments(oid, typ, sortVal, ps, pn int) (json.RawMessage, error) {
	logInfo("获取评论 oid=%d type=%d sort=%d", oid, typ, sortVal)
	return c.request("https://api.bilibili.com/x/v2/reply", map[string]string{
		"type": strconv.Itoa(typ),
			 "oid":  strconv.Itoa(oid),
			 "sort": strconv.Itoa(sortVal),
			 "ps":   strconv.Itoa(ps),
			 "pn":   strconv.Itoa(pn),
	}, "GET")
}

func (c *BilibiliClient) GetCommentReplies(oid, root, typ, ps, pn int) (json.RawMessage, error) {
	logInfo("获取子评论 oid=%d root=%d type=%d", oid, root, typ)
	return c.request("https://api.bilibili.com/x/v2/reply/reply", map[string]string{
		"type": strconv.Itoa(typ),
		"oid":  strconv.Itoa(oid),
		"root": strconv.Itoa(root),
		"ps":   strconv.Itoa(ps),
		"pn":   strconv.Itoa(pn),
	}, "GET")
}

func (c *BilibiliClient) GetUserInfo(mid int) (json.RawMessage, error) {
	logInfo("获取用户信息 mid=%d", mid)

	baseRaw, err := c.wbiRequest("https://api.bilibili.com/x/space/wbi/acc/info", map[string]string{
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
	relRaw, err := c.request("https://api.bilibili.com/x/relation/stat", map[string]string{
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

	merged, err := json.Marshal(baseResp)
	if err != nil {
		return nil, err
	}
	return merged, nil
}

func (c *BilibiliClient) GetUserVideosApp(mid, pn, ps int) (json.RawMessage, error) {
	if pn <= 0 {
		pn = 1
	}
	if ps <= 0 {
		ps = 20
	}
	if ps > 30 {
		ps = 30
	}
	logInfo("获取用户投稿(APP) mid=%d pn=%d ps=%d", mid, pn, ps)
	// APP 接口为游标模式，pn 容易触发异常，这里固定为 1 以保证稳定
	params := map[string]string{
		"vmid":     strconv.Itoa(mid),
		"pn":       "1",
		"ps":       strconv.Itoa(ps),
		"order":    "pubdate",
		"platform": "android",
		"mobi_app": "android",
		"device":   "android",
		"ts":       strconv.FormatInt(time.Now().Unix(), 10),
	}
	params = appSign(params)
	raw, err := c.request("https://app.biliapi.com/x/v2/space/archive/cursor", params, "GET")
	if err != nil {
		return nil, err
	}

	// 修正偶发返回列表顺序异常（旧稿件在前）的问题
	var resp map[string]interface{}
	if err := json.Unmarshal(raw, &resp); err == nil {
		if code, ok := resp["code"].(float64); ok && int(code) == 0 {
			data, _ := resp["data"].(map[string]interface{})
			if data != nil {
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

func (c *BilibiliClient) GetUserVideos(mid, pn, ps int) (json.RawMessage, error) {
	logInfo("获取用户投稿 mid=%d pn=%d ps=%d", mid, pn, ps)

	requestOnce := func() (json.RawMessage, error) {
		return c.wbiRequest("https://api.bilibili.com/x/space/wbi/arc/search", map[string]string{
			"mid":            strconv.Itoa(mid),
			"pn":             strconv.Itoa(pn),
			"ps":             strconv.Itoa(ps),
			"order":          "pubdate",
			"dm_img_list":    "[]",
			"dm_img_str":     "",
			"dm_cover_img_str": "",
		}, "GET")
	}

	raw, err := requestOnce()
	if err != nil {
		logWarn("投稿接口请求失败，降级到 APP: %s", err.Error())
		return c.GetUserVideosApp(mid, pn, ps)
	}

	trimmed := bytes.TrimSpace(raw)
	if len(trimmed) > 0 && trimmed[0] == '<' {
		logWarn("投稿接口命中风控，降级到 APP")
		return c.GetUserVideosApp(mid, pn, ps)
	}

	// 处理业务错误，重点修复偶发 -400
	var resp map[string]interface{}
	if err := json.Unmarshal(raw, &resp); err == nil {
		if code, ok := resp["code"].(float64); ok && int(code) != 0 {
			codeInt := int(code)
			msg, _ := resp["message"].(string)
			logWarn("投稿接口返回异常 code=%d message=%s", codeInt, msg)
			// -400/-412 可能由 wbi key 过期或签名失效导致，刷新后重试一次
			if codeInt == -400 || codeInt == -412 {
				logInfo("检测到投稿接口 code=%d，刷新 WBI Keys 并重试", codeInt)
				c.updateWbiKeys()
				retryRaw, retryErr := requestOnce()
				if retryErr == nil {
					return retryRaw, nil
				}
				logWarn("投稿接口重试失败，降级到 APP: %s", retryErr.Error())
				return c.GetUserVideosApp(mid, pn, ps)
			}
			// 其他非 0 状态，直接降级到 APP，避免前端收到 -400
			return c.GetUserVideosApp(mid, pn, ps)
		}
	}

	return raw, nil
}

func (c *BilibiliClient) GetLoginInfo() (json.RawMessage, error) {
	logInfo("获取登录信息")
	return c.request("https://api.bilibili.com/x/web-interface/nav", map[string]string{}, "GET")
}

func (c *BilibiliClient) GetPlayerV2(aid, cid int, bvid string) (json.RawMessage, error) {
	params := map[string]string{
		"aid": strconv.Itoa(aid),
		"cid": strconv.Itoa(cid),
	}
	if bvid != "" {
		params["bvid"] = bvid
	}
	return c.wbiRequest("https://api.bilibili.com/x/player/wbi/v2", params, "GET")
}

func (c *BilibiliClient) GetPlayUrl(aid, cid, qn, fnval int, platform string) (json.RawMessage, error) {
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
	return c.wbiRequest("https://api.bilibili.com/x/player/wbi/playurl", map[string]string{
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

func (c *BilibiliClient) GetDanmaku(cid, segment int) ([]byte, error) {
	logInfo("获取弹幕 cid=%d segment=%d", cid, segment)
	return c.rawRequest("https://api.bilibili.com/x/v2/dm/web/seg.so", map[string]string{
		"type":          "1",
		"oid":           strconv.Itoa(cid),
			    "segment_index": strconv.Itoa(segment),
	})
}

func (c *BilibiliClient) GetDanmakuConfig(oid int, pid int) (json.RawMessage, error) {
	logInfo("获取弹幕配置 oid=%d pid=%d", oid, pid)
	params := map[string]string{
		"type": "1",
		"oid":  strconv.Itoa(oid),
	}
	if pid > 0 {
		params["pid"] = strconv.Itoa(pid)
	}
	return c.request("https://api.bilibili.com/x/v2/dm/web/view", params, "GET")
}

func (c *BilibiliClient) GetHotSearch(limit int) (json.RawMessage, error) {
	logInfo("获取热搜 limit=%d", limit)
	return c.request("https://api.bilibili.com/x/web-interface/search/square", map[string]string{
		"limit": strconv.Itoa(limit),
	}, "GET")
}

func (c *BilibiliClient) GetRecommend(freshType int) (json.RawMessage, error) {
	logInfo("获取推荐 feed fresh_type=%d", freshType)
	return c.wbiRequest("https://api.bilibili.com/x/web-interface/wbi/index/top/feed/rcmd", map[string]string{
		"fresh_type": strconv.Itoa(freshType),
	}, "GET")
}

func (c *BilibiliClient) GetRecentHistory(max, viewAt int) (json.RawMessage, error) {
	logInfo("获取最近观看 max=%d view_at=%d", max, viewAt)
	params := map[string]string{}
	if max > 0 {
		params["max"] = strconv.Itoa(max)
	}
	if viewAt > 0 {
		params["view_at"] = strconv.Itoa(viewAt)
	}
	params["business"] = "archive"
	return c.request("https://api.bilibili.com/x/web-interface/history/cursor", params, "GET")
}

func (c *BilibiliClient) GetWatchLaterList(pn, ps int) (json.RawMessage, error) {
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
	return c.request("https://api.bilibili.com/x/v2/history/toview", params, "GET")
}

func (c *BilibiliClient) AddToWatchLater(aid int, bvid string) (json.RawMessage, error) {
	sessdata, _, biliJct, _ := c.getAuth()
	if sessdata == "" || biliJct == "" {
		return nil, fmt.Errorf("登录信息不完整")
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
	return c.webPost("https://api.bilibili.com/x/v2/history/toview/add", params)
}

func (c *BilibiliClient) RemoveFromWatchLater(aid int) (json.RawMessage, error) {
	sessdata, _, biliJct, _ := c.getAuth()
	if sessdata == "" || biliJct == "" {
		return nil, fmt.Errorf("登录信息不完整")
	}
	if aid <= 0 {
		return nil, fmt.Errorf("aid 缺失")
	}
	params := map[string]string{
		"csrf": biliJct,
		"aid":  strconv.Itoa(aid),
	}
	return c.webPost("https://api.bilibili.com/x/v2/history/toview/del", params)
}

func (c *BilibiliClient) GetFavoriteFolders(mid int) (json.RawMessage, error) {
	logInfo("获取收藏夹列表 mid=%d", mid)
	return c.request("https://api.bilibili.com/x/v3/fav/folder/created/list-all", map[string]string{
		"up_mid": strconv.Itoa(mid),
	}, "GET")
}

func (c *BilibiliClient) GetFavoriteResources(mediaId, pn, ps int) (json.RawMessage, error) {
	logInfo("获取收藏夹内容 media_id=%d pn=%d ps=%d", mediaId, pn, ps)
	return c.request("https://api.bilibili.com/x/v3/fav/resource/list", map[string]string{
		"media_id": strconv.Itoa(mediaId),
		"pn":       strconv.Itoa(pn),
		"ps":       strconv.Itoa(ps),
		"order":    "mtime",
		"type":     "0",
	}, "GET")
}

func (c *BilibiliClient) GetFavoriteStatus(aid int) (json.RawMessage, error) {
	logInfo("获取收藏状态 aid=%d", aid)
	return c.request("https://api.bilibili.com/x/v2/fav/video/favoured", map[string]string{
		"aid": strconv.Itoa(aid),
	}, "GET")
}

func (c *BilibiliClient) GetCoinStatus(aid int, bvid string) (json.RawMessage, error) {
	logInfo("获取投币状态 aid=%d bvid=%s", aid, bvid)
	params := map[string]string{}
	if bvid != "" {
		params["bvid"] = bvid
	} else if aid > 0 {
		params["aid"] = strconv.Itoa(aid)
	}
	return c.request("https://api.bilibili.com/x/web-interface/archive/coins", params, "GET")
}

func (c *BilibiliClient) AddCoin(aid int, multiply int, selectLike bool, bvid string) (json.RawMessage, error) {
	sessdata, buvid3, biliJct, _ := c.getAuth()
	if sessdata == "" || biliJct == "" {
		return nil, fmt.Errorf("登录信息不完整")
	}
	if buvid3 == "" {
		return nil, fmt.Errorf("缺少 buvid3，按文档该字段异常会触发风控")
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
	return c.webPost(apiURL, params)
}

func (c *BilibiliClient) GetLikeStatus(aid int, bvid string) (json.RawMessage, error) {
	logInfo("获取点赞状态 aid=%d bvid=%s", aid, bvid)
	params := map[string]string{}
	if bvid != "" {
		params["bvid"] = bvid
	} else if aid > 0 {
		params["aid"] = strconv.Itoa(aid)
	}
	return c.request("https://api.bilibili.com/x/web-interface/archive/has/like", params, "GET")
}

func (c *BilibiliClient) ToggleLike(aid int, like int, bvid string) (json.RawMessage, error) {
	sessdata, buvid3, biliJct, _ := c.getAuth()
	if sessdata == "" || biliJct == "" {
		return nil, fmt.Errorf("登录信息不完整")
	}
	if buvid3 == "" {
		return nil, fmt.Errorf("缺少 buvid3，按文档该字段异常会触发风控")
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
	return c.webPost(apiURL, params)
}

func (c *BilibiliClient) ToggleFavorite(aid int, add bool, mediaId int) (json.RawMessage, error) {
	sessdata, _, biliJct, _ := c.getAuth()
	if sessdata == "" || biliJct == "" {
		return nil, fmt.Errorf("登录信息不完整")
	}

	addMedia := ""
	delMedia := ""
	if add {
		addMedia = strconv.Itoa(mediaId)
	} else {
		delMedia = strconv.Itoa(mediaId)
	}

	return c.webPost("https://api.bilibili.com/x/v3/fav/resource/deal", map[string]string{
		"rid":           strconv.Itoa(aid),
		"type":          "2",
		"add_media_ids": addMedia,
		"del_media_ids": delMedia,
		"csrf":          biliJct,
	})
}

func (c *BilibiliClient) ReportHeartbeat(aid, cid int, bvid string, playedTime int) (json.RawMessage, error) {
	sessdata, _, biliJct, _ := c.getAuth()
	if sessdata == "" || biliJct == "" {
		return nil, fmt.Errorf("登录信息不完整")
	}
	params := map[string]string{
		"aid":         strconv.Itoa(aid),
		"cid":         strconv.Itoa(cid),
		"bvid":        bvid,
		"played_time": strconv.Itoa(playedTime),
		"real_played_time": strconv.Itoa(playedTime),
		"start_ts":    strconv.FormatInt(time.Now().Unix(), 10),
		"csrf":        biliJct,
	}
	return c.request("https://api.bilibili.com/x/click-interface/web/heartbeat", params, "POST")
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

func getIntQuery(w http.ResponseWriter, r *http.Request, name string, min, def int, hasMin bool) (int, bool) {
	n, err := intParam(r.URL.Query().Get(name), min, def, hasMin)
	if err != nil {
		writeError(w, 400, err.Error())
		return 0, false
	}
	return n, true
}

func handleAPI(w http.ResponseWriter, action string, call func(*BilibiliClient) (json.RawMessage, error)) {
	result, err := call(getClient())
	if err != nil {
		logError("处理 %s 请求失败: %s", action, err.Error())
		writeError(w, 500, err.Error())
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

type loggingResponseWriter struct {
	http.ResponseWriter
	body       []byte
	statusCode int
}

func (lrw *loggingResponseWriter) Write(b []byte) (int, error) {
	lrw.body = append(lrw.body, b...)
	return lrw.ResponseWriter.Write(b)
}

func (lrw *loggingResponseWriter) WriteHeader(code int) {
	lrw.statusCode = code
	lrw.ResponseWriter.WriteHeader(code)
}

func loggingMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		startTime := time.Now()

		params := make(map[string]string)
		for k, v := range r.URL.Query() {
			if len(v) > 0 {
				params[k] = v[0]
			}
		}
		logRequest(r.Method, r.URL.Path, params)

		lrw := &loggingResponseWriter{ResponseWriter: w, statusCode: 200}
		next.ServeHTTP(lrw, r)

		duration := time.Since(startTime)

		code := -1
		var respBody map[string]interface{}
		if err := json.Unmarshal(lrw.body, &respBody); err == nil {
			if c, ok := respBody["code"].(float64); ok {
				code = int(c)
			}
		}

		logResponse(r.URL.Path, code, duration)
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
			"version": "1.0.0",
			"endpoints": []string{
				"/popular - 热门视频",
				"/ranking - 排行榜",
				"/search - 搜索视频",
				"/video/info - 视频详情",
				"/video/playurl - 播放地址",
				"/video/danmaku - 弹幕",
				"/video/comments - 评论",
				"/video/subtitle/list - CC字幕列表",
				"/video/subtitle/ass - 生成ASS字幕",
				"/user/info - 用户信息",
				"/user/videos - 用户投稿",
				"/login/info - 登录信息",
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
                "/video/comments/replies - 子评论",
			},
		},
	})
}

func handlePopular(w http.ResponseWriter, r *http.Request) {
	pn, ok := getIntQuery(w, r, "pn", 1, 1, true)
	if !ok {
		return
	}
	ps, ok := getIntQuery(w, r, "ps", 1, 20, true)
	if !ok {
		return
	}
	handleAPI(w, "/popular", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetPopularVideos(pn, ps)
	})
}

func handleRanking(w http.ResponseWriter, r *http.Request) {
	rid, ok := getIntQuery(w, r, "rid", 0, 0, true)
	if !ok {
		return
	}
	typ := r.URL.Query().Get("type")
	if typ == "" {
		typ = "all"
	}
	handleAPI(w, "/ranking", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetRanking(rid, typ)
	})
}

func handleSearch(w http.ResponseWriter, r *http.Request) {
	keyword := r.URL.Query().Get("keyword")
	if keyword == "" {
		logWarn("搜索关键词为空")
		writeError(w, 400, "keyword 不能为空")
		return
	}
	page, ok := getIntQuery(w, r, "page", 1, 1, true)
	if !ok {
		return
	}
	pageSize, ok := getIntQuery(w, r, "page_size", 1, 20, true)
	if !ok {
		return
	}
	handleAPI(w, "/search", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.SearchVideos(keyword, page, pageSize)
	})
}

func handleVideoInfo(w http.ResponseWriter, r *http.Request) {
	aid, ok := getIntQuery(w, r, "aid", 1, 0, true)
	if !ok {
		return
	}
	bvid := r.URL.Query().Get("bvid")
	if aid == 0 && bvid == "" {
		logWarn("aid 和 bvid 都未提供")
		writeError(w, 400, "aid 或 bvid 至少提供一个")
		return
	}
	handleAPI(w, "/video/info", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetVideoInfo(aid, bvid)
	})
}

func handleVideoPlayurl(w http.ResponseWriter, r *http.Request) {
	aid, ok := getIntQuery(w, r, "aid", 1, 0, true)
	if !ok {
		return
	}
	cid, ok := getIntQuery(w, r, "cid", 1, 0, true)
	if !ok {
		return
	}
	if aid == 0 || cid == 0 {
		logWarn("aid 或 cid 缺失")
		writeError(w, 400, "aid 和 cid 为必填参数")
		return
	}
	qn, ok := getIntQuery(w, r, "qn", 0, 64, true)
	if !ok {
		return
	}
	fnval, ok := getIntQuery(w, r, "fnval", 0, 1, false)
	if !ok {
		return
	}
	platform := r.URL.Query().Get("platform")
	client := getClient()
	result, err := client.GetPlayUrl(aid, cid, qn, fnval, platform)
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
	cid, ok := getIntQuery(w, r, "cid", 1, 0, true)
	if !ok {
		return
	}
	if cid == 0 {
		logWarn("cid 缺失")
		writeError(w, 400, "cid 为必填参数")
		return
	}
	segment, ok := getIntQuery(w, r, "segment", 1, 1, true)
	if !ok {
		return
	}
	client := getClient()
	data, err := client.GetDanmaku(cid, segment)
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

func buildASSFromSubtitleBody(body []interface{}, fontSize int, outline float64, marginV int, spacing float64, bold int) string {
	var sb strings.Builder
	sb.WriteString("[Script Info]\n")
	sb.WriteString("ScriptType: v4.00+\n")
	sb.WriteString("PlayResX: 320\n")
	sb.WriteString("PlayResY: 170\n")
	sb.WriteString("WrapStyle: 2\n")
	sb.WriteString("ScaledBorderAndShadow: no\n\n")
	sb.WriteString("[V4+ Styles]\n")
	sb.WriteString("Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n")
	outlineStr := strconv.FormatFloat(outline, 'f', 1, 64)
	spacingStr := strconv.FormatFloat(spacing, 'f', 1, 64)
	styleLine := fmt.Sprintf("Style: Default,Arial,%d,&H00FFFFFF,&H000000FF,&H00111111,&H64000000,%d,0,0,0,100,100,%s,0,1,%s,0,2,8,8,%d,1\n\n", fontSize, bold, spacingStr, outlineStr, marginV)
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
		text := html.EscapeString(content)
		text = strings.ReplaceAll(text, "\r\n", "\\N")
		text = strings.ReplaceAll(text, "\n", "\\N")
		text = strings.ReplaceAll(text, "\r", "\\N")
		sb.WriteString(fmt.Sprintf("Dialogue: 0,%s,%s,Default,,0,0,0,,%s\n", formatAssTime(from), formatAssTime(to), text))
	}

	return sb.String()
}

func handleVideoDanmakuConfig(w http.ResponseWriter, r *http.Request) {
	oid, ok := getIntQuery(w, r, "oid", 1, 0, true)
	if !ok {
		return
	}
	if oid == 0 {
		logWarn("oid 缺失")
		writeError(w, 400, "oid 为必填参数")
		return
	}
	pid, ok := getIntQuery(w, r, "pid", 1, 0, true)
	if !ok {
		return
	}
	handleAPI(w, "/video/danmaku/config", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetDanmakuConfig(oid, pid)
	})
}

func handleVideoCommentsReplies(w http.ResponseWriter, r *http.Request) {
	oid, ok := getIntQuery(w, r, "oid", 1, 0, true)
	if !ok {
		return
	}
	if oid == 0 {
		logWarn("oid 缺失")
		writeError(w, 400, "oid 为必填参数")
		return
	}
	root, ok := getIntQuery(w, r, "root", 1, 0, true)
	if !ok {
		return
	}
	if root == 0 {
		logWarn("root 缺失")
		writeError(w, 400, "root 为必填参数")
		return
	}
	typ, ok := getIntQuery(w, r, "type", 0, 1, true)
	if !ok {
		return
	}
	ps, ok := getIntQuery(w, r, "ps", 1, 20, true)
	if !ok {
		return
	}
	pn, ok := getIntQuery(w, r, "pn", 1, 1, true)
	if !ok {
		return
	}
	handleAPI(w, "/video/comments/replies", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetCommentReplies(oid, root, typ, ps, pn)
	})
}

func handleVideoComments(w http.ResponseWriter, r *http.Request) {
	oid, ok := getIntQuery(w, r, "oid", 1, 0, true)
	if !ok {
		return
	}
	if oid == 0 {
		logWarn("oid 缺失")
		writeError(w, 400, "oid 为必填参数")
		return
	}
	typ, ok := getIntQuery(w, r, "type", 0, 1, true)
	if !ok {
		return
	}
	sortVal, ok := getIntQuery(w, r, "sort", 0, 0, true)
	if !ok {
		return
	}
	ps, ok := getIntQuery(w, r, "ps", 1, 20, true)
	if !ok {
		return
	}
	pn, ok := getIntQuery(w, r, "pn", 1, 1, true)
	if !ok {
		return
	}
	handleAPI(w, "/video/comments", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetVideoComments(oid, typ, sortVal, ps, pn)
	})
}

func handleUserVideos(w http.ResponseWriter, r *http.Request) {
	mid, ok := getIntQuery(w, r, "mid", 1, 0, true)
	if !ok {
		return
	}
	if mid == 0 {
		logWarn("mid 缺失")
		writeError(w, 400, "mid 为必填参数")
		return
	}
	pn, ok := getIntQuery(w, r, "pn", 1, 1, true)
	if !ok {
		return
	}
	ps, ok := getIntQuery(w, r, "ps", 1, 20, true)
	if !ok {
		return
	}
	handleAPI(w, "/user/videos", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetUserVideos(mid, pn, ps)
	})
}

func handleUserInfo(w http.ResponseWriter, r *http.Request) {
	mid, ok := getIntQuery(w, r, "mid", 1, 0, true)
	if !ok {
		return
	}
	if mid == 0 {
		logWarn("mid 缺失")
		writeError(w, 400, "mid 为必填参数")
		return
	}
	handleAPI(w, "/user/info", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetUserInfo(mid)
	})
}

func handleLoginInfo(w http.ResponseWriter, r *http.Request) {
	client := getClient()
	result, err := client.GetLoginInfo()
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

func handleLogout(w http.ResponseWriter, r *http.Request) {
	client := getClient()
	client.ClearAuth()
	writeJSON(w, 200, map[string]interface{}{
		"code":    0,
		"message": "logout",
		"data":    nil,
	})
}

func handleHotSearch(w http.ResponseWriter, r *http.Request) {
	limit, ok := getIntQuery(w, r, "limit", 1, 10, true)
	if !ok {
		return
	}
	handleAPI(w, "/hot/search", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetHotSearch(limit)
	})
}

func handleRecommend(w http.ResponseWriter, r *http.Request) {
	freshType, ok := getIntQuery(w, r, "fresh_type", 0, 3, true)
	if !ok {
		return
	}
	handleAPI(w, "/recommend", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetRecommend(freshType)
	})
}

func handleRecentHistory(w http.ResponseWriter, r *http.Request) {
	maxVal, ok := getIntQuery(w, r, "max", 0, 0, false)
	if !ok {
		return
	}
	viewAt, ok := getIntQuery(w, r, "view_at", 0, 0, false)
	if !ok {
		return
	}
	handleAPI(w, "/history/recent", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetRecentHistory(maxVal, viewAt)
	})
}

func handleToviewList(w http.ResponseWriter, r *http.Request) {
	pn, ok := getIntQuery(w, r, "pn", 1, 1, true)
	if !ok {
		return
	}
	ps, ok := getIntQuery(w, r, "ps", 1, 20, true)
	if !ok {
		return
	}
	handleAPI(w, "/toview/list", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetWatchLaterList(pn, ps)
	})
}

func handleToviewAdd(w http.ResponseWriter, r *http.Request) {
	aid, bvid, ok := requireAidOrBvid(w, r)
	if !ok {
		return
	}
	handleAPI(w, "/toview/add", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.AddToWatchLater(aid, bvid)
	})
}

func handleToviewDel(w http.ResponseWriter, r *http.Request) {
	aid, ok := getIntQuery(w, r, "aid", 1, 0, true)
	if !ok {
		return
	}
	if aid == 0 {
		writeError(w, 400, "aid 为必填参数")
		return
	}
	handleAPI(w, "/toview/del", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.RemoveFromWatchLater(aid)
	})
}

func handlePlayerHeartbeat(w http.ResponseWriter, r *http.Request) {
	aid, err := intParam(r.URL.Query().Get("aid"), 1, 0, true)
	if err != nil {
		writeError(w, 400, err.Error())
		return
	}
	cid, err := intParam(r.URL.Query().Get("cid"), 1, 0, true)
	if err != nil {
		writeError(w, 400, err.Error())
		return
	}
	playedTime, err := intParam(r.URL.Query().Get("played_time"), 0, 0, false)
	if err != nil {
		writeError(w, 400, err.Error())
		return
	}
	bvid := r.URL.Query().Get("bvid")
	if bvid == "" {
		writeError(w, 400, "bvid 为必填参数")
		return
	}

	client := getClient()
	result, err := client.ReportHeartbeat(aid, cid, bvid, playedTime)
	if err != nil {
		logError("处理 /player/heartbeat 请求失败: %s", err.Error())
		writeError(w, 500, err.Error())
		return
	}
	writeJSON(w, 200, wrapResult(result))
}

func handleFavFolderList(w http.ResponseWriter, r *http.Request) {
	mid, ok := getIntQuery(w, r, "mid", 1, 0, true)
	if !ok {
		return
	}
	if mid == 0 {
		logWarn("mid 缺失")
		writeError(w, 400, "mid 为必填参数")
		return
	}
	handleAPI(w, "/fav/folder/list", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetFavoriteFolders(mid)
	})
}

func handleFavResourceList(w http.ResponseWriter, r *http.Request) {
	mediaId, ok := getIntQuery(w, r, "media_id", 1, 0, true)
	if !ok {
		return
	}
	if mediaId == 0 {
		logWarn("media_id 缺失")
		writeError(w, 400, "media_id 为必填参数")
		return
	}
	pn, ok := getIntQuery(w, r, "pn", 1, 1, true)
	if !ok {
		return
	}
	ps, ok := getIntQuery(w, r, "ps", 1, 20, true)
	if !ok {
		return
	}
	handleAPI(w, "/fav/resource/list", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetFavoriteResources(mediaId, pn, ps)
	})
}

func requireAidOrBvid(w http.ResponseWriter, r *http.Request) (int, string, bool) {
	aid, err := intParam(r.URL.Query().Get("aid"), 1, 0, true)
	if err != nil {
		writeError(w, 400, err.Error())
		return 0, "", false
	}
	bvid := r.URL.Query().Get("bvid")
	if aid == 0 && bvid == "" {
		logWarn("aid 和 bvid 均缺失")
		writeError(w, 400, "aid 或 bvid 至少提供一个")
		return 0, "", false
	}
	return aid, bvid, true
}

func handleFavStatus(w http.ResponseWriter, r *http.Request) {
	aid, ok := getIntQuery(w, r, "aid", 1, 0, true)
	if !ok {
		return
	}
	if aid == 0 {
		logWarn("aid 缺失")
		writeError(w, 400, "aid 为必填参数")
		return
	}
	handleAPI(w, "/fav/status", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetFavoriteStatus(aid)
	})
}

func handleCoinStatus(w http.ResponseWriter, r *http.Request) {
	aid, bvid, ok := requireAidOrBvid(w, r)
	if !ok {
		return
	}
	handleAPI(w, "/coin/status", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.GetCoinStatus(aid, bvid)
	})
}

func handleCoinAdd(w http.ResponseWriter, r *http.Request) {
	aid, bvid, ok := requireAidOrBvid(w, r)
	if !ok {
		return
	}
	multiply, ok := getIntQuery(w, r, "multiply", 1, 1, true)
	if !ok {
		return
	}
	if multiply > 2 {
		multiply = 2
	}
	selectLike := r.URL.Query().Get("select_like") == "1"
	handleAPI(w, "/coin/add", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.AddCoin(aid, multiply, selectLike, bvid)
	})
}

func handleLikeStatus(w http.ResponseWriter, r *http.Request) {
	aid, bvid, ok := requireAidOrBvid(w, r)
	if !ok {
		return
	}
	client := getClient()
	result, err := client.GetLikeStatus(aid, bvid)
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
	aid, bvid, ok := requireAidOrBvid(w, r)
	if !ok {
		return
	}
	likeVal, ok := getIntQuery(w, r, "like", 1, 1, true)
	if !ok {
		return
	}
	handleAPI(w, "/like/toggle", func(c *BilibiliClient) (json.RawMessage, error) {
		return c.ToggleLike(aid, likeVal, bvid)
	})
}

func handleFavToggle(w http.ResponseWriter, r *http.Request) {
	aid, err := intParam(r.URL.Query().Get("aid"), 1, 0, true)
	if err != nil {
		writeError(w, 400, err.Error())
		return
	}
	if aid == 0 {
		logWarn("aid 缺失")
		writeError(w, 400, "aid 为必填参数")
		return
	}
	action := r.URL.Query().Get("action")
	add := action != "0"

	mediaId, err := intParam(r.URL.Query().Get("media_id"), 1, 0, true)
	if err != nil {
		mediaId = 0
	}

	client := getClient()
	if mediaId <= 0 {
		// 未指定收藏夹时，回退到默认收藏夹
		loginRaw, err := client.GetLoginInfo()
		if err != nil {
			logError("获取登录信息失败: %s", err.Error())
			writeError(w, 500, err.Error())
			return
		}
		var loginResp map[string]interface{}
		if err := json.Unmarshal(loginRaw, &loginResp); err != nil {
			writeError(w, 500, "登录信息解析失败")
			return
		}
		data, _ := loginResp["data"].(map[string]interface{})
		midF, _ := data["mid"].(float64)
		mid := int(midF)
		if mid <= 0 {
			writeError(w, 401, "未登录")
			return
		}

		foldersRaw, err := client.GetFavoriteFolders(mid)
		if err != nil {
			logError("获取收藏夹失败: %s", err.Error())
			writeError(w, 500, err.Error())
			return
		}
		var favResp map[string]interface{}
		if err := json.Unmarshal(foldersRaw, &favResp); err != nil {
			writeError(w, 500, "收藏夹解析失败")
			return
		}
		favData, _ := favResp["data"].(map[string]interface{})
		list, _ := favData["list"].([]interface{})
		if len(list) == 0 {
			writeError(w, 500, "未找到收藏夹")
			return
		}
		first, _ := list[0].(map[string]interface{})
		fidF, _ := first["id"].(float64)
		if fidF == 0 {
			fidF, _ = first["fid"].(float64)
		}
		mediaId = int(fidF)
		if mediaId <= 0 {
			writeError(w, 500, "收藏夹ID无效")
			return
		}
	}

	result, err := client.ToggleFavorite(aid, add, mediaId)
	if err != nil {
		logError("处理 /fav/toggle 请求失败: %s", err.Error())
		writeError(w, 500, err.Error())
		return
	}
	writeJSON(w, 200, wrapResult(result))
}

func handleQrcodeGenerate(w http.ResponseWriter, r *http.Request) {
	logInfo("生成登录二维码")

	httpClient := &http.Client{Timeout: 10 * time.Second}
	req, err := http.NewRequest("GET", "https://passport.bilibili.com/x/passport-login/web/qrcode/generate", nil)
	if err != nil {
		logError("处理 /qrcode/generate 请求失败: %s", err.Error())
		writeError(w, 500, err.Error())
		return
	}

	resp, err := httpClient.Do(req)
	if err != nil {
		logError("处理 /qrcode/generate 请求失败: %s", err.Error())
		writeError(w, 500, err.Error())
		return
	}
	defer resp.Body.Close()

	body, _ := io.ReadAll(resp.Body)
	var result map[string]interface{}
	if err := json.Unmarshal(body, &result); err != nil {
		logError("解析二维码响应失败: %s", err.Error())
		writeError(w, 500, err.Error())
		return
	}

	data, _ := result["data"].(map[string]interface{})
	if data == nil {
		data = map[string]interface{}{}
	}
	qrURL, _ := data["url"].(string)

	// 生成二维码 base64
	qrcodeBase64 := ""
	if qrURL != "" {
		png, err := qrcode.Encode(qrURL, qrcode.Medium, 150)
		if err == nil {
			qrcodeBase64 = "data:image/png;base64," + base64.StdEncoding.EncodeToString(png)
			logInfo("二维码生成成功")
		} else {
			logWarn("二维码图片生成失败: %s", err.Error())
		}
	}

	data["qrcode"] = qrcodeBase64

	code := 0
	if c, ok := result["code"].(float64); ok {
		code = int(c)
	}
	message := ""
	if m, ok := result["message"].(string); ok {
		message = m
	}

	writeJSON(w, 200, map[string]interface{}{
		"code":    code,
		"message": message,
		"data":    data,
	})
}

func handleVideoSubtitleList(w http.ResponseWriter, r *http.Request) {
	aid, err := intParam(r.URL.Query().Get("aid"), 1, 0, true)
	if err != nil {
		writeError(w, 400, err.Error())
		return
	}
	cid, err := intParam(r.URL.Query().Get("cid"), 1, 0, true)
	if err != nil {
		writeError(w, 400, err.Error())
		return
	}
	bvid := r.URL.Query().Get("bvid")
	client := getClient()
	result, err := client.GetPlayerV2(aid, cid, bvid)
	if err != nil {
		logError("处理 /video/subtitle/list 请求失败: %s", err.Error())
		writeError(w, 500, err.Error())
		return
	}

	var resp map[string]interface{}
	if err := json.Unmarshal(result, &resp); err != nil {
		writeError(w, 500, "字幕列表解析失败")
		return
	}
	code, _ := resp["code"].(float64)
	if int(code) != 0 {
		writeJSON(w, 200, wrapResult(result))
		return
	}
	data, _ := resp["data"].(map[string]interface{})
	subtitle, _ := data["subtitle"].(map[string]interface{})
	subs, _ := subtitle["subtitles"].([]interface{})
	writeJSON(w, 200, map[string]interface{}{
		"code":    0,
		"message": "success",
		"data": map[string]interface{}{
			"subtitles": subs,
		},
	})
}

func handleVideoSubtitleASS(w http.ResponseWriter, r *http.Request) {
	aid, err := intParam(r.URL.Query().Get("aid"), 1, 0, true)
	if err != nil {
		writeError(w, 400, err.Error())
		return
	}
	cid, err := intParam(r.URL.Query().Get("cid"), 1, 0, true)
	if err != nil {
		writeError(w, 400, err.Error())
		return
	}
	sid, err := intParam(r.URL.Query().Get("sid"), 1, 0, true)
	if err != nil {
		writeError(w, 400, err.Error())
		return
	}
	bvid := r.URL.Query().Get("bvid")

	fontSize, err := intParam(r.URL.Query().Get("font_size"), 6, 10, true)
	if err != nil {
		writeError(w, 400, err.Error())
		return
	}
	outline, err := floatParam(r.URL.Query().Get("outline"), 2.3)
	if err != nil {
		writeError(w, 400, err.Error())
		return
	}
	marginV, err := intParam(r.URL.Query().Get("margin_v"), 0, 2, false)
	if err != nil {
		writeError(w, 400, err.Error())
		return
	}
	spacing, err := floatParam(r.URL.Query().Get("spacing"), 2.0)
	if err != nil {
		writeError(w, 400, err.Error())
		return
	}
	bold, err := intParam(r.URL.Query().Get("bold"), 0, 1, false)
	if err != nil {
		writeError(w, 400, err.Error())
		return
	}

	client := getClient()
	result, err := client.GetPlayerV2(aid, cid, bvid)
	if err != nil {
		logError("处理 /video/subtitle/ass 请求失败: %s", err.Error())
		writeError(w, 500, err.Error())
		return
	}

	var resp map[string]interface{}
	if err := json.Unmarshal(result, &resp); err != nil {
		writeError(w, 500, "字幕列表解析失败")
		return
	}
	code, _ := resp["code"].(float64)
	if int(code) != 0 {
		writeJSON(w, 200, wrapResult(result))
		return
	}
	data, _ := resp["data"].(map[string]interface{})
	subtitle, _ := data["subtitle"].(map[string]interface{})
	subs, _ := subtitle["subtitles"].([]interface{})

	var subtitleURL string
	for _, item := range subs {
		obj, ok := item.(map[string]interface{})
		if !ok {
			continue
		}
		idVal, _ := obj["id"].(float64)
		if int(idVal) == sid {
			subtitleURL, _ = obj["subtitle_url"].(string)
			break
		}
	}
	if subtitleURL == "" {
		writeError(w, 404, "未找到指定字幕")
		return
	}
	if strings.HasPrefix(subtitleURL, "//") {
		subtitleURL = "https:" + subtitleURL
	}

	bodyRaw, err := client.rawGetURL(subtitleURL)
	if err != nil {
		writeError(w, 500, "字幕下载失败: "+err.Error())
		return
	}
	var subResp map[string]interface{}
	if err := json.Unmarshal(bodyRaw, &subResp); err != nil {
		writeError(w, 500, "字幕内容解析失败")
		return
	}
	body, _ := subResp["body"].([]interface{})
	if len(body) == 0 {
		writeError(w, 404, "该字幕内容为空")
		return
	}

	assContent := buildASSFromSubtitleBody(body, fontSize, outline, marginV, spacing, bold)
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

func handleQrcodePoll(w http.ResponseWriter, r *http.Request) {
	qrcodeKey := r.URL.Query().Get("qrcode_key")
	if qrcodeKey == "" {
		logWarn("qrcode_key 缺失")
		writeError(w, 400, "qrcode_key 为必填参数")
		return
	}

	keyDisplay := qrcodeKey
	if len(keyDisplay) > 10 {
		keyDisplay = keyDisplay[:10] + "..."
	}
	logInfo("轮询二维码状态 key=%s", keyDisplay)

	httpClient := &http.Client{Timeout: 10 * time.Second}
	params := url.Values{}
	params.Set("qrcode_key", qrcodeKey)
	apiURL := "https://passport.bilibili.com/x/passport-login/web/qrcode/poll?" + params.Encode()

	req, err := http.NewRequest("GET", apiURL, nil)
	if err != nil {
		logError("处理 /qrcode/poll 请求失败: %s", err.Error())
		writeError(w, 500, err.Error())
		return
	}
	req.Header.Set("User-Agent", DEFAULT_HEADERS["User-Agent"])

	resp, err := httpClient.Do(req)
	if err != nil {
		logError("处理 /qrcode/poll 请求失败: %s", err.Error())
		writeError(w, 500, err.Error())
		return
	}
	defer resp.Body.Close()

	body, _ := io.ReadAll(resp.Body)
	var result map[string]interface{}
	if err := json.Unmarshal(body, &result); err != nil {
		logError("解析轮询响应失败: %s", err.Error())
		writeError(w, 500, err.Error())
		return
	}

	data, _ := result["data"].(map[string]interface{})

	code := 0
	if c, ok := result["code"].(float64); ok {
		code = int(c)
	}

	// 登录成功时从 cookies 中提取 SESSDATA / bili_jct
	if code == 0 {
		if dataURL, ok := data["url"].(string); ok && dataURL != "" {
			var sessdata, biliJct, buvid3 string
			for _, cookie := range resp.Cookies() {
				switch cookie.Name {
				case "SESSDATA":
					sessdata = cookie.Value
					data["SESSDATA"] = sessdata
				case "bili_jct":
					biliJct = cookie.Value
					data["bili_jct"] = biliJct
				case "buvid3":
					buvid3 = cookie.Value
					data["buvid3"] = buvid3
				}
			}
			if sessdata != "" {
				logInfo("成功获取 SESSDATA")
			}

			// 从响应 data 中提取 refresh_token
			refreshToken := ""
			if rt, ok := data["refresh_token"].(string); ok {
				refreshToken = rt
			}
			if refreshToken != "" {
				data["refresh_token"] = refreshToken
			}

			// 更新全局 Cookie（用于服务器端自动刷新）
			globalClient.UpdateAuth(sessdata, buvid3, biliJct, refreshToken)
			if buvid3 == "" {
				globalClient.ensureBuvid3()
			}
		}
	}

	message := ""
	if m, ok := result["message"].(string); ok {
		message = m
	}

	writeJSON(w, 200, map[string]interface{}{
		"code":    code,
		"message": message,
		"data":    data,
	})
}

// ==================== 路由注册 ====================

func setupRoutes(mux *http.ServeMux) {
	mux.HandleFunc("/", handleRoot)
	mux.HandleFunc("/popular", handlePopular)
	mux.HandleFunc("/ranking", handleRanking)
	mux.HandleFunc("/search", handleSearch)
	mux.HandleFunc("/video/info", handleVideoInfo)
	mux.HandleFunc("/video/playurl", handleVideoPlayurl)
	mux.HandleFunc("/video/danmaku", handleVideoDanmaku)
	mux.HandleFunc("/video/danmaku/config", handleVideoDanmakuConfig)
	mux.HandleFunc("/video/comments", handleVideoComments)
    mux.HandleFunc("/video/comments/replies", handleVideoCommentsReplies)
	mux.HandleFunc("/video/subtitle/list", handleVideoSubtitleList)
	mux.HandleFunc("/video/subtitle/ass", handleVideoSubtitleASS)
	mux.HandleFunc("/user/info", handleUserInfo)
	mux.HandleFunc("/user/videos", handleUserVideos)
	mux.HandleFunc("/login/info", handleLoginInfo)
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
	fmt.Println()
	fmt.Println(strings.Repeat("=", 60))
	logInfo("🚀 Bilibili API Server 启动中...")
	if debug {
		logInfo("调试模式: 开启 (DEBUG=true)")
	} else {
		logInfo("调试模式: 关闭 (设置 DEBUG=true 开启)")
	}
	fmt.Println(strings.Repeat("=", 60))
	fmt.Println()
}

func printEndpoints(port string) {
	fmt.Println()
	fmt.Println(strings.Repeat("=", 60))
	logSuccess("✅ 服务器运行于 http://0.0.0.0:%s", port)
	logInfo("可用接口列表:")
	endpoints := []string{
		"GET  /popular               - 热门视频",
		"GET  /ranking               - 排行榜",
		"GET  /search                - 搜索视频",
		"GET  /video/info            - 视频详情",
		"GET  /video/playurl         - 播放地址",
		"GET  /video/danmaku         - 弹幕数据",
		"GET  /video/comments        - 评论列表",
		"GET  /video/comments/replies- 子评论",
		"GET  /video/subtitle/list   - CC字幕列表",
		"GET  /video/subtitle/ass    - 生成ASS字幕",
		"GET  /user/info             - 用户信息",
		"GET  /user/videos           - 用户投稿",
		"GET  /login/info            - 登录信息",
		"GET  /hot/search            - 热搜榜",
		"GET  /recommend             - 首页推荐",
		"GET  /history/recent        - 最近观看",
		"GET  /toview/list           - 稍后再看列表",
		"GET  /toview/add            - 添加稍后再看",
		"GET  /toview/del            - 取消稍后再看",
		"GET  /player/heartbeat      - 回调心跳",
		"GET  /fav/folder/list       - 收藏夹列表",
		"GET  /fav/resource/list     - 收藏夹内容",
		"GET  /fav/status            - 收藏状态",
		"GET  /fav/toggle            - 收藏切换",
		"GET  /coin/status           - 投币状态",
		"GET  /coin/add              - 投币",
		"GET  /like/status           - 点赞状态",
		"GET  /like/toggle           - 点赞/取消点赞",
		"GET  /qrcode/generate       - 生成登录二维码",
		"GET  /qrcode/poll           - 轮询二维码状态",
	}
	for _, e := range endpoints {
		fmt.Println("  " + e)
	}
	fmt.Println(strings.Repeat("=", 60))
	fmt.Println()
	logInfo("服务器已就绪，等待请求...")
	fmt.Println()
}

func main() {
	port := os.Getenv("PORT")
	if port == "" {
		port = "8000"
	}
	debug := os.Getenv("DEBUG") == "true"

	printBanner(debug)

	globalClient = NewBilibiliClient("", "")

	// 启动时加载本地 Cookie 缓存
	if cs, err := loadCookies(); err == nil {
		globalClient.sessdata = cs.Sessdata
		globalClient.buvid3 = cs.Buvid3
		globalClient.biliJct = cs.BiliJct
		globalClient.refreshToken = cs.RefreshToken
		logInfo("已加载本地 Cookie 缓存")
	} else {
		logWarn("未加载到本地 Cookie 缓存: %s", err.Error())
	}

	globalClient.Init()

	mux := http.NewServeMux()
	setupRoutes(mux)

	handler := loggingMiddleware(mux)

	// 优雅退出
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)

	go func() {
		sig := <-sigChan
		fmt.Println()
		logWarn("收到 %s 信号，正在关闭服务器...", sig.String())
		os.Exit(0)
	}()

	printEndpoints(port)

	if err := http.ListenAndServe("0.0.0.0:"+port, handler); err != nil {
		logError("❌ 启动失败: %s", err.Error())
		log.Fatal(err)
	}
}
