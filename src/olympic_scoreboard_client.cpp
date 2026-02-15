#include "olympic_scoreboard_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace {

static const char *kMedalsCountryUrl =
  "https://sdf.nbcolympics.com/v1/widget/medals/country?competitionCode=OWG2026";
static const char *kMedalsSportUrlPrefix =
  "https://sdf.nbcolympics.com/v1/widget/medals/sport?competitionCode=OWG2026&sportCode=";
static const char *kScheduleUrlPrefix =
  "https://schedules.nbcolympics.com/api/v1/schedule?startDate=";
static const char *kMedalsApiHeaderValue = "daaacddd-1513-46a3-8b79-ac3584258f5b";

class ChunkedStream : public Stream {
public:
  explicit ChunkedStream(Stream &src) : _src(src) {}

  int available() override {
    if (_done) return 0;
    if (_peeked >= 0) return 1;
    if (_remaining > 0) {
      int avail = _src.available();
      if (avail > _remaining) avail = _remaining;
      return avail;
    }
    return _src.available();
  }

  int read() override {
    if (_peeked >= 0) {
      const int c = _peeked;
      _peeked = -1;
      return c;
    }
    if (_done) return -1;
    if (_remaining == 0 && !readChunkHeader()) return -1;

    const int c = _src.read();
    if (c < 0) return -1;
    _remaining--;
    if (_remaining == 0) consumeCrlf();
    return c;
  }

  int peek() override {
    if (_peeked < 0) _peeked = read();
    return _peeked;
  }

  void flush() override {}
  size_t write(uint8_t) override { return 0; }

private:
  Stream &_src;
  int _peeked = -1;
  int _remaining = 0;
  bool _done = false;

  bool readChunkHeader() {
    char line[24];
    size_t n = _src.readBytesUntil('\n', line, sizeof(line) - 1);
    if (n == 0) return false;
    line[n] = '\0';
    if (n && line[n - 1] == '\r') line[n - 1] = '\0';
    char *semi = strchr(line, ';');
    if (semi) *semi = '\0';
    _remaining = (int)strtol(line, nullptr, 16);
    if (_remaining == 0) {
      _done = true;
      return false;
    }
    return true;
  }

  void consumeCrlf() {
    (void)_src.read();
    (void)_src.read();
  }
};

static String asUpper(const String &s) {
  String out = s;
  out.trim();
  out.toUpperCase();
  return out;
}

struct WinterSportDef {
  const char *code;
  const char *name;
};

}  // namespace

static const WinterSportDef kWinterSports[kWinterSportCount] = {
  {"ALP", "Alpine Skiing"},
  {"BTH", "Biathlon"},
  {"BOB", "Bobsled"},
  {"CCS", "Cross-Country Skiing"},
  {"CUR", "Curling"},
  {"FSK", "Figure Skating"},
  {"FRS", "Freestyle Skiing"},
  {"IHO", "Hockey"},
  {"LUG", "Luge"},
  {"NCB", "Nordic Combined"},
  {"SBD", "Snowboarding"},
  {"SKN", "Skeleton"},
  {"SJP", "Ski Jumping"},
  {"SMT", "Ski Mountaineering"},
  {"SSK", "Speed Skating"},
  {"STK", "Short Track"},
};

