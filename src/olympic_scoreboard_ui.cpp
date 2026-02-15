#include "olympic_scoreboard_ui.h"

#include <time.h>

#include "assets.h"
#include "config.h"
#include "palette.h"

namespace {

static inline void drawCentered(TFT_eSPI &tft,
                                const String &text,
                                int16_t x,
                                int16_t y,
                                int font,
                                uint16_t fg,
                                uint16_t bg) {
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(font);
  tft.setTextColor(fg, bg);
  tft.drawString(text, x, y);
}

static uint16_t medalColor(MedalType type) {
  switch (type) {
    case MedalType::GOLD: return 0xFEA0;
    case MedalType::SILVER: return 0xC618;
    case MedalType::BRONZE: return 0xC3C0;
    default: return Palette::WHITE;
  }
}

static const char *medalName(MedalType type) {
  switch (type) {
    case MedalType::GOLD: return "GOLD";
    case MedalType::SILVER: return "SILVER";
    case MedalType::BRONZE: return "BRONZE";
    default: return "MEDAL";
  }
}

}  // namespace

void OlympicScoreboardUi::begin(TFT_eSPI &tft, uint8_t rotation) {
  _tft = &tft;
  _rotation = (uint8_t)(rotation & 3);
  _tft->init();
  _tft->invertDisplay(false);
  _tft->setRotation(_rotation);
  _tft->resetViewport();
  _tft->fillScreen(Palette::BG);
}

void OlympicScoreboardUi::setRotation(uint8_t rotation) {
  if (!_tft) return;
  _rotation = (uint8_t)(rotation & 3);
  _tft->setRotation(_rotation);
  _tft->resetViewport();
  _tft->fillScreen(Palette::BG);
}

void OlympicScoreboardUi::setBacklight(uint8_t pct) {
  ledcWrite(CYD_BL_PWM_CH, map(pct, 0, 100, 0, 255));
}

void OlympicScoreboardUi::clearScreen() {
  if (!_tft) return;
  _tft->setRotation(_rotation);
  _tft->resetViewport();
  _tft->fillScreen(Palette::BG);
  _tft->drawRect(0, 0, _tft->width(), _tft->height(), Palette::FRAME);
}

String OlympicScoreboardUi::formatClock(time_t epoch) const {
  if (epoch <= 0) return "--:--";
  struct tm lt;
  localtime_r(&epoch, &lt);
  char buf[16];
  strftime(buf, sizeof(buf), "%H:%M", &lt);
  return String(buf);
}

String OlympicScoreboardUi::formatDate(time_t epoch) const {
  if (epoch <= 0) return "";
  struct tm lt;
  localtime_r(&epoch, &lt);
  char buf[24];
  strftime(buf, sizeof(buf), "%a %d %b", &lt);
  return String(buf);
}

String OlympicScoreboardUi::elideToWidth(const String &s, int maxPx, int font) const {
  if (!_tft) return s;
  if (maxPx <= 0 || _tft->textWidth(s, font) <= maxPx) return s;
  String out = s;
  while (out.length() > 0 && _tft->textWidth(out + "...", font) > maxPx) {
    out.remove(out.length() - 1);
  }
  if (!out.length()) return "...";
  return out + "...";
}

void OlympicScoreboardUi::drawBootSplash(const String &line1, const String &line2) {
  if (!_tft) return;
  clearScreen();

  const int16_t w = _tft->width();
  const int16_t h = _tft->height();
  if (Assets::drawPng(*_tft, "/splash.png", 0, 0)) {
    if (line2.length()) {
      _tft->fillRect(1, h - 20, w - 2, 18, Palette::BG);
      drawCentered(*_tft, line2, w / 2, h - 11, 2, Palette::WHITE, Palette::BG);
    }
    return;
  }

  _tft->fillRect(0, 0, w, 24, Palette::PANEL_2);
  drawCentered(*_tft, "MILANO CORTINA 2026", w / 2, 12, 2, Palette::WHITE, Palette::PANEL_2);
  drawCentered(*_tft, "OLYMPIC SCOREBOARD", w / 2, h / 2 - 12, 4, Palette::WHITE, Palette::BG);
  drawCentered(*_tft, line1, w / 2, h / 2 + 18, 2, Palette::GREY, Palette::BG);
  if (line2.length()) {
    drawCentered(*_tft, line2, w / 2, h - 12, 2, Palette::GREY, Palette::BG);
  }
}

