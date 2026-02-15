#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <time.h>

#include "anthem.h"
#include "assets.h"
#include "config.h"
#include "olympic_scoreboard_client.h"
#include "olympic_scoreboard_ui.h"
#include "wifi_fallback.h"

SET_LOOP_TASK_STACK_SIZE(16 * 1024);

namespace {

enum class ScreenPage : uint8_t {
  MEDALS,
  SCHEDULE
};

static TFT_eSPI tft;
static OlympicScoreboardUi ui;
static OlympicScoreboardClient client;

static ScreenPage currentPage = ScreenPage::MEDALS;
static MedalTableState medals;
static DailyScheduleState scheduleToday;
static bool hasMedals = false;
static bool hasSchedule = false;
static bool sportBaselinePrimed = false;

static uint32_t lastMedalsPollMs = 0;
static uint32_t lastSchedulePollMs = 0;
static uint32_t lastRotateMs = 0;
static uint32_t lastGoodMedalsMs = 0;
static uint32_t lastGoodScheduleMs = 0;

static bool timeConfigured = false;
static uint32_t lastTimeConfigAttemptMs = 0;
static bool lastWifiConnected = false;

static const uint32_t kMedalsPollIntervalMs = 30000;
static const uint32_t kSchedulePollIntervalMs = 60000;
static const uint32_t kRotateIntervalMs = 18000;
static const uint32_t kStaleAfterMs = 90000;
static const uint32_t kAlertPopupMs = 6000;
static const uint32_t kAlertAudioMs = 8000;

static const uint8_t kAlertQueueSize = 4;
static MedalAlertEvent alertQueue[kAlertQueueSize];
static uint8_t alertHead = 0;
static uint8_t alertTail = 0;
static uint8_t alertCount = 0;

static bool alertActive = false;
static MedalAlertEvent activeAlert;
static uint32_t alertUntilMs = 0;

static bool wifiConnectedNow() {
  return WiFi.status() == WL_CONNECTED;
}

static void ensureTimeConfigured(uint32_t nowMs) {
  if (timeConfigured) return;
  if (nowMs - lastTimeConfigAttemptMs < 15000) return;
  lastTimeConfigAttemptMs = nowMs;

  setenv("TZ", TZ_INFO, 1);
  tzset();
  configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2);

  const time_t now = time(nullptr);
  timeConfigured = now > 1577836800;
  if (timeConfigured) {
    Serial.printf("TIME: configured (%ld)\n", (long)now);
  }
}

static String todayYmd() {
  time_t now = time(nullptr);
  if (now <= 1577836800) {
    return String("2026-02-11");
  }
  struct tm lt;
  localtime_r(&now, &lt);
  char buf[16];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &lt);
  return String(buf);
}

static bool enqueueAlert(const MedalAlertEvent &ev) {
  if (!ev.valid) return false;
  if (alertCount >= kAlertQueueSize) {
    alertHead = (uint8_t)((alertHead + 1) % kAlertQueueSize);
    alertCount--;
  }
  alertQueue[alertTail] = ev;
  alertTail = (uint8_t)((alertTail + 1) % kAlertQueueSize);
  alertCount++;
  return true;
}

static bool dequeueAlert(MedalAlertEvent &out) {
  if (alertCount == 0) return false;
  out = alertQueue[alertHead];
  alertHead = (uint8_t)((alertHead + 1) % kAlertQueueSize);
  alertCount--;
  return true;
}

static bool medalsStale(uint32_t nowMs) {
  return !hasMedals || (lastGoodMedalsMs == 0) || (nowMs - lastGoodMedalsMs > kStaleAfterMs);
}

static bool scheduleStale(uint32_t nowMs) {
  return !hasSchedule || (lastGoodScheduleMs == 0) || (nowMs - lastGoodScheduleMs > kStaleAfterMs);
}

static void renderCurrentPage(uint32_t nowMs) {
  const bool wifi = wifiConnectedNow();
  if (currentPage == ScreenPage::MEDALS) {
    ui.drawMedals(medals, FOCUS_TEAM_ABBR, wifi, medalsStale(nowMs));
  } else {
    ui.drawSchedule(scheduleToday, wifi, scheduleStale(nowMs));
  }
}

static void togglePage(uint32_t nowMs) {
  if (currentPage == ScreenPage::MEDALS) {
    currentPage = ScreenPage::SCHEDULE;
  } else {
    currentPage = ScreenPage::MEDALS;
  }
  lastRotateMs = nowMs;
  renderCurrentPage(nowMs);
}

