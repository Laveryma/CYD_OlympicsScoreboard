#pragma once

#include <TFT_eSPI.h>

#include "olympic_scoreboard_client.h"

class OlympicScoreboardUi {
public:
  void begin(TFT_eSPI &tft, uint8_t rotation);
  void setRotation(uint8_t rotation);
  void setBacklight(uint8_t pct);

  void drawBootSplash(const String &line1, const String &line2);
  void drawMedals(const MedalTableState &medals,
                  const String &favoriteCountryCode,
                  bool wifiConnected,
                  bool stale);
  void drawSchedule(const DailyScheduleState &schedule,
                    bool wifiConnected,
                    bool stale);
  void drawMedalAlert(const MedalAlertEvent &alert, const String &favoriteCountryCode);

private:
  TFT_eSPI *_tft = nullptr;
  uint8_t _rotation = 1;

  void clearScreen();
  String formatClock(time_t epoch) const;
  String formatDate(time_t epoch) const;
  String elideToWidth(const String &s, int maxPx, int font) const;
};
