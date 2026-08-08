#include <WiFi.h>
#include "WiFiClientSecure.h"
#include <time.h>

const char* ssid = "WiFiアクセスポイントのSSIDを記入";
const char* pass = "WiFiアクセスポイントのパスワードを記入";
int wbgt_nos = 11001; // WBGTを知りたい地点番号記入 一覧表はこちら(2026.8.8時点) https://www.env.go.jp/content/000307055.pdf

WiFiClientSecure client;

// キャッシュ
String cachedBody = "";
String cachedLatestRef = "";
bool cacheReady = false;

int cachedTodayWBGT = -1;
int cachedTomorrowWBGT = -1;

// ★ micro:bit にキャッシュを送る関数（追加）
void sendCacheToMicrobit() {
    if (!cacheReady) {
        Serial.println("CACHE NOT READY → SEND EMPTY");
        Serial2.println("WBGT:-1");
        Serial2.println("TWBGT:-1");
        return;
    }

    Serial.print("SEND TDY = ");
    Serial.println(cachedTodayWBGT);
    Serial.print("SEND TMR = ");
    Serial.println(cachedTomorrowWBGT);

    Serial2.print("WBGT:");
    Serial2.println(cachedTodayWBGT);

    Serial2.print("TWBGT:");
    Serial2.println(cachedTomorrowWBGT);
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 14, 25);

  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.println("WIFI connecting...");
    delay(500);
  }
  Serial.println("WIFI connected");

  configTime(9 * 3600, 0, "ntp.nict.jp", "time.google.com");

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    Serial.println("Time synced");
  } else {
    Serial.println("Time sync failed");
  }

  client.setInsecure();

  // ★ 初回 API 接続（起動直後にキャッシュ更新）
  updateCacheAndSend();
}

// 今日のURL
String makeTodayRangeURL() {
    struct tm timeinfo;
    getLocalTime(&timeinfo);

    char ymd[9];
    strftime(ymd, sizeof(ymd), "%Y%m%d", &timeinfo);

    String date = String(ymd);

    String range_from = date + "000000";
    String range_to   = date + "235959";

    String url = "/api/v1/getForecastData?"
                 "location_type=1&date_search_type=1&wbgt_nos=" + String(wbgt_nos) +
                 "&range_date_from=" + range_from +
                 "&range_date_to=" + range_to;

    Serial.println("URL = " + url);
    return url;
}

// reference_time → time_t
time_t toTimeT(String s) {
  struct tm t = {0};
  sscanf(s.c_str(), "%d/%d/%d %d:%d:%d",
         &t.tm_year, &t.tm_mon, &t.tm_mday,
         &t.tm_hour, &t.tm_min, &t.tm_sec);

  t.tm_year -= 1900;
  t.tm_mon  -= 1;

  return mktime(&t);
}

// 今日の最新 reference_time
String getLatestReferenceTime(const String& body, const char* today) {

  String latestRef = "";
  time_t latestRefTime = 0;

  int pos = 0;
  while (true) {
    int refPos = body.indexOf("\"reference_time\"", pos);
    if (refPos < 0) break;

    int refStart = body.indexOf("\"", refPos + 17) + 1;
    int refEnd   = body.indexOf("\"", refStart);
    String ref = body.substring(refStart, refEnd);

    if (ref.startsWith(today)) {
      time_t tt = toTimeT(ref);
      if (tt > latestRefTime) {
        latestRefTime = tt;
        latestRef = ref;
      }
    }

    pos = refEnd;
  }

  Serial.print("LATEST REF = ");
  Serial.println(latestRef);

  return latestRef;
}

// ★★★ 今日のWBGT（JSON 1レコード構造対応版）★★★
// 1レコード = reference_time, wbgt_no, forecast_val, forecast_time, flag
int getTodayWBGT(const String& body, const String& latestRef) {

  struct tm timeinfo;
  getLocalTime(&timeinfo);

  char today[11];
  strftime(today, sizeof(today), "%Y/%m/%d", &timeinfo);

  char nowStr[20];
  strftime(nowStr, sizeof(nowStr), "%Y/%m/%d %H:%M:%S", &timeinfo);

  int maxVal = -1;

  int pos = 0;
  while (true) {

    // ★ 1レコードの先頭：reference_time
    int refPos = body.indexOf("\"reference_time\"", pos);
    if (refPos < 0) break;

    int refStart = body.indexOf("\"", refPos + 17) + 1;
    int refEnd   = body.indexOf("\"", refStart);
    String ref = body.substring(refStart, refEnd);

    // 最新 reference_time だけ採用
    if (ref != latestRef) {
      pos = refEnd;
      continue;
    }

    // ★ この reference_time に対応する forecast_val（直後のキー）
    int fvPos = body.indexOf("\"forecast_val\"", refEnd);
    if (fvPos < 0) {
      Serial.println("FORECAST_VAL NOT FOUND");
      pos = refEnd;
      continue;
    }
    int fvStart = body.indexOf("\"", fvPos + 15) + 1;
    int fvEnd   = body.indexOf("\"", fvStart);
    int val = body.substring(fvStart, fvEnd).toInt();

    // ★ この reference_time に対応する forecast_time（forecast_val の後ろ）
    int ftPos = body.indexOf("\"forecast_time\"", fvEnd);
    if (ftPos < 0) {
      Serial.println("FORECAST_TIME NOT FOUND");
      pos = fvEnd;
      continue;
    }
    int ftStart = body.indexOf("\"", ftPos + 16) + 1;
    int ftEnd   = body.indexOf("\"", ftStart);
    String ft = body.substring(ftStart, ftEnd);

    Serial.print("[TODAY REC] ref=");
    Serial.print(ref);
    Serial.print(" ft=");
    Serial.print(ft);
    Serial.print(" val=");
    Serial.println(val);

    // ★ 今日かつ現在時刻以降
    if (ft.startsWith(today) && ft >= String(nowStr)) {
      if (val > maxVal) maxVal = val;
    }

    // 次のレコードへ
    pos = ftEnd;
  }

  return maxVal;
}