void OlympicScoreboardUi::drawMedals(const MedalTableState &medals,
                                     const String &favoriteCountryCode,
                                     bool wifiConnected,
                                     bool stale) {
  if (!_tft) return;
  clearScreen();

  const int16_t w = _tft->width();
  const int16_t h = _tft->height();

  _tft->fillRect(1, 1, w - 2, 22, Palette::PANEL_2);
  drawCentered(*_tft, "MEDAL STANDINGS", w / 2, 12, 2, Palette::WHITE, Palette::PANEL_2);

  const int16_t xRank = 8;
  const int16_t xFlag = 26;
  const int16_t xCountry = 70;
  const int16_t xGold = 236;
  const int16_t xSilver = 264;
  const int16_t xBronze = 290;
  const int16_t xTotal = 316;

  _tft->setTextFont(1);
  _tft->setTextColor(Palette::GREY, Palette::BG);
  _tft->setTextDatum(ML_DATUM);
  _tft->drawString("#", xRank, 30);
  _tft->drawString("COUNTRY", xCountry, 30);
  _tft->setTextDatum(MR_DATUM);
  _tft->drawString("G", xGold, 30);
  _tft->drawString("S", xSilver, 30);
  _tft->drawString("B", xBronze, 30);
  _tft->drawString("T", xTotal, 30);

  if (!medals.valid || medals.rowCount == 0) {
    drawCentered(*_tft, "Waiting for medals feed...", w / 2, h / 2, 2, Palette::WHITE, Palette::BG);
  } else {
    String fav = favoriteCountryCode;
    fav.toUpperCase();
    const int16_t rowTop = 40;
    const int16_t rowH = 26;
    const int16_t footerH = 20;
    const int16_t maxRows = (h - rowTop - footerH - 2) / rowH;
    const int16_t rowsToDraw = min((int16_t)medals.rowCount, maxRows);
    bool favoriteDrawn = false;

    for (int16_t i = 0; i < rowsToDraw; ++i) {
      const MedalRow &row = medals.rows[i];
      const int16_t y = rowTop + i * rowH;
      const bool isFavorite = row.countryCode == fav;
      if (isFavorite) {
        _tft->fillRect(4, y, w - 8, rowH - 1, Palette::PANEL_2);
        favoriteDrawn = true;
      }
      _tft->drawFastHLine(4, (int16_t)(y + rowH - 1), w - 8, Palette::PANEL);

      _tft->setTextColor(isFavorite ? Palette::WHITE : Palette::GREY, isFavorite ? Palette::PANEL_2 : Palette::BG);
      _tft->setTextFont(1);
      _tft->setTextDatum(ML_DATUM);
      _tft->drawString(String(row.rank), xRank, (int16_t)(y + rowH / 2));

      const int16_t flagSize = 12;
      const int16_t flagY = (int16_t)(y + rowH / 2 - flagSize / 2);
      const String flagUrl = row.flagUrlSmall.length() ? row.flagUrlSmall : row.flagUrlMedium;
      Assets::drawLogo(*_tft, row.countryCode, flagUrl, xFlag, flagY, flagSize);

      _tft->drawString(elideToWidth(row.countryName, 156, 2), xCountry, (int16_t)(y + rowH / 2));
      _tft->setTextDatum(MR_DATUM);
      _tft->drawString(String(row.gold), xGold, (int16_t)(y + rowH / 2));
      _tft->drawString(String(row.silver), xSilver, (int16_t)(y + rowH / 2));
      _tft->drawString(String(row.bronze), xBronze, (int16_t)(y + rowH / 2));
      _tft->drawString(String(row.total), xTotal, (int16_t)(y + rowH / 2));
    }

    if (!favoriteDrawn) {
      const int16_t y = h - 40;
      _tft->fillRect(4, y, w - 8, rowH - 1, Palette::PANEL_2);
      _tft->drawFastHLine(4, (int16_t)(y + rowH - 1), w - 8, Palette::PANEL);
      _tft->setTextColor(Palette::WHITE, Palette::PANEL_2);
      _tft->setTextFont(1);
      _tft->setTextDatum(ML_DATUM);
      _tft->drawString("CAN", xRank, (int16_t)(y + rowH / 2));
      Assets::drawLogo(*_tft, "CAN", "https://images.nbcolympics.com/country-flags/38x25/can.png", xFlag, (int16_t)(y + rowH / 2 - 6), 12);
      _tft->drawString("Canada", xCountry, (int16_t)(y + rowH / 2));
      _tft->setTextDatum(MR_DATUM);
      _tft->drawString(String(medals.favoriteGold), xGold, (int16_t)(y + rowH / 2));
      _tft->drawString(String(medals.favoriteSilver), xSilver, (int16_t)(y + rowH / 2));
      _tft->drawString(String(medals.favoriteBronze), xBronze, (int16_t)(y + rowH / 2));
      _tft->drawString(String(medals.favoriteTotal), xTotal, (int16_t)(y + rowH / 2));
    }
  }

  _tft->fillRect(1, h - 18, w - 2, 17, Palette::PANEL);
  _tft->setTextFont(1);
  _tft->setTextDatum(ML_DATUM);
  _tft->setTextColor(Palette::GREY, Palette::PANEL);
  String left = wifiConnected ? "ONLINE" : "OFFLINE";
  if (stale) left += " | STALE";
  _tft->drawString(left, 6, h - 9);
  _tft->setTextDatum(MR_DATUM);
  _tft->drawString("CAN HIGHLIGHT", w - 6, h - 9);
}

