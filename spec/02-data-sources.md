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
| `geomag` | H-component deviation at the observer's station (nT) → **assimilation target** for local disturbance | USGS Geomagnetism `geomag.usgs.gov/ws/data/?id=CMO&elements=H&sampling_period=60` (1 s available) | 1 min | US public domain | in POC (BOU, CMO); factory data in `resources/world/geomag/` |
| `kp` | Planetary Kp (0–9) → **activity level**, storm "weather" | NOAA SWPC `services.swpc.noaa.gov/products/noaa-planetary-k-index.json` (3 h) and `json/planetary_k_index_1m.json` (1-min estimate) | 1 min / 3 h | US public domain | to verify (T1.1) |
| `solarwind` | Speed v (km/s), density n (cm⁻³), IMF Bz (nT, GSM) → **reservoir loading** (southward Bz × v) | NOAA SWPC `products/solar-wind/plasma-1-day.json`, `products/solar-wind/mag-1-day.json` (DSCOVR / ACE at L1) | 1 min | US public domain | to verify (T1.1) |

Historical sources for Journey days (same quantities):

| ID | Historical source | Licence | Status |
|---|---|---|---|
| `geomag` | same USGS service with `starttime`/`endtime` for past dates | public domain | to verify (T1.1) |
| `kp` | GFZ Potsdam Kp (`kp.gfz-potsdam.de`, definitive Kp, JSON API) | CC BY 4.0 (credit GFZ) | to verify (T1.1) |
| `solarwind` | NASA OMNI 1-min, time-shifted to the bow shock (OMNIWeb / CDAWeb HAPI, dataset `OMNI_HRO_1MIN`) | public, cite OMNI | to verify (T1.1) |

**Bundled storm days** (factory sky, Journey presets, test fixtures):
- 10–11 May 2024 ("Gannon" storm, Kp 9): the reference day.
- One quiet day and one moderate substorm day (to pick in T1.1).

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
