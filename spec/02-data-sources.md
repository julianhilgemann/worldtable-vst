# 02 — Data sources

## Selection criteria
1. Free, ideally keyless (or free key), stable, documented, CORS irrelevant (native app).
2. Updates on a human timescale (minutes–hours) — good for ambient.
3. Licence allows use in a commercial plugin (check & record per source).
4. Globally meaningful ("hearing the world"), and interesting variance.

## Candidate catalogue (verify every endpoint in Phase 1 — task T1.3)

| ID | Domain | Source (provider) | Typical update | Auth | Natural feel → suggested mapping |
|---|---|---|---|---|---|
| `quake` | Seismology | USGS earthquake GeoJSON feeds (past hour/day) | ~1 min | none | Magnitude/energy → low-end weight, rumble, table harmonics; count → density |
| `kp` | Space weather | NOAA SWPC planetary K-index | 3 h (1-min estimates exist) | none | Storm level → shimmer, brightness, reverb size |
| `solarwind` | Space weather | NOAA SWPC solar wind (speed, density, Bz) | ~1 min | none | Speed → LFO rate, density → noise layer, Bz sign → stereo tilt |
| `weather` | Atmosphere | Open-Meteo forecast/current (temp, wind, pressure, humidity) at focus point | 15 min–1 h | none | Wind → filter motion, pressure → root drift, humidity → reverb damping |
| `co2` | Climate | NOAA GML Mauna Loa CO₂ (daily/monthly) | daily | none | Very slow drift → table position over months ("the long tone") |
| `sunspots` | Solar | SILSO sunspot number | daily/monthly | none | Cycle phase → global brightness macro |
| `ocean` | Oceans | NOAA NDBC buoys (wave height, period, water temp) | ~30–60 min | none | Wave period → LFO1 rate (literally swell), height → amplitude breathing |
| `iss` | Orbit | ISS position API | seconds | none | Lat/lon → pan + slow pitch glide (sparingly) |
| `tides` | Oceans | NOAA CO-OPS water levels (station) | 6 min | none | Tide curve → table morph (~12.4 h cycle) |
| `nasa` | Space | NASA DONKI (flares, CMEs) | event-driven | api.nasa.gov key | Events → v1.1 "event voices" |

Optional later: Copernicus/ECMWF (heavy, keyed), GBIF biodiversity, OpenAQ air quality, lightning networks.

## Adapter contract (every source implements this)

```cpp
struct DataReading {            // one normalisable scalar series
    juce::String channel;       // e.g. "quake.energy"
    double       timestampUtc;  // seconds
    double       value;         // raw unit
};

class DataSource {
public:
    virtual ~DataSource() = default;
    virtual juce::String id() const = 0;
    virtual juce::StringArray channels() const = 0;       // what it can provide
    virtual juce::RelativeTime minInterval() const = 0;   // respect rate limits
    virtual juce::URL buildRequest(const WorldFocus&) const = 0;
    virtual juce::Result parse(const juce::MemoryBlock& body,
                               std::vector<DataReading>& out) const = 0; // pure, testable
};
```

`parse()` is **pure** → unit-tested against `tests/fixtures/<id>/*.json`.
The scheduler (not the adapter) does HTTP, retries with backoff, and caching.

## Normalisation (raw → 0..1)
Per channel, configurable:
- `minmax_window` — rolling min/max over a window (e.g. 7 days).
- `zscore_sigmoid` — rolling mean/σ, then logistic squash (robust default).
- `log` — for heavy-tailed data (earthquake energy, flare flux).
- `fixed_range` — known physical bounds (Kp 0–9).
Then: slew (seconds–minutes), curve, invert. History stored in `DataCache`
so the normaliser has context on first launch.

## Offline & reproducibility
- Cache: last responses + rolling history per channel on disk (app-data dir), JSON.
- Bundled "factory world": 30 days of recorded data per source → works with no network.
- Freeze: the current `WorldSnapshot` + built tables are serialised into plugin state.
- Offline render in a DAW (non-realtime): use frozen/cached data only, never fetch.

## Ethics / etiquette
- Identify with a generic User-Agent (`Worldtable/<version>`), no personal data.
- Respect each provider's documented rate limit; default interval ≥ 5 min.
- Credit data providers in the About panel and manual.