void OlympicScoreboardUi::drawSchedule(const DailyScheduleState &schedule,
                                       bool wifiConnected,
                                       bool stale) {
  if (!_tft) return;
  clearScreen();

  const int16_t w = _tft->width();
  const int16_t h = _tft->height();

  _tft->fillRect(1, 1, w - 2, 22, Palette::PANEL_2);
  drawCentered(*_tft, "TODAY'S COMPETITIONS", w / 2, 12, 2, Palette::WHITE, Palette::PANEL_2);

  _tft->setTextFont(1);
  _tft->setTextColor(Palette::GREY, Palette::BG);
  _tft->setTextDatum(ML_DATUM);
  _tft->drawString("TIME", 8, 30);
  _tft->drawString("SPORT", 68, 30);
  _tft->drawString("EVENT", 116, 30);

  if (!schedule.valid || schedule.rowCount == 0) {
    drawCentered(*_tft, "Waiting for schedule feed...", w / 2, h / 2, 2, Palette::WHITE, Palette::BG);
  } else {
    const int16_t rowTop = 42;
    const int16_t rowH = 19;
    const int16_t footerH = 20;
    const int16_t rowsToDraw = min((int16_t)schedule.rowCount, (int16_t)((h - rowTop - footerH - 2) / rowH));

    for (int16_t i = 0; i < rowsToDraw; ++i) {
      const CompetitionRow &row = schedule.rows[i];
      const int16_t y = rowTop + i * rowH;
      const bool isLive = row.status.equalsIgnoreCase("live");
      const uint16_t bg = isLive ? Palette::PANEL_2 : Palette::BG;
      const uint16_t fg = isLive ? Palette::WHITE : Palette::GREY;
      if (isLive) _tft->fillRect(4, y - 7, w - 8, rowH - 1, bg);

      _tft->setTextFont(1);
      _tft->setTextDatum(ML_DATUM);
      _tft->setTextColor(fg, bg);
      _tft->drawString(formatClock(row.startEpoch), 8, y);
      _tft->drawString(row.sportCode, 68, y);
      _tft->drawString(elideToWidth(row.title, w - 122, 1), 116, y);

      if (row.isMedalSession) {
        _tft->fillCircle(108, y, 3, Palette::GOLD);
      }
    }
  }

  _tft->fillRect(1, h - 18, w - 2, 17, Palette::PANEL);
  _tft->setTextFont(1);
  _tft->setTextDatum(ML_DATUM);
  _tft->setTextColor(Palette::GREY, Palette::PANEL);
  String left = wifiConnected ? "ONLINE" : "OFFLINE";
  if (stale) left += " | STALE";
  _tft->drawString(left, 6, h - 9);
  _tft->setTextDatum(MR_DATUM);
  _tft->drawString(schedule.dateYmd, w - 6, h - 9);
}

void OlympicScoreboardUi::drawMedalAlert(const MedalAlertEvent &alert, const String &favoriteCountryCode) {
  if (!_tft) return;
  clearScreen();

  const int16_t w = _tft->width();
  const int16_t h = _tft->height();
  const uint16_t medalCol = medalColor(alert.medalType);

  _tft->fillRect(1, 1, w - 2, 26, Palette::PANEL_2);
  drawCentered(*_tft, "CANADA MEDAL ALERT", w / 2, 13, 2, Palette::WHITE, Palette::PANEL_2);

  const int16_t medalCx = 94;
  const int16_t medalCy = 116;
  _tft->fillCircle(medalCx + 3, medalCy + 3, 38, Palette::PANEL);
  _tft->fillCircle(medalCx, medalCy, 38, medalCol);
  _tft->drawCircle(medalCx, medalCy, 38, Palette::WHITE);
  _tft->drawCircle(medalCx, medalCy, 34, Palette::WHITE);

  _tft->setTextColor(Palette::WHITE, medalCol);
  _tft->setTextFont(2);
  _tft->setTextDatum(MC_DATUM);
  _tft->drawString(String(medalName(alert.medalType)), medalCx, medalCy - 6);
  if (alert.delta > 1) {
    _tft->drawString("x" + String(alert.delta), medalCx, medalCy + 14);
  } else {
    _tft->drawString("+1", medalCx, medalCy + 14);
  }

  Assets::drawLogo(*_tft, favoriteCountryCode, 190, 78, 72);
  _tft->setTextColor(Palette::WHITE, Palette::BG);
  _tft->setTextFont(2);
  _tft->drawString("CAN", 226, 160);
  _tft->setTextColor(Palette::GREY, Palette::BG);
  _tft->drawString(elideToWidth(alert.sportName, w - 16, 2), w / 2, 198);
}
