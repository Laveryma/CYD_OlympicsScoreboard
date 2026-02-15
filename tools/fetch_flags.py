#!/usr/bin/env python3
"""Fetch Olympic medal-country flags into data/flags for SPIFFS upload.

Usage (PowerShell):
  python tools/fetch_flags.py
  python tools/fetch_flags.py --competition OWG2026 --out data/flags

Notes:
- Source URLs may be PNG/JPEG/GIF depending on country.
- If Pillow is installed, non-PNG inputs are converted to PNG.
- Without Pillow, non-PNG images are skipped.
"""

from __future__ import annotations

import argparse
import io
import json
import os
import shutil
import sys
import urllib.error
import urllib.request
from typing import Dict, Optional

MEDALS_URL = "https://sdf.nbcolympics.com/v1/widget/medals/country?competitionCode={competition}"
MEDALS_AUTH_HEADER = "daaacddd-1513-46a3-8b79-ac3584258f5b"
DEFAULT_COMPETITION = "OWG2026"
SIZES = (56, 64, 96)

try:
    from PIL import Image

    PIL_AVAILABLE = True
except Exception:  # noqa: BLE001
    Image = None
    PIL_AVAILABLE = False


def fetch_json(url: str) -> list[dict]:
    req = urllib.request.Request(
        url,
        headers={
            "User-Agent": "flag-fetcher/2.0",
            "Accept": "application/json",
            "x-olyapiauth": MEDALS_AUTH_HEADER,
        },
    )
    with urllib.request.urlopen(req, timeout=30) as resp:
        return json.loads(resp.read().decode("utf-8"))


def download_bytes(url: str) -> Optional[bytes]:
    req = urllib.request.Request(url, headers={"User-Agent": "flag-fetcher/2.0", "Accept": "image/*"})
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            return resp.read()
    except urllib.error.URLError as exc:
        print(f"  ! {url} -> {exc}")
        return None


def to_png_bytes(data: bytes) -> Optional[bytes]:
    # PNG signature.
    if data.startswith(b"\x89PNG\r\n\x1a\n"):
        return data

    if not PIL_AVAILABLE:
        return None

    try:
        with Image.open(io.BytesIO(data)) as img:
            out = io.BytesIO()
            img.convert("RGBA").save(out, format="PNG")
            return out.getvalue()
    except Exception:  # noqa: BLE001
        return None


def save_resized_png(data: bytes, path: str, target_w: int) -> bool:
    if PIL_AVAILABLE:
        try:
            with Image.open(io.BytesIO(data)) as img:
                img = img.convert("RGBA")
                src_w, src_h = img.size
                if src_w <= 0 or src_h <= 0:
                    return False
                target_h = max(1, int(round(target_w * src_h / src_w)))
                resampling = Image.Resampling.LANCZOS if hasattr(Image, "Resampling") else Image.LANCZOS
                img = img.resize((target_w, target_h), resample=resampling)
                os.makedirs(os.path.dirname(path), exist_ok=True)
                img.save(path, format="PNG")
                return True
        except Exception:  # noqa: BLE001
            return False

    png = to_png_bytes(data)
    if not png:
        return False

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)
    return True


def collect_flags(rows: list[dict]) -> Dict[str, str]:
    out: Dict[str, str] = {}
    for row in rows:
        abbr = str(row.get("countryCode", "")).strip().upper()
        flag = row.get("flagUrl", {}) if isinstance(row.get("flagUrl", {}), dict) else {}
        logo = str(flag.get("medium") or flag.get("small") or "").strip()
        if abbr and logo:
            out[abbr] = logo
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description="Fetch Olympics medal flags into data/flags")
    parser.add_argument("--competition", default=DEFAULT_COMPETITION, help="competition code, e.g. OWG2026")
    parser.add_argument("--out", default=os.path.join("data", "flags"), help="output root folder")
    args = parser.parse_args()

    url = MEDALS_URL.format(competition=args.competition)
    print(f"Fetching medals feed: {url}")

    try:
        payload = fetch_json(url)
    except Exception as exc:  # noqa: BLE001
        print(f"Failed to fetch medals JSON: {exc}")
        return 1

    if not isinstance(payload, list):
        print("Unexpected medals payload type")
        return 1

    flags = collect_flags(payload)
    if not flags:
        print("No country flags found in payload")
        return 1

    print(f"Found {len(flags)} countries")
    if not PIL_AVAILABLE:
        print("Pillow not found: non-PNG source images will be skipped")

    ok_count = 0
    for abbr in sorted(flags):
        base_url = flags[abbr]
        print(f"- {abbr}")
        base_bytes = download_bytes(base_url)
        if not base_bytes:
            continue

        for size in SIZES:
            out_path = os.path.join(args.out, str(size), f"{abbr}.png")
            ok = save_resized_png(base_bytes, out_path, size)
            if ok:
                ok_count += 1

        # Canonical fallback cache key expected by runtime fallback.
        src96 = os.path.join(args.out, "96", f"{abbr}.png")
        flat = os.path.join(args.out, f"{abbr}.png")
        if os.path.exists(src96):
            os.makedirs(os.path.dirname(flat), exist_ok=True)
            shutil.copyfile(src96, flat)

    print(f"Done. Downloaded {ok_count} sized PNG files into {args.out}")
    print("Upload to SPIFFS with: pio run -e esp32-cyd-sdfix -t uploadfs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
