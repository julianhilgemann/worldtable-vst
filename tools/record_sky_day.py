#!/usr/bin/env python3
"""Record a magnetosphere "sky day" for Worldtable (spec/tasks/T1.1-storm-day-dataset.md).

Fetches, for one station and a UTC span:
  geomag_<STN>.json  USGS Geomagnetism, H component, 1 min        (US public domain)
  kp.json            GFZ Potsdam Kp, 3-hourly                      (CC BY 4.0)
  solarwind.json     NASA OMNI 1-min (OMNI_HRO_1MIN) via HAPI      (public, cite OMNI)
stores the responses verbatim, and derives
  track.json         one 1-minute grid: h, kp, v, n, bz, by (null = gap)
  manifest.json      span, sources, licences, units, summary, why this day

Usage:
  python3 tools/record_sky_day.py --day 2024-05-10 --why "Gannon storm, Kp 9"
  python3 tools/record_sky_day.py --live-fixtures      # SWPC products → tests/fixtures/

Python standard library only. Rerunning reproduces track.json byte for byte and
manifest.json except `retrieved_at`; raw responses carry server timestamps (USGS
`metadata.generated`) and are kept verbatim.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import statistics
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
USER_AGENT = "Worldtable/0.1 (+https://github.com/julianhilgemann/worldtable-vst)"

USGS_URL = "https://geomag.usgs.gov/ws/data/"
GFZ_URL = "https://kp.gfz.de/app/json/"
HAPI_URL = "https://cdaweb.gsfc.nasa.gov/hapi/data"
OMNI_DATASET = "OMNI_HRO_1MIN"
OMNI_PARAMS = ["BY_GSM", "BZ_GSM", "flow_speed", "proton_density"]  # HAPI wants dataset order

SWPC_LIVE = {
    # fixture path (under tests/fixtures/)       url                                                     keep newest N entries (None = verbatim)
    "kp/swpc_noaa-planetary-k-index":   ("https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json", None),
    "kp/swpc_planetary_k_index_1m":     ("https://services.swpc.noaa.gov/json/planetary_k_index_1m.json", None),
    # RTSW: ~24 h, 1.4/2.4 MB, newest first, several spacecraft interleaved (`active` marks the one in use);
    # 120 entries ≈ 40 min, enough for parser tests.
    "solarwind/swpc_rtsw_mag_1m":       ("https://services.swpc.noaa.gov/json/rtsw/rtsw_mag_1m.json", 120),
    "solarwind/swpc_rtsw_wind_1m":      ("https://services.swpc.noaa.gov/json/rtsw/rtsw_wind_1m.json", 120),
}

# Track channels: name → (unit, decimals)
CHANNELS = {
    "h":  ("nT", 2),       # CMO H component, absolute
    "kp": ("Kp", 3),       # planetary Kp, stepped per 3 h interval
    "v":  ("km/s", 1),     # solar-wind flow speed
    "n":  ("cm^-3", 2),    # proton density
    "bz": ("nT", 2),       # IMF Bz, GSM
    "by": ("nT", 2),       # IMF By, GSM
}

ISO = "%Y-%m-%dT%H:%M:%SZ"


def iso(t: dt.datetime) -> str:
    return t.strftime(ISO)


def parse_iso(s: str) -> dt.datetime:
    s = s.rstrip("Z").split(".")[0]
    return dt.datetime.strptime(s, "%Y-%m-%dT%H:%M:%S").replace(tzinfo=dt.timezone.utc)


def fetch(url: str, params: dict | None = None) -> bytes:
    if params:
        url = url + "?" + urllib.parse.urlencode(params, safe=":,")
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, "Accept": "application/json"})
    for attempt in range(4):
        try:
            with urllib.request.urlopen(request, timeout=90) as response:
                return response.read()
        except (urllib.error.URLError, TimeoutError) as error:
            if attempt == 3:
                raise SystemExit(f"fetch failed: {url}\n  {error}")
            time.sleep(2 ** attempt * 2)
    raise AssertionError("unreachable")


# --------------------------------------------------------------------------- sources

def fetch_geomag(station: str, start: dt.datetime, minutes: int, data_type: str) -> tuple[str, bytes]:
    params = {
        "id": station, "elements": "H", "sampling_period": "60", "format": "json", "type": data_type,
        "starttime": iso(start),
        "endtime": iso(start + dt.timedelta(minutes=minutes - 1)),   # inclusive
    }
    url = USGS_URL + "?" + urllib.parse.urlencode(params, safe=":")
    return url, fetch(url)


def fetch_kp(start: dt.datetime, minutes: int) -> tuple[str, bytes]:
    params = {"start": iso(start), "end": iso(start + dt.timedelta(minutes=minutes) - dt.timedelta(seconds=1)),
              "index": "Kp"}
    url = GFZ_URL + "?" + urllib.parse.urlencode(params, safe=":")
    return url, fetch(url)


def fetch_omni(start: dt.datetime, minutes: int) -> tuple[str, bytes]:
    params = {"id": OMNI_DATASET, "parameters": ",".join(OMNI_PARAMS),
              "time.min": iso(start), "time.max": iso(start + dt.timedelta(minutes=minutes)),  # max exclusive
              "format": "json", "include": "header"}
    url = HAPI_URL + "?" + urllib.parse.urlencode(params, safe=":,")
    return url, fetch(url)


# --------------------------------------------------------------------------- parsing onto the grid

def grid_geomag(body: bytes, start: dt.datetime, minutes: int) -> tuple[list, dict]:
    doc = json.loads(body)
    times = doc["times"]
    values = doc["values"][0]["values"]
    out = [None] * minutes
    for t, v in zip(times, values):
        i = int((parse_iso(t) - start).total_seconds() // 60)
        if 0 <= i < minutes and v is not None:
            out[i] = float(v)
    imo = doc["metadata"]["intermagnet"]["imo"]
    lon, lat, elev = imo["coordinates"]
    station = {"code": imo["iaga_code"], "name": imo["name"], "lat": lat, "lon": lon, "elevation_m": elev,
               "data_type": doc["metadata"]["intermagnet"].get("data_type")}
    return out, station


def grid_kp(body: bytes, start: dt.datetime, minutes: int) -> tuple[list, list]:
    doc = json.loads(body)
    intervals = sorted((parse_iso(t), float(k)) for t, k in zip(doc["datetime"], doc["Kp"]))
    out = [None] * minutes
    for t0, k in intervals:
        i0 = int((t0 - start).total_seconds() // 60)
        for i in range(max(0, i0), min(minutes, i0 + 180)):
            out[i] = k
    return out, sorted(set(doc.get("status", [])))


def grid_omni(body: bytes, start: dt.datetime, minutes: int) -> dict[str, list]:
    doc = json.loads(body)
    params = doc["parameters"][1:]                       # [0] is Time
    fills = [float(p["fill"]) for p in params]
    names = [p["name"] for p in params]
    key = {"BY_GSM": "by", "BZ_GSM": "bz", "flow_speed": "v", "proton_density": "n"}
    out = {key[n]: [None] * minutes for n in names}
    for row in doc["data"]:
        i = int((parse_iso(row[0]) - start).total_seconds() // 60)
        if not 0 <= i < minutes:
            continue
        for name, fill, value in zip(names, fills, row[1:]):
            if value is None or abs(float(value)) >= abs(fill) * 0.9999:
                continue
            out[key[name]][i] = float(value)
    return out


# --------------------------------------------------------------------------- outputs

def count_bays(h: list, threshold_nt: float = 150.0, separation_min: int = 90) -> list[int]:
    """Indices of negative H bays: H below median − threshold, separated by ≥ separation_min."""
    valid = [x for x in h if x is not None]
    base = statistics.median(valid)
    bays, last = [], -10**9
    for i, x in enumerate(h):
        if x is None or x >= base - threshold_nt:
            continue
        if i - last >= separation_min:
            bays.append(i)
        last = i
    return bays


def build_channels(geomag_body: bytes, kp_body: bytes, omni_body: bytes,
                   start: dt.datetime, minutes: int) -> tuple[dict[str, list], dict, list]:
    """Raw responses → track channels on the 1-min grid (pure; no network)."""
    h, station_meta = grid_geomag(geomag_body, start, minutes)
    kp, kp_status = grid_kp(kp_body, start, minutes)
    sw = grid_omni(omni_body, start, minutes)
    return {"h": h, "kp": kp, **sw}, station_meta, kp_status


def render_track(day: str, start: dt.datetime, minutes: int, channels: dict[str, list]) -> str:
    def fmt(values: list, decimals: int) -> str:
        return "[" + ",".join("null" if v is None else repr(round(v, decimals)) for v in values) + "]"

    lines = ["{",
             f'"day":"{day}",',
             f'"start":"{iso(start)}",',
             '"step_s":60,',
             f'"length":{minutes},',
             '"units":' + json.dumps({k: u for k, (u, _) in CHANNELS.items()}, separators=(",", ":")) + ",",
             '"channels":{']
    names = list(CHANNELS)
    for j, name in enumerate(names):
        comma = "," if j < len(names) - 1 else ""
        lines.append(f'"{name}":' + fmt(channels[name], CHANNELS[name][1]) + comma)
    lines += ["}", "}"]
    return "\n".join(lines) + "\n"


def rebuild_track(folder: Path) -> str:
    """Re-derive track.json text from a recorded day's raw files (offline)."""
    manifest = json.loads((folder / "manifest.json").read_text(encoding="utf-8"))
    files = {s["id"]: folder / s["file"] for s in manifest["sources"]}
    start = parse_iso(manifest["span"]["start"])
    minutes = manifest["span"]["length"]
    channels, _, _ = build_channels(files["geomag"].read_bytes(), files["kp"].read_bytes(),
                                    files["solarwind"].read_bytes(), start, minutes)
    return render_track(manifest["day"], start, minutes, channels)


