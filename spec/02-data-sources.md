# 02 — Data sources (v1: magnetosphere only)

v1 hears one phenomenon (ADR-0002). Every source below is a scalar time series,
so the scalar `DataReading` contract fits. Research on other sources is kept
in `spec/parked/sister-data-sources.md`.

## Selection criteria
1. Free, keyless (or free key), stable, documented.
2. Updates on a human timescale (1 min – 3 h).
3. **Redistributable** (ADR-0005): terms allow bundling recorded data in a
   public repo and in the plug-in, with credit. Record the licence per source.
4. Historical data available for the same quantity, so Journey can replay
   recorded days with the same pipeline.

## Sources

| ID | Quantity → Sky Model role | Live source | Cadence | Licence | Status |
|---|---|---|---|---|---|
| `geomag` | H-component deviation at the observer's station (nT) → **assimilation target** for local disturbance | USGS Geomagnetism `https://geomag.usgs.gov/ws/data/?id=CMO&elements=H&sampling_period=60&format=json&starttime=…&endtime=…` (endtime inclusive; 1 s available) | 1 min | US public domain | verified 2026-11-05; in POC (BOU, CMO) |
| `kp` | Planetary Kp (0–9) → **activity level**, storm "weather" | NOAA SWPC `https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json` (3 h, ~8 days, list of objects `{time_tag, Kp, a_running, station_count}`) and `https://services.swpc.noaa.gov/json/planetary_k_index_1m.json` (1-min estimate, `{time_tag, kp_index, estimated_kp, kp}`) | 1 min / 3 h | US public domain | verified 2026-11-05; fixtures in `tests/fixtures/kp/` |
| `solarwind` | Speed v (km/s), density n (cm⁻³), IMF Bz (nT, GSM) → **reservoir loading** (southward Bz × v) | NOAA SWPC real-time solar wind `https://services.swpc.noaa.gov/json/rtsw/rtsw_wind_1m.json` and `…/rtsw_mag_1m.json` (~24 h, newest first, several spacecraft interleaved — SOLAR1, IMAP, ACE — keep entries with `active: true`; 1.4 and 2.4 MB) | 1 min | US public domain | verified 2026-11-05; fixtures (newest 120 entries) in `tests/fixtures/solarwind/`. The old `products/solar-wind/*-1-day.json` URLs return 404 |

Historical sources for Journey days (same quantities):

| ID | Historical source | Licence | Status |
|---|---|---|---|
| `geomag` | same USGS service, `type=quasi-definitive` (gap-free; published ~1–3 months after the fact; the default `adjusted` type had a 7 h gap on 2024-05-11) | public domain | verified 2026-11-05 |
| `kp` | GFZ Kp API `https://kp.gfz.de/app/json/?start=…&end=…&index=Kp` (the old `kp.gfz-potsdam.de` host redirects); response `{Kp:[…], datetime:[…], status:[def…], meta:{license:"CC BY 4.0"}}` | CC BY 4.0, cite doi:10.5880/Kp.0001 | verified 2026-11-05 |
| `solarwind` | NASA OMNI 1-min, time-shifted to the bow shock: CDAWeb HAPI 2.0 `https://cdaweb.gsfc.nasa.gov/hapi/data?id=OMNI_HRO_1MIN&parameters=BY_GSM,BZ_GSM,flow_speed,proton_density&time.min=…&time.max=…&format=json&include=header` (parameters in dataset order; `time.max` exclusive; fill values 9999.99 / 99999.9 / 999.99) | public (NASA open data), acknowledge OMNI | verified 2026-11-05; data through ~mid-Oct 2026 |

**Bundled sky days** (`resources/sky/`, recorded by `tools/record_sky_day.py`; factory sky, Journey presets, test fixtures):
- `2024-05-10`: the "Gannon" storm (Kp 9), the reference day. OMNI plasma is
  missing before 08:04 UT and has 15–30 min gaps during the storm.
- `2025-01-19`: moderate (Kp 4.33), two substorm nights over CMO.
- `2025-01-25`: quiet (Kp ≤ 1.0).

**Additional stations** (each adds a point of view, ADR-0006): candidate
Tromsø (TRO). Check data access and terms first; some INTERMAGNET observatories
publish under non-commercial or bespoke terms.

## Adapter contract (every source implements this)

```cpp
struct DataReading {            // one scalar series sample
    juce::String channel;       // e.g. "solarwind.bz"
    double       timestampUtc;  // seconds
    double       value;         // raw physical unit
};

class DataSource {
public:
    virtual ~DataSource() = default;
    virtual juce::String id() const = 0;
    virtual juce::StringArray channels() const = 0;       // what it can provide
    virtual juce::RelativeTime minInterval() const = 0;   // respect rate limits
    virtual juce::URL buildRequest(const DataRequest&) const = 0;  // station + time span
    virtual juce::Result parse(const juce::MemoryBlock& body,
                               std::vector<DataReading>& out) const = 0; // pure, testable
};
```

`DataRequest` carries the station and a UTC time span, so the same adapter
serves Live (span ending now) and Journey (a past span). `parse()` is
**pure** and unit-tested against `tests/fixtures/<id>/*`. The scheduler, not the
adapter, does HTTP, retries with backoff, and caching. The POC parsers
(`source/data/WorldSources.*`) become the first adapters when `DataSource.h`
is created (protected interface, review first).

## From readings to the Sky Model
- The data layer resamples each channel onto a 1-minute grid, fills short gaps
  (≤ 10 min) by interpolation, and marks longer gaps invalid.
- `WorldSnapshot` hands the model a **track**: a fixed-capacity window of each
  channel (e.g. 72 h × 1 min = 4320 samples × 6 channels), plus validity. The
  model reads the value at its current model time (`03-architecture.md`).
- H deviation = H minus a quiet-time baseline (proposal: median of the
  preceding 24 h; refine in T1.2).
- Sky channels exposed as mod sources are normalised to 0..1 with fixed
  physical ranges (Kp 0–9, v 250–900 km/s, Bz −30…+30 nT) or a rolling
  z-score + logistic squash; then slewed (≥ 2 s).

## Offline & reproducibility
- Cache: last responses + rolling history per channel on disk
  (`~/Library/Application Support/Worldtable/cache/`, as in the POC).
- Bundled storm days → works with no network; Live falls back to the newest
  cached day.
- Freeze: the data track, model state and seed are stored in plugin state.
- Offline render in a DAW (non-realtime): cached or bundled data only, never fetch.

## Etiquette
- User-Agent `Worldtable/<version> (+https://github.com/julianhilgemann/worldtable-vst)`.
  Never send the user's personal data.
- Respect each provider's documented rate limit; SWPC and USGS 1-min products
  are polled no more than once per minute, default every 5 min.
- Credit data providers (USGS, NOAA SWPC, GFZ, NASA OMNI) in the About panel and README.