static bool pollMedals(uint32_t nowMs) {
  MedalTableState fresh;
  if (!client.fetchMedalTable(fresh, FOCUS_TEAM_ABBR)) {
    Serial.println("MEDALS: fetch failed");
    return false;
  }

  if (hasMedals) {
    MedalAlertEvent alert;
    if (client.buildFavoriteMedalAlert(medals, fresh, FOCUS_TEAM_ABBR, alert)) {
      enqueueAlert(alert);
      Serial.printf("MEDALS: %s medal detected (%s)\n",
                    FOCUS_TEAM_ABBR,
                    alert.sportName.c_str());
    }
  }

  medals = fresh;
  hasMedals = true;
  lastGoodMedalsMs = nowMs;

  if (!sportBaselinePrimed) {
    sportBaselinePrimed = client.primeFavoriteSportBaseline(FOCUS_TEAM_ABBR);
    Serial.printf("MEDALS: sport baseline %s\n", sportBaselinePrimed ? "ready" : "unavailable");
  }

  return true;
}

static bool pollSchedule(uint32_t nowMs) {
  DailyScheduleState fresh;
  const String ymd = todayYmd();
  if (!client.fetchDailySchedule(fresh, ymd)) {
    Serial.println("SCHEDULE: fetch failed");
    return false;
  }

  scheduleToday = fresh;
  hasSchedule = true;
  lastGoodScheduleMs = nowMs;
  return true;
}

static void maybeShowAlert(uint32_t nowMs) {
  if (alertActive) return;

  MedalAlertEvent nextAlert;
  if (!dequeueAlert(nextAlert)) return;

  activeAlert = nextAlert;
  alertActive = true;
  alertUntilMs = nowMs + max(kAlertPopupMs, kAlertAudioMs);
  ui.drawMedalAlert(activeAlert, FOCUS_TEAM_ABBR);
  Anthem::playNowForMs(kAlertAudioMs);
}

}  // namespace

void setup() {
  Serial.begin(115200);

  ledcSetup(CYD_BL_PWM_CH, 5000, 8);
  ledcAttachPin(TFT_BL, CYD_BL_PWM_CH);

  uint8_t rotation = TFT_ROTATION;
  ui.begin(tft, rotation);
  if (tft.width() < tft.height()) {
    rotation = (rotation == 1) ? 3 : 1;
    ui.setRotation(rotation);
  }
  ui.setBacklight(85);

  Assets::begin(tft);
  Anthem::begin();

  ui.drawBootSplash("MILANO CORTINA 2026", "CONNECTING WIFI");
  wifiConnectWithFallback();

  const uint32_t nowMs = millis();
  ensureTimeConfigured(nowMs);

  if (wifiConnectedNow()) {
    const bool medalsOk = pollMedals(nowMs);
    const bool scheduleOk = pollSchedule(nowMs);
    lastMedalsPollMs = medalsOk ? nowMs : (nowMs - kMedalsPollIntervalMs);
    lastSchedulePollMs = scheduleOk ? nowMs : (nowMs - kSchedulePollIntervalMs);
  } else {
    lastMedalsPollMs = nowMs - kMedalsPollIntervalMs;
    lastSchedulePollMs = nowMs - kSchedulePollIntervalMs;
  }
  lastRotateMs = nowMs;
  lastWifiConnected = wifiConnectedNow();

  renderCurrentPage(nowMs);
}

void loop() {
  wifiTick();
  const uint32_t nowMs = millis();
  const bool wifi = wifiConnectedNow();
  bool shouldRender = false;
  bool resetRotateTimer = false;

  if (wifi) ensureTimeConfigured(nowMs);

  if (wifi && nowMs - lastMedalsPollMs >= kMedalsPollIntervalMs) {
    lastMedalsPollMs = nowMs;
    if (pollMedals(nowMs) && !alertActive) {
      shouldRender = true;
    }
  }

  if (wifi && nowMs - lastSchedulePollMs >= kSchedulePollIntervalMs) {
    lastSchedulePollMs = nowMs;
    if (pollSchedule(nowMs) && !alertActive) {
      shouldRender = true;
    }
  }

  maybeShowAlert(nowMs);

  if (alertActive && nowMs >= alertUntilMs) {
    alertActive = false;
    shouldRender = true;
    resetRotateTimer = true;
  }

  const bool wifiChanged = (wifi != lastWifiConnected);
  if (wifiChanged) {
    lastWifiConnected = wifi;
    if (!alertActive) {
      shouldRender = true;
    }
  }

  if (!alertActive && nowMs - lastRotateMs >= kRotateIntervalMs) {
    togglePage(nowMs);
    shouldRender = false;
    resetRotateTimer = false;
  } else if (!alertActive && shouldRender) {
    renderCurrentPage(nowMs);
    if (resetRotateTimer) lastRotateMs = nowMs;
  }

  delay(20);
}