bool OlympicScoreboardClient::httpGetJson(const String &url,
                                          JsonDocument &doc,
                                          const JsonDocument *filter,
                                          bool useMedalsAuth) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(12000);

  HTTPClient http;
  http.setTimeout(12000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  if (!http.begin(client, url)) return false;
  http.addHeader("User-Agent", "olympic-scoreboard-esp32");
  http.addHeader("Accept", "application/json");
  if (useMedalsAuth) {
    http.addHeader("x-olyapiauth", kMedalsApiHeaderValue);
  }

  const int code = http.GET();
  if (code != 200) {
    Serial.printf("HTTP %d: %s\n", code, url.c_str());
    http.end();
    return false;
  }

  const String transferEncoding = http.header("Transfer-Encoding");
  const int contentLen = http.getSize();
  const bool useChunked = transferEncoding.equalsIgnoreCase("chunked") || contentLen < 0;
  Stream &stream = http.getStream();
  const auto nesting = DeserializationOption::NestingLimit(24);
  DeserializationError err;
  if (useChunked) {
    ChunkedStream chunked(stream);
    err = filter ? deserializeJson(doc, chunked, DeserializationOption::Filter(*filter), nesting)
                 : deserializeJson(doc, chunked, nesting);
  } else {
    err = filter ? deserializeJson(doc, stream, DeserializationOption::Filter(*filter), nesting)
                 : deserializeJson(doc, stream, nesting);
  }

  http.end();
  if (err) {
    Serial.printf("JSON parse error: %s\n", err.c_str());
    return false;
  }
  return true;
}

bool OlympicScoreboardClient::fetchMedalTable(MedalTableState &out, const String &favoriteCountryCode) {
  out = MedalTableState();

  JsonDocument filter;
  filter[0]["countryName"] = true;
  filter[0]["countryCode"] = true;
  filter[0]["gold"] = true;
  filter[0]["silver"] = true;
  filter[0]["bronze"] = true;
  filter[0]["medalTotal"] = true;
  filter[0]["medalRank"] = true;
  filter[0]["flagUrl"]["small"] = true;
  filter[0]["flagUrl"]["medium"] = true;

  JsonDocument doc;
  if (!httpGetJson(String(kMedalsCountryUrl), doc, &filter, true)) {
    return false;
  }

  const String fav = asUpper(favoriteCountryCode);
  JsonArrayConst rows = doc.as<JsonArrayConst>();
  if (rows.isNull()) return false;

  for (JsonObjectConst row : rows) {
    if (out.rowCount >= kMaxMedalRows) break;
    MedalRow &dst = out.rows[out.rowCount++];

    dst.countryName = String((const char *)(row["countryName"] | ""));
    dst.countryCode = asUpper(String((const char *)(row["countryCode"] | "")));
    dst.gold = (uint16_t)(row["gold"] | 0);
    dst.silver = (uint16_t)(row["silver"] | 0);
    dst.bronze = (uint16_t)(row["bronze"] | 0);
    dst.total = (uint16_t)(row["medalTotal"] | 0);
    dst.rank = (uint16_t)(row["medalRank"] | 0);
    dst.flagUrlSmall = String((const char *)(row["flagUrl"]["small"] | ""));
    dst.flagUrlMedium = String((const char *)(row["flagUrl"]["medium"] | ""));

    if (dst.countryCode == fav) {
      out.hasFavorite = true;
      out.favoriteIndex = (int8_t)(out.rowCount - 1);
      out.favoriteGold = dst.gold;
      out.favoriteSilver = dst.silver;
      out.favoriteBronze = dst.bronze;
      out.favoriteTotal = dst.total;
    }
  }

  if (!out.hasFavorite) {
    out.favoriteGold = 0;
    out.favoriteSilver = 0;
    out.favoriteBronze = 0;
    out.favoriteTotal = 0;
  }
  out.valid = true;
  return true;
}

void OlympicScoreboardClient::sortScheduleRows(DailyScheduleState &schedule) {
  for (uint8_t i = 0; i < schedule.rowCount; ++i) {
    for (uint8_t j = (uint8_t)(i + 1); j < schedule.rowCount; ++j) {
      if (schedule.rows[j].startEpoch < schedule.rows[i].startEpoch) {
        CompetitionRow tmp = schedule.rows[i];
        schedule.rows[i] = schedule.rows[j];
        schedule.rows[j] = tmp;
      }
    }
  }
}

