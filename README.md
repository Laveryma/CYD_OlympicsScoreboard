# CYD Olympic Scoreboard (Milano Cortina 2026)

ESP32-2432S028 (CYD) firmware for an Olympics dashboard with:

- live medal table
- daily event schedule
- favorite-country medal alerts
- optional alert audio playback from SPIFFS

## Features

- Medal table from NBC Olympics medals API (`OWG2026`)
- Daily schedule from NBC Olympics schedule API
- Favorite country highlight (`FOCUS_TEAM_ABBR`) and medal delta alerts
- Full-screen alert popup when favorite country wins new medals
- Optional audio playback on alert (`/audio/o_canada.wav`)
- Automatic page rotation between `MEDALS` and `SCHEDULE`
- SPIFFS-first country flag loading with runtime cache fallback

## Build Environment

Default PlatformIO environment is pinned for reproducibility:

```powershell
pio run -e esp32-cyd-sdfix
```

## Flashing

Firmware upload:

```powershell
pio run -e esp32-cyd-sdfix -t upload
```

SPIFFS upload:

```powershell
pio run -e esp32-cyd-sdfix -t uploadfs
```

Clean + full erase + reflash sequence:

```powershell
pio run -e esp32-cyd-sdfix -t clean
pio run -e esp32-cyd-sdfix -t erase
pio run -e esp32-cyd-sdfix -t upload
pio run -e esp32-cyd-sdfix -t uploadfs
```

## Configuration

Edit `include/config.h`:

- `WIFI_SSID_1` / `WIFI_PASSWORD_1`
- optional fallback Wi-Fi: `WIFI_SSID_2` / `WIFI_PASSWORD_2`
- `FOCUS_TEAM_ABBR` (favorite country NOC code, e.g. `CAN`, `USA`, `NOR`)
- `TZ_INFO` (local time/countdown display)
- `ANTHEM_DAC_PIN`, `ANTHEM_DAC_PIN_ALT`, `ANTHEM_GAIN_PCT`

Use `include/config.example.h` as the template when creating a new config.

## SPIFFS Assets

Expected paths under `data/`:

- `data/audio/o_canada.wav` <--find and update if your not lucky enough to be Canadian :) must be .wav, 8-bit works if size is an issue, best audio if 16-bit
- `data/flags/56/<NOC>.png`
- `data/flags/64/<NOC>.png`
- `data/flags/96/<NOC>.png`
- optional fallback `data/flags/<NOC>.png`

See `README_AUDIO.md` for audio format details.

## Data Sources

- Medals by country:
  `https://sdf.nbcolympics.com/v1/widget/medals/country?competitionCode=OWG2026`
- Medals by sport (favorite-country alert attribution):
  `https://sdf.nbcolympics.com/v1/widget/medals/sport?competitionCode=OWG2026&sportCode=<CODE>`
- Daily schedule:
  `https://schedules.nbcolympics.com/api/v1/schedule?startDate=YYYY-MM-DD`

## Support

If you enjoy what I’m making and want to support more late-night builds, experiments, and ideas turning into reality, it's genuinely appreciated.

[<img src="docs/buymeacoffee-icon.svg" alt="Buy Me a Coffee" width="36" /> Buy Me a Coffee](https://buymeacoffee.com/zerocypherxiii)

