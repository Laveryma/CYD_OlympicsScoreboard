#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

enum class MedalType : uint8_t {
  GOLD,
  SILVER,
  BRONZE,
  UNKNOWN
};

static const uint8_t kMaxMedalRows = 24;
static const uint8_t kMaxScheduleRows = 40;
static const uint8_t kWinterSportCount = 16;

struct MedalRow {
  String countryCode;
  String countryName;
  String flagUrlSmall;
  String flagUrlMedium;
  uint16_t gold = 0;
  uint16_t silver = 0;
  uint16_t bronze = 0;
  uint16_t total = 0;
  uint16_t rank = 0;
};

struct MedalTableState {
  bool valid = false;
  uint8_t rowCount = 0;
  MedalRow rows[kMaxMedalRows];
  bool hasFavorite = false;
  int8_t favoriteIndex = -1;
  uint16_t favoriteGold = 0;
  uint16_t favoriteSilver = 0;
  uint16_t favoriteBronze = 0;
  uint16_t favoriteTotal = 0;
};

struct CompetitionRow {
  time_t startEpoch = 0;
  String status;
  String sportCode;
  String sportName;
  String title;
  bool isMedalSession = false;
};

struct DailyScheduleState {
  bool valid = false;
  String dateYmd;
  uint8_t rowCount = 0;
  CompetitionRow rows[kMaxScheduleRows];
};

struct MedalAlertEvent {
  bool valid = false;
  MedalType medalType = MedalType::UNKNOWN;
  uint8_t delta = 0;
  String sportCode;
  String sportName;
};

class OlympicScoreboardClient {
public:
  bool fetchMedalTable(MedalTableState &out, const String &favoriteCountryCode);
  bool fetchDailySchedule(DailyScheduleState &out, const String &startDateYmd);
  bool primeFavoriteSportBaseline(const String &favoriteCountryCode);
  bool buildFavoriteMedalAlert(const MedalTableState &prev,
                               const MedalTableState &curr,
                               const String &favoriteCountryCode,
                               MedalAlertEvent &out);

private:
  struct SportMedalCounts {
    uint16_t gold = 0;
    uint16_t silver = 0;
    uint16_t bronze = 0;
  };

  bool httpGetJson(const String &url,
                   ArduinoJson::JsonDocument &doc,
                   const ArduinoJson::JsonDocument *filter = nullptr,
                   bool useMedalsAuth = false);
  bool fetchFavoriteSportCounts(const String &favoriteCountryCode,
                                SportMedalCounts *outCounts,
                                uint8_t count);
  bool fetchFavoriteSportCountsOne(const String &favoriteCountryCode,
                                   const char *sportCode,
                                   SportMedalCounts &outCounts);
  void sortScheduleRows(DailyScheduleState &schedule);

  bool _sportBaselineValid = false;
  SportMedalCounts _sportBaseline[kWinterSportCount];
};