bool OlympicScoreboardClient::fetchDailySchedule(DailyScheduleState &out, const String &startDateYmd) {
  out = DailyScheduleState();
  out.dateYmd = startDateYmd;

  if (startDateYmd.length() < 8) return false;

  JsonDocument filter;
  filter["data"][0]["singleEvent"]["title"] = true;
  filter["data"][0]["singleEvent"]["shortTitle"] = true;
  filter["data"][0]["singleEvent"]["startDate"] = true;
  filter["data"][0]["singleEvent"]["status"] = true;
  filter["data"][0]["singleEvent"]["isMedalSession"] = true;
  filter["data"][0]["singleEvent"]["gameType"] = true;
  filter["data"][0]["sports"][0]["code"] = true;
  filter["data"][0]["sports"][0]["shortDisplayTitle"] = true;
  filter["data"][0]["sports"][0]["title"] = true;

  JsonDocument doc;
  String url = String(kScheduleUrlPrefix) + startDateYmd;
  if (!httpGetJson(url, doc, &filter, false)) {
    String compact = startDateYmd;
    compact.replace("-", "");
    if (compact == startDateYmd) return false;
    url = String(kScheduleUrlPrefix) + compact;
    if (!httpGetJson(url, doc, &filter, false)) return false;
  }

  JsonArrayConst data = doc["data"].as<JsonArrayConst>();
  if (data.isNull()) return false;

  for (JsonObjectConst item : data) {
    if (out.rowCount >= kMaxScheduleRows) break;
    JsonObjectConst singleEvent = item["singleEvent"].as<JsonObjectConst>();
    if (singleEvent.isNull()) continue;

    const String gameType = String((const char *)(singleEvent["gameType"] | ""));
    if (gameType.length() && !gameType.equalsIgnoreCase("olympics")) continue;

    CompetitionRow row;
    row.startEpoch = (time_t)(singleEvent["startDate"] | 0);
    if (row.startEpoch <= 0) continue;
    row.status = String((const char *)(singleEvent["status"] | ""));
    row.isMedalSession = singleEvent["isMedalSession"] | false;
    row.title = String((const char *)(singleEvent["shortTitle"] | ""));
    if (!row.title.length()) {
      row.title = String((const char *)(singleEvent["title"] | ""));
    }

    JsonVariantConst sportsVar = item["sports"];
    JsonObjectConst sportObj;
    if (sportsVar.is<JsonArrayConst>()) {
      JsonArrayConst arr = sportsVar.as<JsonArrayConst>();
      if (!arr.isNull() && arr.size() > 0) sportObj = arr[0];
    } else if (sportsVar.is<JsonObjectConst>()) {
      sportObj = sportsVar.as<JsonObjectConst>();
    }

    if (!sportObj.isNull()) {
      row.sportCode = String((const char *)(sportObj["code"] | ""));
      row.sportName = String((const char *)(sportObj["shortDisplayTitle"] | ""));
      if (!row.sportName.length()) row.sportName = String((const char *)(sportObj["title"] | ""));
    }

    if (!row.sportCode.length()) row.sportCode = "---";
    row.sportCode.toUpperCase();
    if (!row.sportName.length()) row.sportName = "Olympics";
    if (!row.title.length()) row.title = row.sportName;

    out.rows[out.rowCount++] = row;
  }

  sortScheduleRows(out);
  out.valid = true;
  return true;
}

bool OlympicScoreboardClient::fetchFavoriteSportCountsOne(const String &favoriteCountryCode,
                                                          const char *sportCode,
                                                          SportMedalCounts &outCounts) {
  outCounts = SportMedalCounts();

  JsonDocument filter;
  filter[0]["countryCode"] = true;
  filter[0]["gold"] = true;
  filter[0]["silver"] = true;
  filter[0]["bronze"] = true;

  JsonDocument doc;
  const String url = String(kMedalsSportUrlPrefix) + String(sportCode);
  if (!httpGetJson(url, doc, &filter, true)) return false;

  const String fav = asUpper(favoriteCountryCode);
  JsonArrayConst rows = doc.as<JsonArrayConst>();
  if (rows.isNull()) return false;

  for (JsonObjectConst row : rows) {
    String code = asUpper(String((const char *)(row["countryCode"] | "")));
    if (code != fav) continue;
    outCounts.gold = (uint16_t)(row["gold"] | 0);
    outCounts.silver = (uint16_t)(row["silver"] | 0);
    outCounts.bronze = (uint16_t)(row["bronze"] | 0);
    break;
  }
  return true;
}