def gap_percent(values: list) -> float:
    return round(100.0 * sum(1 for v in values if v is None) / len(values), 2)


def record_day(day: str, hours: int, station: str, geomag_type: str, why: str, out_root: Path) -> None:
    start = dt.datetime.fromisoformat(day).replace(tzinfo=dt.timezone.utc)
    minutes = hours * 60
    folder = out_root / day
    folder.mkdir(parents=True, exist_ok=True)

    print(f"{day}: fetching {hours} h for {station} …")
    geomag_url, geomag_body = fetch_geomag(station, start, minutes, geomag_type)
    kp_url, kp_body = fetch_kp(start, minutes)
    omni_url, omni_body = fetch_omni(start, minutes)

    geomag_file = f"geomag_{station}.json"
    (folder / geomag_file).write_bytes(geomag_body)
    (folder / "kp.json").write_bytes(kp_body)
    (folder / "solarwind.json").write_bytes(omni_body)

    channels, station_meta, kp_status = build_channels(geomag_body, kp_body, omni_body, start, minutes)
    (folder / "track.json").write_text(render_track(day, start, minutes, channels), encoding="utf-8")
    h, kp, sw = channels["h"], channels["kp"], channels

    valid_h = [x for x in h if x is not None]
    h_median = statistics.median(valid_h)
    bays = count_bays(h)
    summary = {
        "max_kp": max(x for x in kp if x is not None),
        "min_bz_nT": min(x for x in sw["bz"] if x is not None),
        "max_speed_km_s": max(x for x in sw["v"] if x is not None),
        "h_median_nT": round(h_median, 2),
        "max_abs_dh_nT": round(max(abs(x - h_median) for x in valid_h), 1),
        "negative_h_bays_utc": [iso(start + dt.timedelta(minutes=i)) for i in bays],
        "gaps_percent": {name: gap_percent(values) for name, values in channels.items()},
        "kp_status": kp_status,
    }
    manifest = {
        "day": day,
        "span": {"start": iso(start), "end": iso(start + dt.timedelta(minutes=minutes)), "step_s": 60,
                 "length": minutes},
        "why": why,
        "station": station_meta,
        "sources": [
            {"id": "geomag", "file": geomag_file, "provider": "USGS Geomagnetism Program", "url": geomag_url,
             "licence": "US public domain", "credit": "U.S. Geological Survey Geomagnetism Program"},
            {"id": "kp", "file": "kp.json", "provider": "GFZ German Research Centre for Geosciences", "url": kp_url,
             "licence": "CC BY 4.0",
             "credit": "Matzka, J. et al. (2021), Geomagnetic Kp index, GFZ Data Services, doi:10.5880/Kp.0001"},
            {"id": "solarwind", "file": "solarwind.json", "provider": "NASA GSFC SPDF (OMNIWeb / CDAWeb)",
             "url": omni_url, "licence": "public (NASA open data), cite OMNI",
             "credit": "NASA/GSFC's Space Physics Data Facility's OMNIWeb/CDAWeb service, and OMNI data"},
        ],
        "track": {"file": "track.json", "units": {k: u for k, (u, _) in CHANNELS.items()},
                  "notes": "1-min grid; null = gap; kp stepped per 3 h interval; h absolute (not baseline-removed); "
                           "OMNI fill values mapped to null"},
        "summary": summary,
        "retrieved_at": iso(dt.datetime.now(dt.timezone.utc).replace(microsecond=0)),
    }
    (folder / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    s = summary
    print(f"  max Kp {s['max_kp']:.3f} · min Bz {s['min_bz_nT']:.1f} nT · max v {s['max_speed_km_s']:.0f} km/s · "
          f"max |ΔH| {s['max_abs_dh_nT']:.0f} nT · bays {len(bays)}")
    print(f"  gaps % {s['gaps_percent']}")
    print(f"  → {folder.relative_to(REPO) if folder.is_relative_to(REPO) else folder}")


def record_live_fixtures(stamp: str) -> None:
    for name, (url, keep) in SWPC_LIVE.items():
        body = fetch(url)
        path = REPO / "tests" / "fixtures" / f"{name}_{stamp}.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        if keep is None:
            path.write_bytes(body)
            note = "verbatim"
        else:
            entries = json.loads(body)[:keep]         # RTSW lists are newest first
            path.write_text("[\n" + ",\n".join(json.dumps(e, separators=(",", ":")) for e in entries) + "\n]\n",
                            encoding="utf-8")
            note = f"newest {keep} entries, re-serialised"
        print(f"  {path.relative_to(REPO)} ({path.stat().st_size} B, {note})")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--day", help="first UTC day, YYYY-MM-DD")
    parser.add_argument("--hours", type=int, default=48)
    parser.add_argument("--station", default="CMO")
    parser.add_argument("--geomag-type", default="quasi-definitive",
                        help="USGS data type; quasi-definitive is gap-free for past days but appears only "
                             "after ~1-3 months (the default 'adjusted' had a 7 h gap on 2024-05-11)")
    parser.add_argument("--why", default="", help="why this day was chosen (goes into the manifest)")
    parser.add_argument("--out", type=Path, default=REPO / "resources" / "sky")
    parser.add_argument("--live-fixtures", action="store_true", help="record current SWPC products as fixtures")
    parser.add_argument("--rebuild", action="store_true",
                        help="offline: re-derive track.json of --day from its stored raw files")
    args = parser.parse_args(argv)

    if args.rebuild:
        if not args.day:
            parser.error("--rebuild needs --day")
        folder = args.out / args.day
        (folder / "track.json").write_text(rebuild_track(folder), encoding="utf-8")
        print(f"rebuilt {folder / 'track.json'}")
        return 0
    if args.live_fixtures:
        record_live_fixtures(dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%d"))
        return 0
    if not args.day:
        parser.error("--day or --live-fixtures is required")
    record_day(args.day, args.hours, args.station, args.geomag_type, args.why, args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
