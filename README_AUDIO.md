# Audio: Medal Alert Playback

The firmware can play `O Canada` during favorite-country medal alerts.

## Trigger

Audio playback is triggered when the medal table reports a positive medal delta for `FOCUS_TEAM_ABBR`.

- Alert popup duration: `kAlertPopupMs` (currently `6000` ms)
- Audio playback duration cap: `kAlertAudioMs` (currently `8000` ms)

## Required File

Place the WAV file at:

- SPIFFS path: `/audio/o_canada.wav`
- Project path before `uploadfs`: `data/audio/o_canada.wav`

## Supported WAV Format

Use uncompressed PCM WAV:

- Mono (`1` channel)
- Bit depth: `16-bit` (preferred) or `8-bit`
- Sample rate: `11025`, `16000`, or `22050` Hz recommended

Rejected formats include stereo, ADPCM, MP3, float WAV, etc.

## Upload

```powershell
pio run -e esp32-cyd-sdfix -t uploadfs
```

## Playback Controls

- During playback, each BOOT button click decreases gain by `10%` down to `0%`
- DAC output pins are controlled by `ANTHEM_DAC_PIN` and `ANTHEM_DAC_PIN_ALT` in `include/config.h`
- Default output gain is set by `ANTHEM_GAIN_PCT`