bool OlympicScoreboardClient::fetchFavoriteSportCounts(const String &favoriteCountryCode,
                                                       SportMedalCounts *outCounts,
                                                       uint8_t count) {
  bool ok = true;
  for (uint8_t i = 0; i < count; ++i) {
    if (!fetchFavoriteSportCountsOne(favoriteCountryCode, kWinterSports[i].code, outCounts[i])) {
      ok = false;
    }
    delay(10);
    yield();
  }
  return ok;
}

bool OlympicScoreboardClient::primeFavoriteSportBaseline(const String &favoriteCountryCode) {
  SportMedalCounts latest[kWinterSportCount];
  if (!fetchFavoriteSportCounts(favoriteCountryCode, latest, kWinterSportCount)) {
    return false;
  }
  for (uint8_t i = 0; i < kWinterSportCount; ++i) {
    _sportBaseline[i] = latest[i];
  }
  _sportBaselineValid = true;
  return true;
}

bool OlympicScoreboardClient::buildFavoriteMedalAlert(const MedalTableState &prev,
                                                      const MedalTableState &curr,
                                                      const String &favoriteCountryCode,
                                                      MedalAlertEvent &out) {
  out = MedalAlertEvent();
  if (!prev.valid || !curr.valid) return false;

  const int dGold = (int)curr.favoriteGold - (int)prev.favoriteGold;
  const int dSilver = (int)curr.favoriteSilver - (int)prev.favoriteSilver;
  const int dBronze = (int)curr.favoriteBronze - (int)prev.favoriteBronze;
  if (dGold <= 0 && dSilver <= 0 && dBronze <= 0) return false;

  MedalType alertType = MedalType::UNKNOWN;
  uint8_t alertDelta = 0;
  if (dGold > 0) {
    alertType = MedalType::GOLD;
    alertDelta = (uint8_t)dGold;
  } else if (dSilver > 0) {
    alertType = MedalType::SILVER;
    alertDelta = (uint8_t)dSilver;
  } else if (dBronze > 0) {
    alertType = MedalType::BRONZE;
    alertDelta = (uint8_t)dBronze;
  }

  out.valid = true;
  out.medalType = alertType;
  out.delta = alertDelta;
  out.sportCode = "---";
  out.sportName = "Olympic Event";

  SportMedalCounts latest[kWinterSportCount];
  if (fetchFavoriteSportCounts(favoriteCountryCode, latest, kWinterSportCount)) {
    int bestIdx = -1;
    int bestDelta = 0;

    if (_sportBaselineValid) {
      for (uint8_t i = 0; i < kWinterSportCount; ++i) {
        int d = 0;
        if (alertType == MedalType::GOLD) {
          d = (int)latest[i].gold - (int)_sportBaseline[i].gold;
        } else if (alertType == MedalType::SILVER) {
          d = (int)latest[i].silver - (int)_sportBaseline[i].silver;
        } else if (alertType == MedalType::BRONZE) {
          d = (int)latest[i].bronze - (int)_sportBaseline[i].bronze;
        }
        if (d > bestDelta) {
          bestDelta = d;
          bestIdx = i;
        }
      }
    }

    for (uint8_t i = 0; i < kWinterSportCount; ++i) {
      _sportBaseline[i] = latest[i];
    }
    _sportBaselineValid = true;

    if (bestIdx >= 0) {
      out.sportCode = kWinterSports[bestIdx].code;
      out.sportName = kWinterSports[bestIdx].name;
    }
  }

  return true;
}