// ★★★ 明日のWBGT（JSON 1レコード構造対応版）★★★
int getTomorrowWBGT(const String& body, const String& latestRef) {

  struct tm timeinfo;
  getLocalTime(&timeinfo);

  timeinfo.tm_mday += 1;
  mktime(&timeinfo);

  char tomorrow[11];
  strftime(tomorrow, sizeof(tomorrow), "%Y/%m/%d", &timeinfo);

  int maxVal = -1;

  int pos = 0;
  while (true) {

    // ★ 1レコードの先頭：reference_time
    int refPos = body.indexOf("\"reference_time\"", pos);
    if (refPos < 0) break;

    int refStart = body.indexOf("\"", refPos + 17) + 1;
    int refEnd   = body.indexOf("\"", refStart);
    String ref = body.substring(refStart, refEnd);

    // 最新 reference_time だけ採用
    if (ref != latestRef) {
      pos = refEnd;
      continue;
    }

    // ★ この reference_time に対応する forecast_val
    int fvPos = body.indexOf("\"forecast_val\"", refEnd);
    if (fvPos < 0) {
      Serial.println("FORECAST_VAL NOT FOUND (TMR)");
      pos = refEnd;
      continue;
    }
    int fvStart = body.indexOf("\"", fvPos + 15) + 1;
    int fvEnd   = body.indexOf("\"", fvStart);
    int val = body.substring(fvStart, fvEnd).toInt();

    // ★ この reference_time に対応する forecast_time
    int ftPos = body.indexOf("\"forecast_time\"", fvEnd);
    if (ftPos < 0) {
      Serial.println("FORECAST_TIME NOT FOUND (TMR)");
      pos = fvEnd;
      continue;
    }
    int ftStart = body.indexOf("\"", ftPos + 16) + 1;
    int ftEnd   = body.indexOf("\"", ftStart);
    String ft = body.substring(ftStart, ftEnd);

    Serial.print("[TMR REC] ref=");
    Serial.print(ref);
    Serial.print(" ft=");
    Serial.print(ft);
    Serial.print(" val=");
    Serial.println(val);

    // ★ 明日の日付だけ
    if (ft.startsWith(tomorrow)) {
      if (val > maxVal) maxVal = val;
    }

    // 次のレコードへ
    pos = ftEnd;
  }

  return maxVal;
}

// ★ APIアクセスしてキャッシュ更新 → micro:bit に送信
void updateCacheAndSend() {

  Serial.println("=== UPDATE CACHE ===");

  String url = makeTodayRangeURL();

  if (!client.connect("www.wbgt.env.go.jp", 443)) {
    Serial.println("CONNECT ERR");
    return;
  }

  client.println("GET " + url + " HTTP/1.1");
  client.println("Host: www.wbgt.env.go.jp");
  client.println("Connection: close");
  client.println();

  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
  }

  cachedBody = client.readString();

  Serial.println("----- JSON BODY START -----");
  Serial.println(cachedBody);
  Serial.println("----- JSON BODY END -----");

  struct tm timeinfo;
  getLocalTime(&timeinfo);

  char today[11];
  strftime(today, sizeof(today), "%Y/%m/%d", &timeinfo);

  cachedLatestRef = getLatestReferenceTime(cachedBody, today);

  cachedTodayWBGT    = getTodayWBGT(cachedBody, cachedLatestRef)    / 10;
  cachedTomorrowWBGT = getTomorrowWBGT(cachedBody, cachedLatestRef) / 10;

  cacheReady = true;

  Serial.print("TDY WBGT = ");
  Serial.println(cachedTodayWBGT);

  Serial.print("TMR WBGT = ");
  Serial.println(cachedTomorrowWBGT);

  // ★ micro:bit に同時送信
  sendCacheToMicrobit();
}

bool updatedThisHour = false;

void loop() {

  struct tm timeinfo;
  getLocalTime(&timeinfo);

  // ★ 毎時35分にキャッシュ更新＋micro:bitへ送信
  if (timeinfo.tm_min == 35 && !updatedThisHour) {
    updateCacheAndSend();
    updatedThisHour = true;
  }

  // ★ 30分でフラグ解除（そのまま残す）
  if (timeinfo.tm_min == 30) {
    updatedThisHour = false;
  }

  // ★ micro:bit からの REQ を受け取る（キャッシュ方式）
  if (Serial2.available()) {
    String req = Serial2.readStringUntil('\n');
    req.trim();

    Serial.print("REQ = ");
    Serial.println(req);

    if (req == "REQ") {
        sendCacheToMicrobit();
    }
  }
}
